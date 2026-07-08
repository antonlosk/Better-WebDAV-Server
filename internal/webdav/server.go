package webdav

import (
	"betterwebdav/internal/auth"
	"betterwebdav/internal/config"
	"betterwebdav/internal/logs"
	"betterwebdav/web"
	"context"
	"fmt"
	"html/template"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"

	"golang.org/x/net/webdav"
)

var (
	server     *http.Server
	mu         sync.Mutex
	status     = "Stopped"
	startTime  time.Time
	serverDone chan struct{}
)

// safeResponseWriter предотвращает панику и ошибки двойной записи заголовков (superfluous WriteHeader),
// если Java-клиент обрывает соединение раньше времени.
type safeResponseWriter struct {
	http.ResponseWriter
	wroteHeader bool
	mu          sync.Mutex
}

func (w *safeResponseWriter) WriteHeader(code int) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.wroteHeader {
		return
	}
	w.wroteHeader = true
	w.ResponseWriter.WriteHeader(code)
}

func (w *safeResponseWriter) Write(b []byte) (int, error) {
	w.mu.Lock()
	if !w.wroteHeader {
		w.wroteHeader = true
		w.ResponseWriter.WriteHeader(http.StatusOK)
	}
	w.mu.Unlock()
	return w.ResponseWriter.Write(b)
}

type webdavLogFilter struct{}

func (f *webdavLogFilter) Write(p []byte) (n int, err error) {
	msg := string(p)
	if strings.Contains(msg, "superfluous response.WriteHeader") {
		return len(p), nil
	}
	logs.Log("WARNING", "Internal HTTP Server: "+strings.TrimSpace(msg))
	return len(p), nil
}

func Status() (string, string) {
	mu.Lock()
	defer mu.Unlock()
	uptime := "0s"
	if status == "Running" {
		uptime = time.Since(startTime).Round(time.Second).String()
	}
	return status, uptime
}

func StartServer() error {
	mu.Lock()
	defer mu.Unlock()

	if status == "Running" {
		return nil
	}

	cfg := config.GetConfig()
	fs := &webdav.Handler{
		Prefix:     "",
		FileSystem: webdav.Dir(cfg.SharedPath),
		LockSystem: webdav.NewMemLS(),
	}

	mux := http.NewServeMux()
	mux.Handle("/static/web/", http.StripPrefix("/static/web/", http.FileServer(http.FS(web.FS))))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		ip := auth.GetClientIP(r)

		if auth.IsLockedOut(ip) {
			http.Error(w, "Too many failed attempts. Try again later.", http.StatusTooManyRequests)
			return
		}

		user, pass, ok := r.BasicAuth()
		if !ok {
			w.Header().Set("WWW-Authenticate", `Basic realm="WebDAV Server"`)
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}

		if !auth.AuthenticateWebDAV(user, pass) {
			auth.RecordFailedLogin(ip, "WebDAV")
			logs.Log("WARNING", fmt.Sprintf("Failed WebDAV auth attempt from IP: %s", ip))
			w.Header().Set("WWW-Authenticate", `Basic realm="WebDAV Server"`)
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}

		auth.ResetLoginAttempts(ip)

		// Оборачиваем Writer для защиты от обрывов связи клиентом
		sw := &safeResponseWriter{ResponseWriter: w}

		if r.Method == "PUT" {
			// ИСПРАВЛЕНИЕ: Принудительно отключаем Keep-Alive для тяжелых загрузок.
			// Это заставляет Java/Android клиенты закрывать сессию корректно и спасает от ошибки "interrupted".
			r.Header.Set("Connection", "close")
			sw.Header().Set("Connection", "close")

			logs.Log("INFO", fmt.Sprintf("Upload started: %s by %s (IP: %s)", r.URL.Path, user, ip))
			defer func() {
				logs.Log("INFO", fmt.Sprintf("Upload finished: %s by %s (IP: %s)", r.URL.Path, user, ip))
			}()
		} else if r.Method == "DELETE" {
			logs.Log("INFO", fmt.Sprintf("Resource deleted: %s by %s (IP: %s)", r.URL.Path, user, ip))
		} else if r.Method == "MKCOL" {
			logs.Log("INFO", fmt.Sprintf("Directory created: %s by %s (IP: %s)", r.URL.Path, user, ip))
		} else if r.Method == "MOVE" {
			dest := r.Header.Get("Destination")
			if u, err := url.Parse(dest); err == nil {
				dest = u.Path
			}
			logs.Log("INFO", fmt.Sprintf("Resource moved/renamed: %s -> %s by %s (IP: %s)", r.URL.Path, dest, user, ip))
		}

		if r.Method == http.MethodGet {
			ctx := context.Background()
			fInfo, err := fs.FileSystem.Stat(ctx, r.URL.Path)
			if err == nil {
				if fInfo.IsDir() {
					if len(r.URL.Path) > 0 && r.URL.Path[len(r.URL.Path)-1] != '/' {
						http.Redirect(sw, r, r.URL.Path+"/", http.StatusFound)
						return
					}
					serveDirectoryListing(sw, r, fs.FileSystem, r.URL.Path)
					return
				} else {
					logs.Log("INFO", fmt.Sprintf("Download started: %s by %s (IP: %s)", r.URL.Path, user, ip))
					defer func() {
						logs.Log("INFO", fmt.Sprintf("Download finished: %s by %s (IP: %s)", r.URL.Path, user, ip))
					}()
				}
			}
		}

		// Передаем безопасный Writer в стандартный обработчик
		fs.ServeHTTP(sw, r)
	})

	server = &http.Server{
		Addr:     ":" + cfg.WebDAVPort,
		Handler:  mux,
		ErrorLog: log.New(&webdavLogFilter{}, "", 0),
	}

	ln, err := net.Listen("tcp", server.Addr)
	if err != nil {
		logs.Log("ERROR", "Failed to bind WebDAV port: "+err.Error())
		return err
	}

	status = "Running"
	startTime = time.Now()
	serverDone = make(chan struct{})

	logs.Log("INFO", "WebDAV Server started on port "+cfg.WebDAVPort+" mapping to "+cfg.SharedPath)

	go func() {
		defer close(serverDone)

		err := server.Serve(ln)

		mu.Lock()
		status = "Stopped"
		mu.Unlock()

		if err != nil && err != http.ErrServerClosed {
			logs.Log("ERROR", "WebDAV Server error: "+err.Error())
		}
	}()

	return nil
}

