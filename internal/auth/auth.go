package auth

import (
	"betterwebdav/internal/database"
	"betterwebdav/internal/logs"
	"net"
	"net/http"
	"sync"
	"time"

	"golang.org/x/crypto/bcrypt"
)

var (
	loginAttempts = make(map[string]int)
	lockoutTimes  = make(map[string]time.Time)
	loginMu       sync.Mutex
)

func IsLockedOut(ip string) bool {
	loginMu.Lock()
	defer loginMu.Unlock()

	if lockoutTime, exists := lockoutTimes[ip]; exists {
		if time.Now().Before(lockoutTime) {
			return true
		}
		delete(lockoutTimes, ip)
		delete(loginAttempts, ip)
	}
	return false
}

func RecordFailedLogin(ip string, service string) {
	loginMu.Lock()
	defer loginMu.Unlock()

	loginAttempts[ip]++
	if loginAttempts[ip] >= 5 {
		lockoutTimes[ip] = time.Now().Add(5 * time.Minute)
		logs.Log("WARNING", "Brute-force protection: IP blocked for 5 minutes via "+service+" - "+ip)
	}
}

func ResetLoginAttempts(ip string) {
	loginMu.Lock()
	defer loginMu.Unlock()

	delete(loginAttempts, ip)
	delete(lockoutTimes, ip)
}

func GetClientIP(r *http.Request) string {
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

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

// НОВАЯ ФУНКЦИЯ: Получение прав пользователя
func GetPermissions(username string) (canUpload bool, canDelete bool) {
	var up, del int
	err := database.DB.QueryRow("SELECT can_upload, can_delete FROM webdav_users WHERE username = ?", username).Scan(&up, &del)
	if err != nil {
		return false, false
	}
	return up == 1, del == 1
}