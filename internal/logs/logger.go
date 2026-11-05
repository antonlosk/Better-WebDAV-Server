package logs

import (
	"betterwebdav/internal/config"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

type LogEntry struct {
	ID        int
	Level     string
	Message   string
	CreatedAt string
}

// getAppDir returns absolute directory path where the binary resides
func getAppDir() string {
	if exePath, err := os.Executable(); err == nil {
		if resolved, err := filepath.EvalSymlinks(exePath); err == nil {
			return filepath.Dir(resolved)
		}
		return filepath.Dir(exePath)
	}
	return "."
}

// safeLogWriter guarantees writes to log file even if stdout is an invalid handle in service mode
type safeLogWriter struct {
	fileWriter io.Writer
}

func (w *safeLogWriter) Write(p []byte) (n int, err error) {
	// Always write to the file rotator first
	n, err = w.fileWriter.Write(p)

	// Best-effort console write: ignore invalid handle errors in background daemon/service mode
	if os.Stdout != nil {
		_, _ = os.Stdout.Write(p)
	}

	return n, err
}

// RotatingFileWriter manages size-based file rotation without whole-file rewrites
type RotatingFileWriter struct {
	mu       sync.Mutex
	filename string
	maxBytes int64
	size     int64
	file     *os.File
}

func parseMaxBytes(sizeStr, unitStr string) int64 {
	unit := strings.ToUpper(strings.TrimSpace(unitStr))
	if unit == "UNLIMITED" {
		return 0 // 0 means unlimited size / auto-rotation disabled
	}

	size, err := strconv.ParseInt(sizeStr, 10, 64)
	if err != nil || size <= 0 {
		return 64 * 1024 * 1024
	}

	switch unit {
	case "KB":
		return size * 1024
	case "GB":
		return size * 1024 * 1024 * 1024
	case "MB":
		fallthrough
	default:
		return size * 1024 * 1024
	}
}

func NewRotatingFileWriter(filename string, maxBytes int64) (*RotatingFileWriter, error) {
	var size int64
	if fi, err := os.Stat(filename); err == nil {
		size = fi.Size()
	}

	f, err := os.OpenFile(filename, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err != nil {
		return nil, err
	}

	rw := &RotatingFileWriter{
		filename: filename,
		maxBytes: maxBytes,
		size:     size,
		file:     f,
	}

	if maxBytes > 0 && size >= maxBytes {
		_ = rw.rotate()
	}

	return rw, nil
}

func (w *RotatingFileWriter) Write(p []byte) (n int, err error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	writeLen := int64(len(p))
	if w.maxBytes > 0 && w.size > 0 && w.size+writeLen > w.maxBytes {
		_ = w.rotate()
	}

	if w.file == nil {
		f, err := os.OpenFile(w.filename, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
		if err != nil {
			return 0, err
		}
		w.file = f
	}

	n, err = w.file.Write(p)
	w.size += int64(n)
	return n, err
}

func (w *RotatingFileWriter) rotate() error {
	if w.file != nil {
		_ = w.file.Close()
		w.file = nil
	}

	timestamp := time.Now().Format("2006-01-02T15-04-05")
	dir := filepath.Dir(w.filename)
	ext := filepath.Ext(w.filename)
	base := strings.TrimSuffix(filepath.Base(w.filename), ext)
	rotatedName := filepath.Join(dir, fmt.Sprintf("%s-%s%s", base, timestamp, ext))

	_ = os.Rename(w.filename, rotatedName)

	f, err := os.OpenFile(w.filename, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err != nil {
		return err
	}
	w.file = f
	w.size = 0
	return nil
}

func (w *RotatingFileWriter) SetLimit(maxBytes int64) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.maxBytes = maxBytes
	if w.maxBytes > 0 && w.size >= w.maxBytes && w.size > 0 {
		_ = w.rotate()
	}
}

func (w *RotatingFileWriter) Truncate() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.file != nil {
		_ = w.file.Close()
		w.file = nil
	}

	if err := os.WriteFile(w.filename, []byte(""), 0666); err != nil {
		return err
	}

	f, err := os.OpenFile(w.filename, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err != nil {
		return err
	}
	w.file = f
	w.size = 0
	return nil
}

func (w *RotatingFileWriter) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.file != nil {
		return w.file.Close()
	}
	return nil
}

var (
	rotator *RotatingFileWriter
	logMu   sync.Mutex
)

func InitLogger() {
	appDir := getAppDir()
	_ = os.MkdirAll(filepath.Join(appDir, "logs"), 0755)

	cfg := config.GetConfig()
	maxBytes := parseMaxBytes(cfg.LogMaxSize, cfg.LogMaxUnit)

	logPath := filepath.Join(appDir, "logs", "server.log")
	var err error
	rotator, err = NewRotatingFileWriter(logPath, maxBytes)
	if err != nil {
		log.Fatalf("Failed to initialize rotating logger: %v", err)
	}

	// Use safeLogWriter instead of io.MultiWriter to survive disconnected stdout in service mode
	safeWriter := &safeLogWriter{fileWriter: rotator}
	log.SetOutput(safeWriter)
	log.SetFlags(log.Ldate | log.Ltime)

	Log("INFO", "Application started")
}

func UpdateRotationLimit(sizeStr, unitStr string) {
	if rotator != nil {
		maxBytes := parseMaxBytes(sizeStr, unitStr)
		rotator.SetLimit(maxBytes)
	}
}

func CloseLogger() {
	if rotator != nil {
		_ = rotator.Close()
	}
}

func Log(level, message string) {
	log.Printf("[%s] %s\n", level, message)
}

func getTailLines(file *os.File, limit int) []string {
	var lines []string
	stat, err := file.Stat()
	if err != nil {
		return nil
	}

	filesize := stat.Size()
	if filesize == 0 {
		return nil
	}

	var cursor int64 = filesize
	var bufSize int64 = 32768
	var leftover string

	for cursor > 0 && len(lines) < limit {
		if cursor < bufSize {
			bufSize = cursor
		}
		cursor -= bufSize

		buf := make([]byte, bufSize)
		_, _ = file.ReadAt(buf, cursor)

		chunk := string(buf) + leftover
		parts := strings.Split(chunk, "\n")

		leftover = parts[0]

		for i := len(parts) - 1; i > 0; i-- {
			line := strings.TrimSpace(parts[i])
			if line != "" {
				lines = append(lines, line)
				if len(lines) == limit {
					break
				}
			}
		}
	}

	if cursor == 0 && leftover != "" && len(lines) < limit {
		line := strings.TrimSpace(leftover)
		if line != "" {
			lines = append(lines, line)
		}
	}

	return lines
}

func GetLogs(limit int) []LogEntry {
	logMu.Lock()
	defer logMu.Unlock()

	logPath := filepath.Join(getAppDir(), "logs", "server.log")
	file, err := os.Open(logPath)
	if err != nil {
		return nil
	}
	defer file.Close()

	lines := getTailLines(file, limit)

	var entries []LogEntry
	idCounter := 1

	for _, line := range lines {
		if len(line) < 20 {
			continue
		}

		createdAt := line[0:19]
		rest := line[20:]
		level := "INFO"
		message := rest

		if strings.HasPrefix(rest, "[") {
			endIdx := strings.Index(rest, "]")
			if endIdx != -1 {
				level = rest[1:endIdx]
				message = strings.TrimSpace(rest[endIdx+1:])
			}
		}

		entries = append(entries, LogEntry{
			ID:        idCounter,
			Level:     level,
			Message:   message,
			CreatedAt: createdAt,
		})
		idCounter++
	}
	return entries
}

func ClearLogs() {
	if rotator != nil {
		_ = rotator.Truncate()
	}
}
