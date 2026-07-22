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
// ГЛОБАЛЬНЫЙ БЛОК ЗАЩИТЫ ОТ БРУТФОРСА (ПЕРСИСТЕНТНЫЙ В БД)
// ==========================================

func IsLockedOut(ip string) bool {
	var lockoutUntil int64
	err := database.DB.QueryRow("SELECT lockout_until FROM login_tracking WHERE ip = ?", ip).Scan(&lockoutUntil)
	
	if err == sql.ErrNoRows {
		return false // Записей об этом IP нет
	}
	if err != nil {
		logs.Log("ERROR", "Failed to query lockout status: "+err.Error())
		return false
	}

	// Если текущее время меньше времени блокировки (Unix seconds)
	if time.Now().Unix() < lockoutUntil {
		return true
	}

	// Если время блокировки вышло — удаляем этот IP из "черного списка"
	database.DB.Exec("DELETE FROM login_tracking WHERE ip = ?", ip)
	return false
}

func RecordFailedLogin(ip string, service string) {
	// UPSERT: Если IP нет, добавляем с attempts=1. Если есть — увеличиваем счетчик.
	_, err := database.DB.Exec(`
		INSERT INTO login_tracking (ip, attempts, lockout_until) 
		VALUES (?, 1, 0)
		ON CONFLICT(ip) DO UPDATE SET attempts = attempts + 1
	`, ip)
	
	if err != nil {
		logs.Log("ERROR", "Failed to update login tracking: "+err.Error())
		return
	}

	// Проверяем, достигло ли количество попыток лимита
	var attempts int
	database.DB.QueryRow("SELECT attempts FROM login_tracking WHERE ip = ?", ip).Scan(&attempts)

	if attempts >= 5 {
		// Блокируем на 5 минут вперед
		lockoutTime := time.Now().Add(5 * time.Minute).Unix()
		database.DB.Exec("UPDATE login_tracking SET lockout_until = ? WHERE ip = ?", lockoutTime, ip)
		logs.Log("WARNING", "Brute-force protection: IP blocked for 5 minutes via "+service+" - "+ip)
	}
}

func ResetLoginAttempts(ip string) {
	// При успешном входе полностью стираем историю попыток для этого IP
	database.DB.Exec("DELETE FROM login_tracking WHERE ip = ?", ip)
}

func GetClientIP(r *http.Request) string {
	// 1. Ищем реальный IP, если сервер стоит за прокси (Nginx/Cloudflare)
	if ip := r.Header.Get("X-Real-IP"); ip != "" {
		return ip
	}
	if ip := r.Header.Get("X-Forwarded-For"); ip != "" {
		// Заголовок может содержать несколько IP через запятую
		parts := strings.Split(ip, ",")
		return strings.TrimSpace(parts[0])
	}

	// 2. Если прокси нет, используем стандартный адрес сокета
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

// ==========================================
// ЛОГИКА АВТОРИЗАЦИИ
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