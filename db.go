package database

import (
	"database/sql"
	"log"
	"os"

	_ "modernc.org/sqlite"
)

var DB *sql.DB

func InitDB() {
	var err error
	os.MkdirAll("data", 0755)
	
	// Включаем режим WAL (позволяет одновременно читать и писать) 
	// и устанавливаем busy_timeout в 5000мс. Если база заблокирована, 
	// запрос подождет до 5 секунд вместо того, чтобы сразу упасть.
	DB, err = sql.Open("sqlite", "data/storage.db?_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)")
	if err != nil {
		log.Fatalf("Failed to open database: %v", err)
	}

	// ЖЕСТКОЕ ОГРАНИЧЕНИЕ: для SQLite в Go лучше всего ограничить количество 
	// открытых соединений до 1. Это заставит Go выстраивать все параллельные 
	// запросы (логи, авторизацию и т.д.) в очередь, полностью исключая ошибку SQLITE_BUSY.
	DB.SetMaxOpenConns(1)

	createTables := `
	CREATE TABLE IF NOT EXISTS admin_users (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		username TEXT UNIQUE,
		password_hash TEXT
	);
	CREATE TABLE IF NOT EXISTS webdav_users (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		username TEXT UNIQUE,
		password_hash TEXT,
		status TEXT DEFAULT 'Enabled',
		created_at DATETIME DEFAULT CURRENT_TIMESTAMP
	);
	CREATE TABLE IF NOT EXISTS settings (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		webdav_port TEXT,
		web_ui_port TEXT,
		shared_path TEXT
	);
	CREATE TABLE IF NOT EXISTS logs (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		level TEXT,
		message TEXT,
		created_at DATETIME DEFAULT CURRENT_TIMESTAMP
	);
	`
	_, err = DB.Exec(createTables)
	if err != nil {
		log.Fatalf("Failed to create tables: %v", err)
	}
}