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
	CREATE TABLE IF NOT EXISTS login_tracking (
		ip TEXT,
		service TEXT,
		attempts INTEGER DEFAULT 0,
		lockout_until INTEGER DEFAULT 0,
		PRIMARY KEY (ip, service)
	);
	`
	_, err = DB.Exec(createTables)
	if err != nil {
		log.Fatalf("Failed to create tables: %v", err)
	}

	DB.Exec("ALTER TABLE webdav_users ADD COLUMN can_upload INTEGER DEFAULT 1")
	DB.Exec("ALTER TABLE webdav_users ADD COLUMN can_delete INTEGER DEFAULT 1")
	DB.Exec("ALTER TABLE settings ADD COLUMN log_retention TEXT DEFAULT '1_year'")

	// Migrate login_tracking schema if updating from single-column primary key
	var hasServiceCol int
	DB.QueryRow("SELECT COUNT(*) FROM pragma_table_info('login_tracking') WHERE name = 'service'").Scan(&hasServiceCol)
	if hasServiceCol == 0 {
		DB.Exec("DROP TABLE IF EXISTS login_tracking")
		DB.Exec(`
			CREATE TABLE IF NOT EXISTS login_tracking (
				ip TEXT,
				service TEXT,
				attempts INTEGER DEFAULT 0,
				lockout_until INTEGER DEFAULT 0,
				PRIMARY KEY (ip, service)
			)
		`)
	}
}

func CloseDB() {
	if DB != nil {
		DB.Close()
	}
}