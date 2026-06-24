package config

import (
	"betterwebdav/internal/database"
	"database/sql"
)

type Settings struct {
	WebDAVPort string
	WebUIPort  string
	SharedPath string
}

var currentSettings Settings

func InitConfig() {
	err := database.DB.QueryRow("SELECT webdav_port, web_ui_port, shared_path FROM settings WHERE id = 1").
		Scan(&currentSettings.WebDAVPort, &currentSettings.WebUIPort, &currentSettings.SharedPath)
	
	if err == sql.ErrNoRows {
		currentSettings = Settings{WebDAVPort: "80", WebUIPort: "8080", SharedPath: "C:/"}
		database.DB.Exec("INSERT INTO settings (webdav_port, web_ui_port, shared_path) VALUES (?, ?, ?)",
			currentSettings.WebDAVPort, currentSettings.WebUIPort, currentSettings.SharedPath)
	}
}

func GetConfig() Settings {
	return currentSettings
}

func SaveConfig(s Settings) error {
	_, err := database.DB.Exec("UPDATE settings SET webdav_port=?, web_ui_port=?, shared_path=? WHERE id=1",
		s.WebDAVPort, s.WebUIPort, s.SharedPath)
	if err == nil {
		currentSettings = s
	}
	return err
}