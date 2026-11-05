package service

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

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

func IsInstalled() bool {
	if svc == nil {
		return false
	}
	if runtime.GOOS == "windows" {
		out, err := exec.Command("sc.exe", "query", "BetterWebDAVServer").CombinedOutput()
		if err != nil {
			return false
		}
		// 1060 indicates the specified service does not exist as an installed service
		return !strings.Contains(string(out), "1060")
	}
	status, err := svc.Status()
	return err == nil && status != service.StatusUnknown
}

// IsAutostartEnabled checks if the service is configured to start automatically on system boot
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

// SetAutostart configures the service startup type (Auto vs Manual)
func SetAutostart(enable bool) error {
	if !IsInstalled() {
		return fmt.Errorf("service is not installed")
	}
	if runtime.GOOS == "windows" {
		mode := "demand"
		if enable {
			mode = "auto"
		}
		// Note: space after 'start=' is mandatory in sc.exe syntax
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

func Uninstall() error {
	if svc == nil {
		return fmt.Errorf("service manager not initialized")
	}
	_ = service.Control(svc, "stop")
	return service.Control(svc, "uninstall")
}
