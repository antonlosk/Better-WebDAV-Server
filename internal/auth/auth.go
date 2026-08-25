package auth

import (
	"betterwebdav/internal/database"
	"betterwebdav/internal/logs"
	"database/sql"
	"net"
	"net/http"
	"strings"
	"time"

	"golang.org/x/crypto/bcrypt"
)

// ==========================================
// SERVICE-ISOLATED BRUTE-FORCE PROTECTION
// ==========================================

func IsLockedOut(ip string, service string) bool {
	var lockoutUntil int64
	err := database.DB.QueryRow("SELECT lockout_until FROM login_tracking WHERE ip = ? AND service = ?", ip, service).Scan(&lockoutUntil)

	if err == sql.ErrNoRows {
		return false
	}
	if err != nil {
		logs.Log("ERROR", "Failed to query lockout status: "+err.Error())
		return false
	}

	if time.Now().Unix() < lockoutUntil {
		return true
	}

	database.DB.Exec("DELETE FROM login_tracking WHERE ip = ? AND service = ?", ip, service)
	return false
}

func RecordFailedLogin(ip string, service string) {
	_, err := database.DB.Exec(`
		INSERT INTO login_tracking (ip, service, attempts, lockout_until) 
		VALUES (?, ?, 1, 0)
		ON CONFLICT(ip, service) DO UPDATE SET attempts = attempts + 1
	`, ip, service)

	if err != nil {
		logs.Log("ERROR", "Failed to update login tracking: "+err.Error())
		return
	}

	var attempts int
	database.DB.QueryRow("SELECT attempts FROM login_tracking WHERE ip = ? AND service = ?", ip, service).Scan(&attempts)

	if attempts >= 5 {
		lockoutTime := time.Now().Add(5 * time.Minute).Unix()
		database.DB.Exec("UPDATE login_tracking SET lockout_until = ? WHERE ip = ? AND service = ?", lockoutTime, ip, service)
		logs.Log("WARNING", "Brute-force protection: IP blocked for 5 minutes on ["+service+"] - "+ip)
	}
}

func ResetLoginAttempts(ip string, service string) {
	database.DB.Exec("DELETE FROM login_tracking WHERE ip = ? AND service = ?", ip, service)
}

func GetClientIP(r *http.Request) string {
	if ip := r.Header.Get("X-Real-IP"); ip != "" {
		return ip
	}
	if ip := r.Header.Get("X-Forwarded-For"); ip != "" {
		parts := strings.Split(ip, ",")
		return strings.TrimSpace(parts[0])
	}

	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

// ==========================================
// AUTHENTICATION LOGIC
// ==========================================

func HashPassword(password string) (string, error) {
	bytes, err := bcrypt.GenerateFromPassword([]byte(password), 10)
	return string(bytes), err
}

func CheckPasswordHash(password, hash string) bool {
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	return err == nil
}

func AdminExists() bool {
	var count int
	database.DB.QueryRow("SELECT COUNT(*) FROM admin_users").Scan(&count)
	return count > 0
}

func CreateAdmin(username, password string) error {
	hash, err := HashPassword(password)
	if err != nil {
		return err
	}
	_, err = database.DB.Exec("INSERT INTO admin_users (username, password_hash) VALUES (?, ?)", username, hash)
	return err
}

func AuthenticateAdmin(username, password string) bool {
	var hash string
	err := database.DB.QueryRow("SELECT password_hash FROM admin_users WHERE username = ?", username).Scan(&hash)
	if err != nil {
		return false
	}
	return CheckPasswordHash(password, hash)
}

func AuthenticateWebDAV(username, password string) bool {
	var hash, status string
	err := database.DB.QueryRow("SELECT password_hash, status FROM webdav_users WHERE username = ?", username).Scan(&hash, &status)
	if err != nil || status != "Enabled" {
		return false
	}
	return CheckPasswordHash(password, hash)
}

func GetPermissions(username string) (canUpload bool, canDelete bool) {
	var up, del int
	err := database.DB.QueryRow("SELECT can_upload, can_delete FROM webdav_users WHERE username = ?", username).Scan(&up, &del)
	if err != nil {
		return false, false
	}
	return up == 1, del == 1
}