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
	os.MkdirAll("data", 0755)
	os.MkdirAll("logs", 0755)

	database.InitDB()
	config.InitConfig()
	logs.InitLogger()

	// Запускаем серверы
	webdav.StartServer()
	handlers.StartWebServer() // Теперь это не блокирующий вызов

	// Авто-открытие браузера (только при первом запуске)
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

	// ==========================================
	// GRACEFUL SHUTDOWN (МЯГКАЯ ОСТАНОВКА)
	// ==========================================

	// Создаем канал, который слушает прерывания от ОС (Ctrl+C или закрытие службы)
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)

	// Программа будет "висеть" на этой строке и работать до тех пор, пока не придет сигнал
	<-quit

	fmt.Println("\n[SYSTEM] Received shutdown signal. Initiating graceful shutdown...")
	logs.Log("INFO", "Graceful shutdown initiated by system signal...")

	// 1. Останавливаем панель управления
	handlers.StopWebServer()
	
	// 2. Останавливаем WebDAV-сервер (даст текущим загрузкам до 5 сек на завершение)
	webdav.StopServer()
	
	// 3. Безопасно закрываем SQLite (сбрасываем кэш на диск)
	database.CloseDB()

	logs.Log("INFO", "Application exited cleanly")
	fmt.Println("[SYSTEM] Application exited cleanly. Goodbye!")
}