func StopServer() {
	mu.Lock()
	if server == nil || status != "Running" {
		mu.Unlock()
		return
	}

	srv := server
	done := serverDone
	mu.Unlock()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	srv.Shutdown(ctx)
	<-done

	logs.Log("INFO", "WebDAV Server stopped")
}

func RestartServer() {
	StopServer()
	StartServer()
}

type ExplorerItem struct {
	Name      string
	URL       string
	Icon      string
	IconClass string
	Size      string
	ModTime   string
}

type ExplorerData struct {
	Path  string
	Items []ExplorerItem
}

func serveDirectoryListing(w http.ResponseWriter, r *http.Request, fileSystem webdav.FileSystem, path string) {
	ctx := context.Background()
	f, err := fileSystem.OpenFile(ctx, path, os.O_RDONLY, 0)
	if err != nil {
		http.Error(w, "Error accessing directory", http.StatusInternalServerError)
		return
	}
	defer f.Close()

	dirs, err := f.Readdir(-1)
	if err != nil {
		http.Error(w, "Error reading directory", http.StatusInternalServerError)
		return
	}

	var items []ExplorerItem

	for _, d := range dirs {
		name := d.Name()
		isDir := d.IsDir()

		icon := "insert_drive_file"
		iconClass := "file-icon"
		if isDir {
			icon = "folder"
			iconClass = ""
			name += "/"
		}

		sizeStr := "-"
		if !isDir {
			sizeStr = formatSize(d.Size())
		}

		modTime := d.ModTime().Format("2006-01-02 15:04:05")
		escapedURL := url.PathEscape(d.Name())
		if isDir {
			escapedURL += "/"
		}

		items = append(items, ExplorerItem{
			Name:      name,
			URL:       escapedURL,
			Icon:      icon,
			IconClass: iconClass,
			Size:      sizeStr,
			ModTime:   modTime,
		})
	}

	data := ExplorerData{
		Path:  path,
		Items: items,
	}

	t, err := template.ParseFS(web.FS, "templates/explorer.html")
	if err != nil {
		logs.Log("ERROR", "Failed to parse explorer template: "+err.Error())
		http.Error(w, "Internal Server Error (Template Parse)", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := t.Execute(w, data); err != nil {
		logs.Log("ERROR", "Failed to execute explorer template: "+err.Error())
	}
}

func formatSize(bytes int64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := int64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}
