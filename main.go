package main

import (
	"betterwebdav/internal/auth"
	"betterwebdav/internal/config"
	"betterwebdav/internal/database"
	"betterwebdav/internal/handlers"
	"betterwebdav/internal/logs"
	"betterwebdav/internal/service"
	"betterwebdav/internal/webdav"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"time"

	kardianos "github.com/kardianos/service"
)

func init() {
	// Ensure working directory is set to the executable location (critical for Windows Service)
	if exePath, err := os.Executable(); err == nil {
		_ = os.Chdir(filepath.Dir(exePath))
	}
}

func openBrowser(url string) error {
	var cmd string
	var args []string

	switch runtime.GOOS {
	case "windows":
		cmd = "rundll32"
		args = []string{"url.dll,FileProtocolHandler", url}
	case "darwin":
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

type program struct{}

func (p *program) Start(s kardianos.Service) error {
	go p.run()
	return nil
}

func (p *program) run() {
	_ = os.MkdirAll("data", 0755)
	_ = os.MkdirAll("logs", 0755)

	database.InitDB()
	config.InitConfig()
	logs.InitLogger()

	webdav.StartServer()
	handlers.StartWebServer()

	// Auto-open browser only on first setup and when running interactively (not as background service)
	if kardianos.Interactive() {
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
	}
}

func (p *program) Stop(s kardianos.Service) error {
	logs.Log("INFO", "Shutdown initiated by system signal")

	handlers.StopWebServer()
	webdav.StopServer()
	database.CloseDB()
	logs.CloseLogger()

	return nil
}

func main() {
	prg := &program{}
	s, err := service.InitService(prg)
	if err != nil {
		fmt.Printf("Service initialization failed: %v\n", err)
		return
	}

	if err = s.Run(); err != nil {
		fmt.Printf("Service execution error: %v\n", err)
	}
}
