package database

import (
	"database/sql"
	"log"
	_ "modernc.org/sqlite"
	"os"
)

var DB *sql.DB

func InitDB() {
	var err error
	os.MkdirAll("data", 0755)
	DB, err = sql.Open("sqlite", "data/storage.db")
	if err != nil {
		log.Fatalf("Failed to open database: %v", err)
	}

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