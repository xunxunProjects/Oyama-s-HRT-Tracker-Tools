package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestWriteOutputRefusesOverwriteUnlessForced(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "message.asc")
	if err := writeOutput(path, []byte("first"), false); err != nil {
		t.Fatalf("write: %v", err)
	}
	if err := writeOutput(path, []byte("second"), false); err == nil {
		t.Fatalf("overwrite was allowed")
	}
	if err := writeOutput(path, []byte("second"), true); err != nil {
		t.Fatalf("forced write: %v", err)
	}
	data, err := os.ReadFile(path)
	if err != nil || string(data) != "second" {
		t.Fatalf("content = %q, err = %v", data, err)
	}
	if runtime.GOOS != "windows" {
		info, err := os.Stat(path)
		if err != nil {
			t.Fatal(err)
		}
		if info.Mode().Perm() != 0o600 {
			t.Fatalf("mode = %o, want 0600", info.Mode().Perm())
		}
	}
}

func TestReadPassphraseFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "pass.txt")
	if err := os.WriteFile(path, []byte("correct horse battery staple\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	passphrase, err := readPassphraseFile(path, true)
	if err != nil {
		t.Fatalf("read passphrase: %v", err)
	}
	if string(passphrase) != "correct horse battery staple" {
		t.Fatalf("passphrase = %q", passphrase)
	}
	if runtime.GOOS != "windows" {
		if err := os.Chmod(path, 0o644); err != nil {
			t.Fatal(err)
		}
		if _, err := readPassphraseFile(path, true); err == nil {
			t.Fatalf("world-readable passphrase file was accepted")
		}
	}
	if err := os.WriteFile(path, []byte("short\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := readPassphraseFile(path, true); err == nil {
		t.Fatalf("short passphrase was accepted")
	}
}

func TestReadLimited(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "input")
	if err := os.WriteFile(path, []byte("hello"), 0o600); err != nil {
		t.Fatal(err)
	}
	data, err := readLimited(path, 5)
	if err != nil || string(data) != "hello" {
		t.Fatalf("read: %q, %v", data, err)
	}
	if _, err := readLimited(path, 4); err == nil {
		t.Fatalf("oversized file was accepted")
	}
}

func TestValidateServer(t *testing.T) {
	for _, value := range []string{
		"https://hrt.example.com",
		"https://hrt.example.com/base",
		"http://127.0.0.1:8787",
		"http://localhost:8787",
	} {
		if _, err := validateServer(value); err != nil {
			t.Fatalf("valid server %q rejected: %v", value, err)
		}
	}
	for _, value := range []string{
		"http://example.com",
		"ftp://example.com",
		"https://user:pass@example.com",
		"https://example.com/?x=1",
		"",
	} {
		if _, err := validateServer(value); err == nil {
			t.Fatalf("invalid server %q accepted", value)
		}
	}
}

func TestValidateProxy(t *testing.T) {
	if _, err := validateProxy("socks5h://127.0.0.1:9050"); err != nil {
		t.Fatalf("socks5h proxy rejected: %v", err)
	}
	alias, err := validateProxy("socks5://127.0.0.1:9050")
	if err != nil {
		t.Fatalf("socks5 alias rejected: %v", err)
	}
	if alias.Scheme != "socks5h" {
		t.Fatalf("socks5 alias scheme = %q, want socks5h", alias.Scheme)
	}
	defaulted, err := validateProxy("socks5://127.0.0.1")
	if err != nil {
		t.Fatalf("portless socks5 rejected: %v", err)
	}
	if defaulted.Port() != "1080" {
		t.Fatalf("portless socks5 port = %q, want 1080", defaulted.Port())
	}
	for _, value := range []string{
		"http://127.0.0.1:9050",
		"socks5h://",
		"",
	} {
		if _, err := validateProxy(value); err == nil && value != "" {
			t.Fatalf("invalid proxy %q accepted", value)
		}
	}
}

func TestReadTokenFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "token")
	token := strings.Repeat("a", 64)
	if err := os.WriteFile(path, []byte(token+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	got, err := readTokenFile(path)
	if err != nil || got != token {
		t.Fatalf("token = %q, %v", got, err)
	}
	if runtime.GOOS != "windows" {
		if err := os.Chmod(path, 0o644); err != nil {
			t.Fatal(err)
		}
		if _, err := readTokenFile(path); err == nil {
			t.Fatalf("world-readable token file was accepted")
		}
	}
}
