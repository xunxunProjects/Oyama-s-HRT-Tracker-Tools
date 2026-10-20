package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func writeTestFile(t *testing.T, path string, data []byte, mode os.FileMode) {
	t.Helper()
	if err := os.WriteFile(path, data, mode); err != nil {
		t.Fatal(err)
	}
}

func runCLI(t *testing.T, args ...string) (int, string, string) {
	t.Helper()
	var stdout, stderr bytes.Buffer
	code := run(args, &stdout, &stderr)
	return code, stdout.String(), stderr.String()
}

func TestCLIKeygenEncryptDecrypt(t *testing.T) {
	dir := t.TempDir()
	passFile := filepath.Join(dir, "pass")
	privateFile := filepath.Join(dir, "private.asc")
	publicFile := filepath.Join(dir, "public.asc")
	writeTestFile(t, passFile, []byte("correct horse battery staple\n"), 0o600)

	code, stdout, stderr := runCLI(t,
		"keygen",
		"--name", "Alice",
		"--email", "alice@example.invalid",
		"--passphrase-file", passFile,
		"--private-out", privateFile,
		"--public-out", publicFile,
	)
	if code != 0 {
		t.Fatalf("keygen failed: %s", stderr)
	}
	if !strings.Contains(stdout, "fingerprint:") {
		t.Fatalf("keygen output: %s", stdout)
	}
	if runtime.GOOS != "windows" {
		info, err := os.Stat(privateFile)
		if err != nil || info.Mode().Perm() != 0o600 {
			t.Fatalf("private mode: %v", err)
		}
	}
	if code, _, stderr := runCLI(t,
		"keygen",
		"--name", "Alice",
		"--email", "alice@example.invalid",
		"--passphrase-file", passFile,
		"--private-out", privateFile,
		"--public-out", publicFile,
	); code == 0 {
		t.Fatalf("keygen overwrote existing files: %s", stderr)
	}

	plaintextFile := filepath.Join(dir, "message.txt")
	ciphertextFile := filepath.Join(dir, "message.asc")
	decryptedFile := filepath.Join(dir, "message.out")
	writeTestFile(t, plaintextFile, []byte("hello face to face\n"), 0o600)

	if code, _, stderr := runCLI(t,
		"encrypt",
		"--recipient", publicFile,
		"--in", plaintextFile,
		"--out", ciphertextFile,
	); code != 0 {
		t.Fatalf("encrypt failed: %s", stderr)
	}
	if code, _, stderr := runCLI(t,
		"decrypt",
		"--private", privateFile,
		"--passphrase-file", passFile,
		"--in", ciphertextFile,
		"--out", decryptedFile,
	); code != 0 {
		t.Fatalf("decrypt failed: %s", stderr)
	}
	decrypted, err := os.ReadFile(decryptedFile)
	if err != nil || string(decrypted) != "hello face to face\n" {
		t.Fatalf("decrypted = %q, err = %v", decrypted, err)
	}

	listCode, listOut, listErr := runCLI(t, "keyring", "list", publicFile)
	if listCode != 0 || !strings.Contains(listOut, "Alice") {
		t.Fatalf("keyring list: %d %q %q", listCode, listOut, listErr)
	}
}

func TestKeyringPullRejectsInvalidServerKeyring(t *testing.T) {
	dir := t.TempDir()
	tokenFile := filepath.Join(dir, "token")
	output := filepath.Join(dir, "cohort.asc")
	writeTestFile(t, tokenFile, []byte(testToken+"\n"), 0o600)

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api/f2f/status" {
			_ = json.NewEncoder(w).Encode(map[string]any{
				"registered": true,
				"cohort": map[string]any{
					"id": "cohort-1", "state": "active", "memberCount": 1, "totalSeats": 1,
				},
				"seat": 1,
			})
			return
		}
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		_, _ = w.Write([]byte("this is not an armored keyring"))
	}))
	defer server.Close()

	code, _, stderr := runCLI(t, "keyring", "pull",
		"--server", server.URL,
		"--token-file", tokenFile,
		"--out", output,
	)
	if code == 0 || !strings.Contains(stderr, "invalid keyring") {
		t.Fatalf("invalid keyring was accepted: code=%d stderr=%q", code, stderr)
	}
	if _, err := os.Stat(output); !os.IsNotExist(err) {
		t.Fatalf("invalid keyring was written to disk (err=%v)", err)
	}
}

func TestCLINetworkCommands(t *testing.T) {
	dir := t.TempDir()
	passFile := filepath.Join(dir, "pass")
	privateFile := filepath.Join(dir, "private.asc")
	publicFile := filepath.Join(dir, "public.asc")
	tokenFile := filepath.Join(dir, "token")
	writtenKeyring := filepath.Join(dir, "cohort.asc")
	writeTestFile(t, passFile, []byte("correct horse battery staple\n"), 0o600)
	writeTestFile(t, tokenFile, []byte(testToken+"\n"), 0o600)
	if code, _, stderr := runCLI(t, "keygen",
		"--name", "Alice", "--email", "alice@example.invalid",
		"--passphrase-file", passFile,
		"--private-out", privateFile, "--public-out", publicFile,
	); code != 0 {
		t.Fatalf("keygen: %s", stderr)
	}
	publicBytes, err := os.ReadFile(publicFile)
	if err != nil {
		t.Fatal(err)
	}

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+testToken {
			w.WriteHeader(http.StatusUnauthorized)
			_, _ = w.Write([]byte(`{"code":"NOT_AUTHORIZED","message":"no"}`))
			return
		}
		switch r.URL.Path {
		case "/api/f2f/status":
			_ = json.NewEncoder(w).Encode(map[string]any{
				"registered": true,
				"cohort": map[string]any{
					"id": "cohort-1", "state": "active", "memberCount": 1, "totalSeats": 1,
				},
				"seat": 1,
			})
		case "/api/f2f/keys":
			_ = json.NewEncoder(w).Encode(map[string]any{
				"cohort_id": "cohort-1", "seat": 1, "key_id": "key-1",
				"fingerprint": strings.Repeat("a", 40), "status": "active",
			})
		case "/api/f2f/cohorts/cohort-1/keyring":
			w.Header().Set("Content-Type", "text/plain; charset=utf-8")
			_, _ = w.Write(publicBytes)
		default:
			w.WriteHeader(http.StatusNotFound)
		}
	}))
	defer server.Close()

	code, stdout, stderr := runCLI(t, "register",
		"--server", server.URL,
		"--token-file", tokenFile,
		"--public-key", publicFile,
		"--json",
	)
	if code != 0 || !strings.Contains(stdout, "cohort-1") {
		t.Fatalf("register: %d %q %q", code, stdout, stderr)
	}
	code, stdout, stderr = runCLI(t, "status",
		"--server", server.URL,
		"--token-file", tokenFile,
	)
	if code != 0 || !strings.Contains(stdout, "seat: 1") {
		t.Fatalf("status: %d %q %q", code, stdout, stderr)
	}
	code, stdout, stderr = runCLI(t, "keyring", "pull",
		"--server", server.URL,
		"--token-file", tokenFile,
		"--out", writtenKeyring,
	)
	if code != 0 {
		t.Fatalf("keyring pull: %s", stderr)
	}
	if !strings.Contains(stdout, "cohort cohort-1") {
		t.Fatalf("keyring pull output: %s", stdout)
	}
	info, err := os.Stat(writtenKeyring)
	if err != nil || (runtime.GOOS != "windows" && info.Mode().Perm() != 0o600) {
		t.Fatalf("keyring mode: %v", err)
	}
}
