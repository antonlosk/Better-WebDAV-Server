package logs

import (
	"betterwebdav/internal/database"
	"log"
)

type LogEntry struct {
	ID        int
	Level     string
	Message   string
	CreatedAt string
}

func InitLogger() {
	Log("INFO", "Application started")
}

func Log(level, message string) {
	log.Printf("[%s] %s\n", level, message)
	_, err := database.DB.Exec("INSERT INTO logs (level, message) VALUES (?, ?)", level, message)
	if err != nil {
		log.Printf("Failed to write log to DB: %v", err)
	}
}

func GetLogs(limit int) []LogEntry {
	rows, err := database.DB.Query("SELECT id, level, message, datetime(created_at, 'localtime') FROM logs ORDER BY id DESC LIMIT ?", limit)
	if err != nil {
		return nil
	}
	defer rows.Close()

	var entries []LogEntry
	for rows.Next() {
		var l LogEntry
		rows.Scan(&l.ID, &l.Level, &l.Message, &l.CreatedAt)
		entries = append(entries, l)
	}
	return entries
}

func ClearLogs() {
	database.DB.Exec("DELETE FROM logs")
	Log("INFO", "Logs cleared by admin")
}