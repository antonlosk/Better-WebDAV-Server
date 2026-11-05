package service

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/kardianos/service"
)

var svc service.Service

func getAppDir() string {
	if exePath, err := os.Executable(); err == nil {
		if resolved, err := filepath.EvalSymlinks(exePath); err == nil {
			return filepath.Dir(resolved)
		}
		return filepath.Dir(exePath)
	}
	return "."
}

func InitService(prg service.Interface) (service.Service, error) {
	dir := getAppDir()
	svcConfig := &service.Config{
		Name:             "BetterWebDAVServer",
		DisplayName:      "Better WebDAV Server",
		Description:      "High-performance WebDAV and Management Server",
		WorkingDirectory: dir,
		Option: service.KeyValue{
			"StartType": "automatic",
		},
	}

	var err error
	svc, err = service.New(prg, svcConfig)
	return svc, err
}

// IsInstalled checks if the service is currently installed and not in DELETE_PENDING state
func IsInstalled() bool {
	if runtime.GOOS == "windows" {
		out, err := exec.Command("sc.exe", "query", "BetterWebDAVServer").CombinedOutput()
		if err != nil {
			return false
		}
		str := string(out)
		// If service does not exist (1060) or is marked for deletion (DELETE_PENDING), treat as not installed
		if strings.Contains(str, "1060") || strings.Contains(str, "DELETE_PENDING") {
			return false
		}
		return true
	}

	if svc == nil {
		return false
	}
	status, err := svc.Status()
	return err == nil && status != service.StatusUnknown
}

// IsAutostartEnabled checks if the service startup mode is set to Automatic
func IsAutostartEnabled() bool {
	if !IsInstalled() {
		return false
	}
	if runtime.GOOS == "windows" {
		out, err := exec.Command("sc.exe", "qc", "BetterWebDAVServer").CombinedOutput()
		if err != nil {
			return false
		}
		return strings.Contains(string(out), "AUTO_START")
	}

	out, err := exec.Command("systemctl", "is-enabled", "BetterWebDAVServer").CombinedOutput()
	if err != nil {
		return false
	}
	return strings.TrimSpace(string(out)) == "enabled"
}

// SetAutostart configures whether the service starts automatically on boot without deleting it
func SetAutostart(enable bool) error {
	if !IsInstalled() {
		return fmt.Errorf("service is not installed")
	}

	if runtime.GOOS == "windows" {
		mode := "demand" // Manual start
		if enable {
			mode = "auto" // Automatic start on boot
		}
		cmd := exec.Command("sc.exe", "config", "BetterWebDAVServer", "start=", mode)
		if out, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("%v: %s", err, string(out))
		}
		return nil
	}

	action := "disable"
	if enable {
		action = "enable"
	}
	cmd := exec.Command("systemctl", action, "BetterWebDAVServer")
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("%v: %s", err, string(out))
	}
	return nil
}

// Install registers the system service and applies desired autostart setting
func Install(autostart bool) error {
	if svc == nil {
		return fmt.Errorf("service manager not initialized")
	}
	if err := service.Control(svc, "install"); err != nil {
		return err
	}
	_ = SetAutostart(autostart)
	return nil
}

// Uninstall forcefully stops and permanently removes the system service
func Uninstall() error {
	if runtime.GOOS == "windows" {
		// Stop service first
		_ = exec.Command("sc.exe", "stop", "BetterWebDAVServer").Run()
		time.Sleep(300 * time.Millisecond)

		// Delete service via sc.exe
		cmd := exec.Command("sc.exe", "delete", "BetterWebDAVServer")
		if out, err := cmd.CombinedOutput(); err != nil {
			if !strings.Contains(string(out), "1060") {
				return fmt.Errorf("%v: %s", err, string(out))
			}
		}
		return nil
	}

	if svc == nil {
		return fmt.Errorf("service manager not initialized")
	}
	_ = service.Control(svc, "stop")
	return service.Control(svc, "uninstall")
}