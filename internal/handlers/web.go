package handlers

import (
	"betterwebdav/internal/auth"
	"betterwebdav/internal/config"
	"betterwebdav/internal/database"
	"betterwebdav/internal/logs"
	"betterwebdav/internal/webdav"
	"betterwebdav/web"
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"html"
	"html/template"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/sessions"
	"github.com/shirou/gopsutil/v3/disk"
)

var (
	store    *sessions.CookieStore
	uiServer *http.Server
	uiMu     sync.Mutex
)

func initSessionStore() {
	keyPath := "data/session.key"
	var key []byte
	var err error

	key, err = os.ReadFile(keyPath)
	if err != nil || len(key) != 32 {
		key = make([]byte, 32)
		if _, err := rand.Read(key); err != nil {
			logs.Log("CRITICAL", "Failed to generate secure random key: "+err.Error())
			panic(err)
		}
		if err := os.WriteFile(keyPath, key, 0600); err != nil {
			logs.Log("ERROR", "Failed to save session key to disk: "+err.Error())
		} else {
			logs.Log("INFO", "Generated new secure session secret key")
		}
	}

	store = sessions.NewCookieStore(key)
	store.Options = &sessions.Options{
		Path:     "/",
		MaxAge:   86400 * 30,
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		Secure:   false,
	}
}

type contextKey string

const csrfContextKey contextKey = "csrf_token"

func generateCSRFToken() string {
	b := make([]byte, 32)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func csrfProtect(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if len(r.URL.Path) >= 8 && r.URL.Path[:8] == "/static/" {
			next.ServeHTTP(w, r)
			return
		}
		if r.URL.Path == "/favicon.ico" {
			http.Error(w, "Not Found", http.StatusNotFound)
			return
		}

		session, _ := store.Get(r, "admin-session")
		token, ok := session.Values["csrf_token"].(string)

		if !ok || token == "" {
			token = generateCSRFToken()
			session.Values["csrf_token"] = token
			session.Save(r, w)
		}

		ctx := context.WithValue(r.Context(), csrfContextKey, token)
		r = r.WithContext(ctx)

		if r.Method == "POST" {
			formToken := r.FormValue("csrf_token")
			if formToken == "" || formToken != token {
				logs.Log("WARNING", fmt.Sprintf("Blocked invalid CSRF attempt from %s", r.RemoteAddr))
				http.Error(w, "Forbidden - Invalid CSRF Token", http.StatusForbidden)
				return
			}
		}

		next.ServeHTTP(w, r)
	})
}

func getCSRFField(r *http.Request) template.HTML {
	token, _ := r.Context().Value(csrfContextKey).(string)
	return template.HTML(fmt.Sprintf(`<input type="hidden" name="csrf_token" value="%s">`, html.EscapeString(token)))
}

func createMux() *http.ServeMux {
	mux := http.NewServeMux()
	mux.Handle("/static/web/", http.StripPrefix("/static/web/", http.FileServer(http.FS(web.FS))))

	mux.HandleFunc("/", authMiddleware(dashboardHandler))
	mux.HandleFunc("/setup", setupHandler)
	mux.HandleFunc("/login", loginHandler)
	mux.HandleFunc("/logout", logoutHandler)
	mux.HandleFunc("/settings", authMiddleware(settingsHandler))
	mux.HandleFunc("/admins", authMiddleware(adminsHandler))
	mux.HandleFunc("/users", authMiddleware(usersHandler))
	mux.HandleFunc("/logs", authMiddleware(logsHandler))
	mux.HandleFunc("/api/control", authMiddleware(controlHandler))

	return mux
}

func StartWebServer() {
	initSessionStore()

	cfg := config.GetConfig()
	mux := createMux()

	uiMu.Lock()
	uiServer = &http.Server{
		Addr:    ":" + cfg.WebUIPort,
		Handler: csrfProtect(mux),
	}
	uiMu.Unlock()

	logs.Log("INFO", "Web Management UI starting on port "+cfg.WebUIPort)

	go func() {
		if err := uiServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logs.Log("ERROR", "Failed to start Web Management UI: "+err.Error())
		}
	}()
}

func StopWebServer() {
	uiMu.Lock()
	defer uiMu.Unlock()

	if uiServer != nil {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		uiServer.Shutdown(ctx)
		logs.Log("INFO", "Web Management UI stopped")
	}
}

