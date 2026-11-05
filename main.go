package main

import (
	"betterwebdav/internal/auth"
	"betterwebdav/internal/config"
	"betterwebdav/internal/database"
	"betterwebdav/internal/handlers"
	"betterwebdav/internal/logs"
	"betterwebdav/internal/webdav"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"syscall"
	"time"
)

func openBrowser(url string) error {
	var cmd string
	var args []string

	switch runtime.GOOS {
	case "windows":
		cmd = "rundll32"
		args = []string{"url.dll,FileProtocolHandler", url}
	case "darwin": // macOS
		cmd = "open"
		args = []string{url}
	case "linux":
		cmd = "xdg-open"
		args = []string{url}
	default:
		return fmt.Errorf("unsupported platform")
	}

	return exec.Command(cmd, args...).Start()
}

func main() {
	_ = os.MkdirAll("data", 0755)
	_ = os.MkdirAll("logs", 0755)

	database.InitDB()
	config.InitConfig()
	logs.InitLogger()

	webdav.StartServer()
	handlers.StartWebServer()

	// Auto-open browser on first launch only
	go func() {
		time.Sleep(1 * time.Second)
		if !auth.AdminExists() {
			cfg := config.GetConfig()
			url := fmt.Sprintf("http://localhost:%s", cfg.WebUIPort)

			logs.Log("INFO", "Initial setup required. Opening browser...")
			if err := openBrowser(url); err != nil {
				logs.Log("WARNING", "Failed to auto-open browser: "+err.Error())
			}
		}
	}()

	// Graceful shutdown signal listener
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)

	<-quit

	fmt.Println("\n[SYSTEM] Received shutdown signal. Initiating shutdown...")
	logs.Log("INFO", "Shutdown initiated by system signal")

	// 1. Stop Web UI Server
	handlers.StopWebServer()

	// 2. Stop WebDAV Server
	webdav.StopServer()

	// 3. Close SQLite Database Connection
	database.CloseDB()

	// 4. Close Server Log File
	logs.CloseLogger()

	fmt.Println("[SYSTEM] Application exited cleanly. Goodbye!")
}
