package main

import (
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

const (
	maxPassphraseBytes = 4096
	minPassphraseBytes = 12
	maxTokenBytes      = 4096
)

func readLimited(path string, maxBytes int64) ([]byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return nil, err
	}
	if info.IsDir() {
		return nil, fmt.Errorf("%s is a directory", path)
	}
	if info.Size() > maxBytes {
		return nil, fmt.Errorf("%s is larger than %d bytes", path, maxBytes)
	}
	data, err := io.ReadAll(io.LimitReader(file, maxBytes+1))
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > maxBytes {
		return nil, fmt.Errorf("%s is larger than %d bytes", path, maxBytes)
	}
	return data, nil
}

func privateFileMode(path string) error {
	if runtime.GOOS == "windows" {
		return nil
	}
	info, err := os.Stat(path)
	if err != nil {
		return err
	}
	if info.Mode().Perm()&0o077 != 0 {
		return fmt.Errorf("%s must be mode 0600 (it is %04o)", path, info.Mode().Perm())
	}
	return nil
}

func readPassphraseFile(path string, required bool) ([]byte, error) {
	if path == "" {
		if required {
			return nil, errors.New("a passphrase file is required")
		}
		return nil, nil
	}
	if err := privateFileMode(path); err != nil {
		return nil, err
	}
	data, err := readLimited(path, maxPassphraseBytes)
	if err != nil {
		return nil, err
	}
	passphrase := []byte(strings.TrimRight(string(data), "\r\n"))
	if len(passphrase) < minPassphraseBytes {
		return nil, fmt.Errorf("passphrase must be at least %d bytes", minPassphraseBytes)
	}
	return passphrase, nil
}

func readTokenFile(path string) (string, error) {
	if path == "" {
		return "", errors.New("a token file is required for this command")
	}
	if err := privateFileMode(path); err != nil {
		return "", err
	}
	data, err := readLimited(path, maxTokenBytes)
	if err != nil {
		return "", err
	}
	token := strings.TrimSpace(string(data))
	if token == "" {
		return "", errors.New("token file is empty")
	}
	if strings.ContainsAny(token, "\r\n") {
		return "", errors.New("token file contains more than one line")
	}
	return token, nil
}

func writeOutput(path string, data []byte, force bool) error {
	if path == "" {
		return errors.New("output path is required")
	}
	dir := filepath.Dir(path)
	if !force {
		file, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
		if err != nil {
			if errors.Is(err, os.ErrExist) {
				return fmt.Errorf("%s already exists; use --force to replace it", path)
			}
			return err
		}
		if _, err := file.Write(data); err != nil {
			file.Close()
			_ = os.Remove(path)
			return err
		}
		if err := file.Sync(); err != nil {
			file.Close()
			_ = os.Remove(path)
			return err
		}
		return file.Close()
	}

	temp, err := os.CreateTemp(dir, ".hrt-f2f-*")
	if err != nil {
		return err
	}
	tempName := temp.Name()
	cleanup := func() {
		temp.Close()
		_ = os.Remove(tempName)
	}
	if err := temp.Chmod(0o600); err != nil {
		cleanup()
		return err
	}
	if _, err := temp.Write(data); err != nil {
		cleanup()
		return err
	}
	if err := temp.Sync(); err != nil {
		cleanup()
		return err
	}
	if err := temp.Close(); err != nil {
		_ = os.Remove(tempName)
		return err
	}
	if err := os.Rename(tempName, path); err != nil {
		if runtime.GOOS == "windows" {
			/* Windows does not replace an existing file atomically. */
			if removeErr := os.Remove(path); removeErr == nil || errors.Is(removeErr, os.ErrNotExist) {
				if retryErr := os.Rename(tempName, path); retryErr == nil {
					return nil
				}
			}
		}
		_ = os.Remove(tempName)
		return err
	}
	return nil
}

func validateServer(value string) (*url.URL, error) {
	if strings.TrimSpace(value) == "" {
		return nil, errors.New("server URL is required")
	}
	parsed, err := url.Parse(value)
	if err != nil {
		return nil, fmt.Errorf("invalid server URL: %w", err)
	}
	if parsed.Scheme != "https" && parsed.Scheme != "http" {
		return nil, errors.New("server URL must use https:// (http:// is allowed only for loopback)")
	}
	if parsed.User != nil {
		return nil, errors.New("server URL must not contain credentials")
	}
	if parsed.Hostname() == "" {
		return nil, errors.New("server URL has no host")
	}
	if parsed.RawQuery != "" || parsed.Fragment != "" {
		return nil, errors.New("server URL must not contain a query or fragment")
	}
	if parsed.Scheme == "http" {
		host := strings.Trim(parsed.Hostname(), "[]")
		ip := net.ParseIP(host)
		if host != "localhost" && (ip == nil || !ip.IsLoopback()) {
			return nil, errors.New("http:// is allowed only for a loopback server")
		}
	}
	parsed.Path = strings.TrimRight(parsed.Path, "/")
	return parsed, nil
}

func validateProxy(value string) (*url.URL, error) {
	if strings.TrimSpace(value) == "" {
		return nil, nil
	}
	parsed, err := url.Parse(value)
	if err != nil {
		return nil, fmt.Errorf("invalid proxy URL: %w", err)
	}
	if parsed.Scheme == "socks5" {
		parsed.Scheme = "socks5h"
	}
	if parsed.Scheme != "socks5h" {
		return nil, errors.New("proxy must use socks5:// or socks5h:// so the proxy resolves every name")
	}
	if parsed.User != nil {
		return nil, errors.New("proxy authentication is not supported")
	}
	if parsed.Hostname() == "" {
		return nil, errors.New("proxy URL needs a host")
	}
	if parsed.Port() == "" {
		parsed.Host = net.JoinHostPort(parsed.Hostname(), "1080")
	}
	return parsed, nil
}