func RestartWebServer() {
	uiMu.Lock()
	defer uiMu.Unlock()

	if uiServer != nil {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		uiServer.Shutdown(ctx)
	}

	cfg := config.GetConfig()
	mux := createMux()

	uiServer = &http.Server{
		Addr:    ":" + cfg.WebUIPort,
		Handler: csrfProtect(mux),
	}

	logs.Log("INFO", "Web Management UI restarted on port "+cfg.WebUIPort)

	go func() {
		if err := uiServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logs.Log("ERROR", "Failed to restart Web Management UI: "+err.Error())
		}
	}()
}

func authMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !auth.AdminExists() {
			http.Redirect(w, r, "/setup", http.StatusFound)
			return
		}
		session, err := store.Get(r, "admin-session")
		if err != nil {
			logs.Log("WARNING", "Session decode error: "+err.Error())
		}
		if authCheck, ok := session.Values["authenticated"].(bool); !ok || !authCheck {
			http.Redirect(w, r, "/login", http.StatusFound)
			return
		}
		next(w, r)
	}
}

func renderTemplate(w http.ResponseWriter, r *http.Request, name string, data map[string]interface{}) {
	if data == nil {
		data = make(map[string]interface{})
	}
	data["CSRFField"] = getCSRFField(r)

	t, err := template.ParseFS(web.FS, "templates/layout.html", "templates/"+name+".html")
	if err != nil {
		logs.Log("ERROR", fmt.Sprintf("Template parsing error (%s): %v", name, err))
		http.Error(w, "Internal Server Error (Template Parse)", http.StatusInternalServerError)
		return
	}
	if err := t.ExecuteTemplate(w, "layout.html", data); err != nil {
		logs.Log("ERROR", fmt.Sprintf("Template execution error (%s): %v", name, err))
	}
}

func renderStandalone(w http.ResponseWriter, r *http.Request, name string, data map[string]interface{}) {
	if data == nil {
		data = make(map[string]interface{})
	}
	data["CSRFField"] = getCSRFField(r)

	t, err := template.ParseFS(web.FS, "templates/"+name+".html")
	if err != nil {
		logs.Log("ERROR", fmt.Sprintf("Template parsing error (%s): %v", name, err))
		http.Error(w, "Internal Server Error (Template Parse)", http.StatusInternalServerError)
		return
	}
	if err := t.Execute(w, data); err != nil {
		logs.Log("ERROR", fmt.Sprintf("Template execution error (%s): %v", name, err))
	}
}

func setupHandler(w http.ResponseWriter, r *http.Request) {
	if auth.AdminExists() {
		http.Redirect(w, r, "/login", http.StatusFound)
		return
	}
	if r.Method == "POST" {
		user, pass := r.FormValue("username"), r.FormValue("password")
		if user != "" && pass != "" {
			if err := auth.CreateAdmin(user, pass); err != nil {
				logs.Log("ERROR", "Failed to create admin: "+err.Error())
				http.Error(w, "Failed to create admin user", http.StatusInternalServerError)
				return
			}
			logs.Log("INFO", "Admin created successfully")
			http.Redirect(w, r, "/login", http.StatusFound)
			return
		}
	}
	renderStandalone(w, r, "setup", nil)
}

func loginHandler(w http.ResponseWriter, r *http.Request) {
	if !auth.AdminExists() {
		http.Redirect(w, r, "/setup", http.StatusFound)
		return
	}

	ip := auth.GetClientIP(r)

	// Isolate lockout check specifically for "admin" service
	if auth.IsLockedOut(ip, "admin") {
		renderStandalone(w, r, "login", map[string]interface{}{"Error": "Too many failed attempts. Try again in 5 minutes."})
		return
	}

	if r.Method == "POST" {
		user, pass := r.FormValue("username"), r.FormValue("password")

		if auth.AuthenticateAdmin(user, pass) {
			auth.ResetLoginAttempts(ip, "admin")

			session, _ := store.Get(r, "admin-session")
			session.Values["authenticated"] = true
			if err := session.Save(r, w); err != nil {
				logs.Log("ERROR", "Failed to save session during login: "+err.Error())
				http.Error(w, "Internal Server Error", http.StatusInternalServerError)
				return
			}
			logs.Log("INFO", "Admin logged in from IP: "+ip)
			http.Redirect(w, r, "/", http.StatusFound)
			return
		}

		auth.RecordFailedLogin(ip, "admin")
		logs.Log("WARNING", fmt.Sprintf("Failed admin login attempt from IP: %s", ip))
		renderStandalone(w, r, "login", map[string]interface{}{"Error": "Invalid credentials"})
		return
	}

	renderStandalone(w, r, "login", nil)
}

func logoutHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Redirect(w, r, "/", http.StatusFound)
		return
	}

	session, _ := store.Get(r, "admin-session")
	session.Values["authenticated"] = false
	if err := session.Save(r, w); err != nil {
		logs.Log("ERROR", "Failed to save session during logout: "+err.Error())
	}
	http.Redirect(w, r, "/login", http.StatusFound)
}

func formatBytes(bytes uint64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := uint64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

func dashboardHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	status, uptime := webdav.Status()
	cfg := config.GetConfig()

	var userCount int
	if err := database.DB.QueryRow("SELECT COUNT(*) FROM webdav_users").Scan(&userCount); err != nil {
		logs.Log("ERROR", "Failed to count users: "+err.Error())
	}

	diskTotal, diskFree, diskUsed, diskPercentStr := "Unknown", "Unknown", "Unknown", "0.0"
	diskPercentRaw := 0.0

	usage, err := disk.Usage(cfg.SharedPath)
	if err == nil {
		diskTotal = formatBytes(usage.Total)
		diskFree = formatBytes(usage.Free)
		diskUsed = formatBytes(usage.Used)
		diskPercentRaw = usage.UsedPercent
		diskPercentStr = fmt.Sprintf("%.1f", usage.UsedPercent)
	}

	data := map[string]interface{}{
		"Status":         status,
		"Uptime":         uptime,
		"Config":         cfg,
		"UserCount":      userCount,
		"DiskTotal":      diskTotal,
		"DiskFree":       diskFree,
		"DiskUsed":       diskUsed,
		"DiskPercent":    diskPercentStr,
		"DiskPercentRaw": diskPercentRaw,
	}
	renderTemplate(w, r, "dashboard", data)
}

func controlHandler(w http.ResponseWriter, r *http.Request) {
	action := r.FormValue("action")
	switch action {
	case "start":
		webdav.StartServer()
	case "stop":
		webdav.StopServer()
	case "restart":
		webdav.RestartServer()
	}
	http.Redirect(w, r, "/", http.StatusFound)
}

func settingsHandler(w http.ResponseWriter, r *http.Request) {
	msg := ""
	newUIPortToRedirect := ""

	if r.Method == "POST" {
		newPath := r.FormValue("shared_path")
		newDavPort := r.FormValue("webdav_port")
		newUIPort := r.FormValue("web_ui_port")
		newLogRetention := r.FormValue("log_retention")

		if newLogRetention == "" {
			newLogRetention = "1_year"
		}

		davPortInt, errDav := strconv.Atoi(newDavPort)
		uiPortInt, errUI := strconv.Atoi(newUIPort)

		if errDav != nil || davPortInt < 1 || davPortInt > 65535 || errUI != nil || uiPortInt < 1 || uiPortInt > 65535 {
			msg = "Error: Ports must be valid numbers between 1 and 65535!"
		} else if newDavPort == newUIPort {
			msg = "Error: WebDAV and Web UI ports must be different!"
		} else if _, err := os.Stat(newPath); os.IsNotExist(err) {
			msg = "Error: Directory does not exist!"
		} else {
			currentCfg := config.GetConfig()

			var lDav net.Listener
			var errListenDav error
			if newDavPort != currentCfg.WebDAVPort {
				lDav, errListenDav = net.Listen("tcp", ":"+newDavPort)
			}

			var lUI net.Listener
			var errListenUI error
			if newUIPort != currentCfg.WebUIPort {
				lUI, errListenUI = net.Listen("tcp", ":"+newUIPort)
			}

			if errListenDav != nil {
				msg = "Error: WebDAV Port is already in use!"
				if lUI != nil {
					lUI.Close()
				}
			} else if errListenUI != nil {
				msg = "Error: Web UI Port is already in use!"
				if lDav != nil {
					lDav.Close()
				}
			} else {
				if lDav != nil {
					lDav.Close()
				}
				if lUI != nil {
					lUI.Close()
				}

				uiPortChanged := (newUIPort != currentCfg.WebUIPort)
				davChanged := (newDavPort != currentCfg.WebDAVPort || newPath != currentCfg.SharedPath)

				err := config.SaveConfig(config.Settings{
					WebDAVPort:   newDavPort,
					WebUIPort:    newUIPort,
					SharedPath:   newPath,
					LogRetention: newLogRetention,
				})

				if err != nil {
					logs.Log("ERROR", "Failed to save configuration: "+err.Error())
					msg = "Error: Failed to save settings to database!"
				} else {
					go logs.CleanOldLogs()

					if davChanged {
						logs.Log("INFO", "WebDAV settings changed. Restarting WebDAV...")
						webdav.RestartServer()
					}

					if uiPortChanged {
						newUIPortToRedirect = newUIPort
						msg = fmt.Sprintf("Settings saved! Web UI port changed to %s. Redirecting...", newUIPort)
						go func() {
							time.Sleep(500 * time.Millisecond)
							RestartWebServer()
						}()
					} else {
						msg = "Settings saved successfully!"
					}
				}
			}
		}
	}

	renderTemplate(w, r, "settings", map[string]interface{}{
		"Config":    config.GetConfig(),
		"Message":   msg,
		"NewUIPort": newUIPortToRedirect,
	})
}

