package logs

import (
	"betterwebdav/internal/config"
	"bufio"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type LogEntry struct {
	ID        int
	Level     string
	Message   string
	CreatedAt string
}

var (
	logFile *os.File
	logMu   sync.Mutex
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

	go func() {
		CleanOldLogs()
		for {
			time.Sleep(1 * time.Hour)
			CleanOldLogs()
		}
	}()
}

func CloseLogger() {
	logMu.Lock()
	defer logMu.Unlock()
	if logFile != nil {
		logFile.Close()
	}
}

func Log(level, message string) {
	logMu.Lock()
	defer logMu.Unlock()
	log.Printf("[%s] %s\n", level, message)
}

func GetLogs(limit int) []LogEntry {
	logMu.Lock()
	defer logMu.Unlock()

	logPath := filepath.Join("logs", "server.log")
	file, err := os.Open(logPath)
	if err != nil {
		return nil
	}
	defer file.Close()

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
	for i := len(allLines) - 1; i >= start; i-- {
		line := allLines[i]
		if len(line) < 20 {
			continue
		}

		createdAt := line[0:19]
		rest := line[20:]
		level := "INFO"
		message := rest

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

// ОЧИСТКА БЕЗ ОСТАВЛЕНИЯ ЗАПИСЕЙ
func ClearLogs() {
	logMu.Lock()
	defer logMu.Unlock()

	logPath := filepath.Join("logs", "server.log")

	// 1. Закрываем текущий дескриптор
	if logFile != nil {
		logFile.Close()
	}

	// 2. Жестко перезаписываем файл пустотой (0 байт)
	os.WriteFile(logPath, []byte(""), 0666)

	// 3. Открываем чистый файл заново для работы логгера
	var err error
	logFile, err = os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err == nil {
		log.SetOutput(io.MultiWriter(os.Stdout, logFile))
	}
	
	// Мы убрали log.Printf("Logs cleared..."), теперь файл остается абсолютно пустым.
}

func CleanOldLogs() {
	cfg := config.GetConfig()
	
	if cfg.LogRetention == "never" {
		return
	}

	var retention time.Duration

	switch cfg.LogRetention {
	case "1_hour": retention = 1 * time.Hour
	case "24_hours": retention = 24 * time.Hour
	case "7_days": retention = 7 * 24 * time.Hour
	case "1_month": retention = 30 * 24 * time.Hour
	case "3_months": retention = 90 * 24 * time.Hour
	case "6_months": retention = 180 * 24 * time.Hour
	case "9_months": retention = 270 * 24 * time.Hour
	case "1_year": retention = 365 * 24 * time.Hour
	default: retention = 365 * 24 * time.Hour
	}

	cutoff := time.Now().Add(-retention)
	layout := "2006/01/02 15:04:05"

	logMu.Lock()
	defer logMu.Unlock()

	logPath := filepath.Join("logs", "server.log")
	tmpPath := filepath.Join("logs", "server.tmp")

	file, err := os.Open(logPath)
	if err != nil {
		return
	}

	var keptLines []string
	scanner := bufio.NewScanner(file)
	
	for scanner.Scan() {
		line := scanner.Text()
		if len(line) >= 19 {
			if t, err := time.Parse(layout, line[:19]); err == nil {
				if t.Before(cutoff) {
					continue 
				}
			}
		}
		keptLines = append(keptLines, line)
	}
	file.Close()

	if logFile != nil {
		logFile.Close()
	}

	tmp, err := os.Create(tmpPath)
	if err == nil {
		for _, l := range keptLines {
			tmp.WriteString(l + "\n")
		}
		tmp.Close()
		os.Rename(tmpPath, logPath) 
	}

	logFile, _ = os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	log.SetOutput(io.MultiWriter(os.Stdout, logFile))
}