package main

import (
	"betterwebdav/internal/auth" // Добавили импорт пакета auth
	"betterwebdav/internal/config"
	"betterwebdav/internal/database"
	"betterwebdav/internal/handlers"
	"betterwebdav/internal/logs"
	"betterwebdav/internal/webdav"
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"time"
)

// openBrowser открывает ссылку в браузере по умолчанию в зависимости от ОС
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
	// Создаем обязательные директории
	os.MkdirAll("data", 0755)
	os.MkdirAll("logs", 0755)

	// Инициализируем базу данных, конфиг и логи
	database.InitDB()
	config.InitConfig()
	logs.InitLogger()

	// Автоматически запускаем WebDAV сервер
	webdav.StartServer()

	// Авто-открытие браузера
	go func() {
		time.Sleep(1 * time.Second) // Ждем запуска веб-сервера админки
		
		// ПРОВЕРКА: Открываем браузер ТОЛЬКО если администратор еще не создан
		if !auth.AdminExists() {
			cfg := config.GetConfig()
			url := fmt.Sprintf("http://localhost:%s", cfg.WebUIPort)
			
			logs.Log("INFO", "Initial setup required. Opening browser...")
			if err := openBrowser(url); err != nil {
				logs.Log("WARNING", "Failed to auto-open browser: "+err.Error())
			}
		}
	}()

	// Запускаем веб-интерфейс панели управления (Блокирующий вызов)
	handlers.StartWebServer()
}