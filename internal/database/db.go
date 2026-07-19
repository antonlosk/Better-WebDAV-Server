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

	DB, err = sql.Open("sqlite", "data/storage.db?_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)")
	if err != nil {
		log.Fatalf("Failed to open database: %v", err)
	}

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
		can_upload INTEGER DEFAULT 1,
		can_delete INTEGER DEFAULT 1,
		created_at DATETIME DEFAULT CURRENT_TIMESTAMP
	);
	CREATE TABLE IF NOT EXISTS settings (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		webdav_port TEXT,
		web_ui_port TEXT,
		shared_path TEXT,
		log_retention TEXT DEFAULT '1_year'
	);
	`
	_, err = DB.Exec(createTables)
	if err != nil {
		log.Fatalf("Failed to create tables: %v", err)
	}

	DB.Exec("ALTER TABLE webdav_users ADD COLUMN can_upload INTEGER DEFAULT 1")
	DB.Exec("ALTER TABLE webdav_users ADD COLUMN can_delete INTEGER DEFAULT 1")
	
	// Изменили fallback-значение для старых баз на 1_year
	DB.Exec("ALTER TABLE settings ADD COLUMN log_retention TEXT DEFAULT '1_year'")
}

func CloseDB() {
	if DB != nil {
		DB.Close()
	}
}