type AdminUser struct {
	ID       int
	Username string
}

func adminsHandler(w http.ResponseWriter, r *http.Request) {
	msg := ""
	isError := false

	if r.Method == "POST" {
		action := r.FormValue("action")
		id := r.FormValue("id")

		switch action {
		case "add":
			u, p := r.FormValue("username"), r.FormValue("password")
			if u == "" || p == "" {
				msg = "Username and password cannot be empty."
				isError = true
			} else {
				hash, err := auth.HashPassword(p)
				if err != nil {
					logs.Log("ERROR", "Password hashing failed for admin: "+err.Error())
					msg = "Failed to process password."
					isError = true
				} else {
					_, err = database.DB.Exec("INSERT INTO admin_users (username, password_hash) VALUES (?, ?)", u, hash)
					if err != nil {
						logs.Log("ERROR", "Failed to add administrator: "+err.Error())
						msg = "Failed to create administrator (username might already exist)."
						isError = true
					} else {
						logs.Log("INFO", "Added new administrator: "+u)
						msg = "Administrator created successfully."
					}
				}
			}
		case "delete":
			var count int
			database.DB.QueryRow("SELECT COUNT(*) FROM admin_users").Scan(&count)
			if count <= 1 {
				logs.Log("WARNING", "Blocked attempt to delete the last administrator")
				msg = "Cannot delete the only administrator account."
				isError = true
			} else {
				var deletedUser string
				database.DB.QueryRow("SELECT username FROM admin_users WHERE id = ?", id).Scan(&deletedUser)
				_, err := database.DB.Exec("DELETE FROM admin_users WHERE id = ?", id)
				if err != nil {
					logs.Log("ERROR", "Failed to delete administrator: "+err.Error())
					msg = "Failed to delete administrator."
					isError = true
				} else {
					logs.Log("INFO", "Deleted administrator: "+deletedUser)
					msg = "Administrator deleted successfully."
				}
			}
		case "edit_username":
			newUsername := r.FormValue("new_username")
			if newUsername == "" {
				msg = "Username cannot be empty."
				isError = true
			} else {
				_, err := database.DB.Exec("UPDATE admin_users SET username = ? WHERE id = ?", newUsername, id)
				if err != nil {
					logs.Log("ERROR", "Failed to update admin username: "+err.Error())
					msg = "Failed to update username (it might already be in use)."
					isError = true
				} else {
					logs.Log("INFO", "Updated admin username to "+newUsername+" (ID: "+id+")")
					msg = "Administrator username updated successfully."
				}
			}
		case "pass":
			newPass := r.FormValue("password")
			if newPass == "" {
				msg = "Password cannot be empty."
				isError = true
			} else {
				hash, err := auth.HashPassword(newPass)
				if err != nil {
					logs.Log("ERROR", "Password hashing failed: "+err.Error())
					msg = "Failed to process password."
					isError = true
				} else {
					_, err = database.DB.Exec("UPDATE admin_users SET password_hash = ? WHERE id = ?", hash, id)
					if err != nil {
						logs.Log("ERROR", "Failed to update admin password: "+err.Error())
						msg = "Failed to update password."
						isError = true
					} else {
						logs.Log("INFO", "Password updated for administrator ID: "+id)
						msg = "Administrator password updated successfully."
					}
				}
			}
		}
	}

	rows, err := database.DB.Query("SELECT id, username FROM admin_users ORDER BY id ASC")
	if err != nil {
		logs.Log("ERROR", "Failed to fetch administrators: "+err.Error())
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	var admins []AdminUser
	for rows.Next() {
		var a AdminUser
		if err := rows.Scan(&a.ID, &a.Username); err != nil {
			continue
		}
		admins = append(admins, a)
	}

	renderTemplate(w, r, "admins", map[string]interface{}{
		"Admins":  admins,
		"Message": msg,
		"IsError": isError,
	})
}

type WDUser struct {
	ID        int
	Username  string
	Status    string
	CanUpload bool
	CanDelete bool
	CreatedAt string
}

func usersHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method == "POST" {
		action := r.FormValue("action")
		id := r.FormValue("id")

		if action == "add" {
			u, p := r.FormValue("username"), r.FormValue("password")

			upInt, delInt := 0, 0
			if r.FormValue("can_upload") == "on" {
				upInt = 1
			}
			if r.FormValue("can_delete") == "on" {
				delInt = 1
			}

			hash, err := auth.HashPassword(p)
			if err != nil {
				logs.Log("ERROR", "Password hashing failed: "+err.Error())
				http.Error(w, "Internal Server Error", http.StatusInternalServerError)
				return
			}
			_, err = database.DB.Exec("INSERT INTO webdav_users (username, password_hash, can_upload, can_delete) VALUES (?, ?, ?, ?)", u, hash, upInt, delInt)
			if err != nil {
				logs.Log("ERROR", "Failed to add WebDAV user: "+err.Error())
			} else {
				logs.Log("INFO", "Added new WebDAV user: "+u)
			}
		} else if action == "delete" {
			if _, err := database.DB.Exec("DELETE FROM webdav_users WHERE id = ?", id); err != nil {
				logs.Log("ERROR", "Failed to delete WebDAV user: "+err.Error())
			} else {
				logs.Log("INFO", "Deleted WebDAV user ID: "+id)
			}
		} else if action == "toggle" {
			database.DB.Exec("UPDATE webdav_users SET status = CASE WHEN status='Enabled' THEN 'Disabled' ELSE 'Enabled' END WHERE id = ?", id)
		} else if action == "toggle_upload" {
			database.DB.Exec("UPDATE webdav_users SET can_upload = CASE WHEN can_upload=1 THEN 0 ELSE 1 END WHERE id = ?", id)
		} else if action == "toggle_delete" {
			database.DB.Exec("UPDATE webdav_users SET can_delete = CASE WHEN can_delete=1 THEN 0 ELSE 1 END WHERE id = ?", id)
		} else if action == "pass" {
			hash, err := auth.HashPassword(r.FormValue("password"))
			if err != nil {
				logs.Log("ERROR", "Password hashing failed for update: "+err.Error())
			} else {
				if _, err := database.DB.Exec("UPDATE webdav_users SET password_hash = ? WHERE id = ?", hash, id); err != nil {
					logs.Log("ERROR", "Failed to update user password: "+err.Error())
				} else {
					logs.Log("INFO", "Password changed for user ID: "+id)
				}
			}
		}
		http.Redirect(w, r, "/users", http.StatusFound)
		return
	}

	rows, err := database.DB.Query("SELECT id, username, status, can_upload, can_delete, datetime(created_at, 'localtime') FROM webdav_users")
	if err != nil {
		logs.Log("ERROR", "Failed to fetch users from database: "+err.Error())
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	var users []WDUser
	for rows.Next() {
		var u WDUser
		var upInt, delInt int
		if err := rows.Scan(&u.ID, &u.Username, &u.Status, &upInt, &delInt, &u.CreatedAt); err != nil {
			continue
		}
		u.CanUpload = upInt == 1
		u.CanDelete = delInt == 1
		users = append(users, u)
	}

	renderTemplate(w, r, "users", map[string]interface{}{"Users": users})
}

func logsHandler(w http.ResponseWriter, r *http.Request) {
	if r.FormValue("action") == "clear" {
		logs.ClearLogs()
		http.Redirect(w, r, "/logs", http.StatusFound)
		return
	}
	if r.FormValue("action") == "download" {
		w.Header().Set("Content-Disposition", "attachment; filename=server_logs.txt")
		http.ServeFile(w, r, "logs/server.log")
		return
	}

	renderTemplate(w, r, "logs", map[string]interface{}{"Logs": logs.GetLogs(150)})
}