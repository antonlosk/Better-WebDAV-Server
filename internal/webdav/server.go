package webdav

import (
	"betterwebdav/internal/auth"
	"betterwebdav/internal/config"
	"betterwebdav/internal/logs"
	"betterwebdav/web"
	"context"
	"fmt"
	"html/template"
	"net"
	"net/http"
	"net/url"
	"os"
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

	// ==============================================================
	// ИСПРАВЛЕНИЕ: Раздаем встроенный CSS для страницы WebDAV!
	// ==============================================================
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
			logs.Log("WARNING", fmt.Sprintf("Failed WebDAV auth attempt from %s", ip))
			w.Header().Set("WWW-Authenticate", `Basic realm="WebDAV Server"`)
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}

		auth.ResetLoginAttempts(ip)

		if r.Method == "PUT" {
			logs.Log("INFO", fmt.Sprintf("File uploaded: %s by %s", r.URL.Path, user))
		} else if r.Method == "DELETE" {
			logs.Log("INFO", fmt.Sprintf("Resource deleted: %s by %s", r.URL.Path, user))
		}

		if r.Method == http.MethodGet {
			ctx := context.Background()
			fInfo, err := fs.FileSystem.Stat(ctx, r.URL.Path)
			if err == nil && fInfo.IsDir() {
				if len(r.URL.Path) > 0 && r.URL.Path[len(r.URL.Path)-1] != '/' {
					http.Redirect(w, r, r.URL.Path+"/", http.StatusFound)
					return
				}
				serveDirectoryListing(w, r, fs.FileSystem, r.URL.Path)
				return
			}
		}

		fs.ServeHTTP(w, r)
	})

	server = &http.Server{Addr: ":" + cfg.WebDAVPort, Handler: mux}

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