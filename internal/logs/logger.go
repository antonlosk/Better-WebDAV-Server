package logs

import (
	"bufio"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

type LogEntry struct {
	ID        int
	Level     string
	Message   string
	CreatedAt string
}

var (
	logFile *os.File
	logMu   sync.Mutex // Мьютекс для защиты от чтения во время обнуления файла
)

func InitLogger() {
	os.MkdirAll("logs", 0755)

	var err error
	logPath := filepath.Join("logs", "server.log")
	logFile, err = os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err != nil {
		log.Fatalf("Failed to open log file: %v", err)
	}

	multiWriter := io.MultiWriter(os.Stdout, logFile)
	log.SetOutput(multiWriter)
	log.SetFlags(log.Ldate | log.Ltime)

	Log("INFO", "Application started")
}

func CloseLogger() {
	logMu.Lock()
	defer logMu.Unlock()
	if logFile != nil {
		logFile.Close()
	}
}

// Запись ТОЛЬКО в файл и в консоль
func Log(level, message string) {
	logMu.Lock()
	defer logMu.Unlock()
	log.Printf("[%s] %s\n", level, message)
}

// Чтение логов НАПРЯМУЮ из текстового файла
func GetLogs(limit int) []LogEntry {
	logMu.Lock()
	defer logMu.Unlock()

	logPath := filepath.Join("logs", "server.log")
	file, err := os.Open(logPath)
	if err != nil {
		return nil
	}
	defer file.Close()

	// Читаем все строки
	scanner := bufio.NewScanner(file)
	var allLines []string
	for scanner.Scan() {
		allLines = append(allLines, scanner.Text())
	}

	var entries []LogEntry
	start := len(allLines) - limit
	if start < 0 {
		start = 0
	}

	idCounter := 1
	// Идем с конца в начало (новые логи сверху)
	for i := len(allLines) - 1; i >= start; i-- {
		line := allLines[i]
		if len(line) < 20 {
			continue
		}

		// Парсим стандартный формат Go: "2026/07/19 15:30:12 [INFO] Message"
		createdAt := line[0:19]
		rest := line[20:]

		level := "INFO"
		message := rest

		// Извлекаем уровень (INFO, ERROR, WARNING)
		if strings.HasPrefix(rest, "[") {
			endIdx := strings.Index(rest, "]")
			if endIdx != -1 {
				level = rest[1:endIdx]
				message = strings.TrimSpace(rest[endIdx+1:])
			}
		}

		entries = append(entries, LogEntry{
			ID:        idCounter,
			Level:     level,
			Message:   message,
			CreatedAt: createdAt,
		})
		idCounter++
	}
	return entries
}

// Очистка физического файла
func ClearLogs() {
	logMu.Lock()
	defer logMu.Unlock()

	if logFile != nil {
		logFile.Truncate(0) // Обрезаем файл до 0 байт
		logFile.Seek(0, 0)  // Сбрасываем курсор в начало
	}
	
	// Пишем первую запись в чистый файл
	log.Printf("[INFO] Logs cleared by admin via Web UI\n")
}