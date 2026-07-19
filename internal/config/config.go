package config

import (
	"betterwebdav/internal/database"
	"database/sql"
)

type Settings struct {
	WebDAVPort   string
	WebUIPort    string
	SharedPath   string
	LogRetention string
}

var currentSettings Settings

func InitConfig() {
	err := database.DB.QueryRow("SELECT webdav_port, web_ui_port, shared_path, log_retention FROM settings WHERE id = 1").
		Scan(&currentSettings.WebDAVPort, &currentSettings.WebUIPort, &currentSettings.SharedPath, &currentSettings.LogRetention)

	if err == sql.ErrNoRows {
		// По умолчанию теперь 1 год
		currentSettings = Settings{WebDAVPort: "80", WebUIPort: "8080", SharedPath: "C:/", LogRetention: "1_year"}
		database.DB.Exec("INSERT INTO settings (webdav_port, web_ui_port, shared_path, log_retention) VALUES (?, ?, ?, ?)",
			currentSettings.WebDAVPort, currentSettings.WebUIPort, currentSettings.SharedPath, currentSettings.LogRetention)
	} else if err != nil {
		currentSettings = Settings{WebDAVPort: "80", WebUIPort: "8080", SharedPath: "C:/", LogRetention: "1_year"}
	}
}

func GetConfig() Settings {
	return currentSettings
}

func SaveConfig(s Settings) error {
	_, err := database.DB.Exec("UPDATE settings SET webdav_port=?, web_ui_port=?, shared_path=?, log_retention=? WHERE id=1",
		s.WebDAVPort, s.WebUIPort, s.SharedPath, s.LogRetention)
	if err == nil {
		currentSettings = s
	}
	return err
}