package service

import (
	"fmt"
	"os"
	"path/filepath"

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

// InitService configures the system daemon/service descriptor with explicit WorkingDirectory
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

// IsInstalled checks whether the service is registered in the OS service manager
func IsInstalled() bool {
	if svc == nil {
		return false
	}
	status, err := svc.Status()
	if err != nil {
		return false
	}
	return status != service.StatusUnknown
}

// Install registers the server as an autostart system service
func Install() error {
	if svc == nil {
		return fmt.Errorf("service manager not initialized")
	}
	return service.Control(svc, "install")
}

// Uninstall stops and removes the system service from the OS
func Uninstall() error {
	if svc == nil {
		return fmt.Errorf("service manager not initialized")
	}
	_ = service.Control(svc, "stop")
	return service.Control(svc, "uninstall")
}
