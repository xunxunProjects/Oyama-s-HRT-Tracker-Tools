package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

const testToken = "0123456789abcdef0123456789abcdef"

func TestAPIClientStatusRegisterAndKeyring(t *testing.T) {
	var registered bool
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+testToken {
			w.WriteHeader(http.StatusUnauthorized)
			_, _ = w.Write([]byte(`{"code":"NOT_AUTHORIZED","message":"no"}`))
			return
		}
		switch {
		case r.URL.Path == "/base/api/f2f/status":
			_ = json.NewEncoder(w).Encode(map[string]any{
				"registered": true,
				"cohort": map[string]any{
					"id": "cohort-1", "state": "active", "memberCount": 1, "totalSeats": 1,
				},
				"seat": 1,
				"key": map[string]any{
					"id": "key-1", "fingerprint": strings.Repeat("a", 40),
					"publicKey": "key", "status": "active", "createdAt": 1760000000,
				},
				"escrow": map[string]any{
					"publicKey": "escrow", "fingerprint": strings.Repeat("b", 40),
				},
			})
		case r.URL.Path == "/base/api/f2f/keys" && r.Method == http.MethodPost:
			registered = true
			var body map[string]string
			_ = json.NewDecoder(r.Body).Decode(&body)
			if body["public_key"] == "" || body["fingerprint"] == "" {
				w.WriteHeader(http.StatusBadRequest)
				_, _ = w.Write([]byte(`{"code":"INVALID_KEY","message":"bad"}`))
				return
			}
			_ = json.NewEncoder(w).Encode(map[string]any{
				"cohort_id": "cohort-1", "seat": 1, "key_id": "key-1",
				"fingerprint": body["fingerprint"], "status": "active",
			})
		case r.URL.Path == "/base/api/f2f/cohorts/cohort-1/keyring":
			w.Header().Set("Content-Type", "text/plain; charset=utf-8")
			_, _ = w.Write([]byte("-----BEGIN PGP PUBLIC KEY BLOCK-----\n...\n"))
		default:
			w.WriteHeader(http.StatusNotFound)
			_, _ = w.Write([]byte(`{"code":"NOT_FOUND","message":"not found"}`))
		}
	}))
	defer server.Close()

	client, err := newAPIClient(server.URL+"/base", testToken, "", 5*time.Second)
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	status, err := client.Status()
	if err != nil {
		t.Fatalf("status: %v", err)
	}
	if !status.Registered || status.Seat != 1 || status.Cohort == nil ||
		status.Cohort.ID != "cohort-1" || status.Key == nil ||
		status.Escrow == nil || status.Escrow.Fingerprint != strings.Repeat("b", 40) {
		t.Fatalf("unexpected status: %+v", status)
	}
	result, err := client.Register("public", strings.Repeat("a", 40))
	if err != nil {
		t.Fatalf("register: %v", err)
	}
	if !registered || result.Seat != 1 || result.KeyID != "key-1" {
		t.Fatalf("unexpected register result: %+v", result)
	}
	keyring, err := client.Keyring("cohort-1")
	if err != nil {
		t.Fatalf("keyring: %v", err)
	}
	if !strings.Contains(keyring, "BEGIN PGP PUBLIC KEY BLOCK") {
		t.Fatalf("unexpected keyring: %q", keyring)
	}
}

func TestAPIClientSurfacesErrors(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
		_, _ = w.Write([]byte(`{"code":"NOT_A_MEMBER","message":"not a member"}`))
	}))
	defer server.Close()
	client, err := newAPIClient(server.URL, testToken, "", 5*time.Second)
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	_, err = client.Keyring("cohort-1")
	if err == nil {
		t.Fatalf("expected an error")
	}
	apiErr, ok := err.(*apiError)
	if !ok || apiErr.Code != "NOT_A_MEMBER" || apiErr.Status != http.StatusForbidden {
		t.Fatalf("unexpected error: %#v", err)
	}
}
