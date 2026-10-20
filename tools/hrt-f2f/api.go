package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"

	"golang.org/x/net/proxy"
)

const (
	maxStatusResponse  = 64 * 1024
	maxKeyringResponse = 4 << 20
)

type apiError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
	Status  int    `json:"-"`
}

func (e *apiError) Error() string {
	if e.Code == "" {
		return e.Message
	}
	return e.Code + ": " + e.Message
}

type f2fCohort struct {
	ID          string `json:"id"`
	State       string `json:"state"`
	MemberCount int    `json:"memberCount"`
	TotalSeats  int    `json:"totalSeats"`
}

type f2fKey struct {
	ID          string `json:"id"`
	Fingerprint string `json:"fingerprint"`
	PublicKey   string `json:"publicKey"`
	Status      string `json:"status"`
	CreatedAt   int64  `json:"createdAt"`
}

type f2fEscrow struct {
	PublicKey   string `json:"publicKey"`
	Fingerprint string `json:"fingerprint"`
}

type f2fStatus struct {
	Registered bool       `json:"registered"`
	Cohort     *f2fCohort `json:"cohort"`
	Seat       int        `json:"seat"`
	Key        *f2fKey    `json:"key"`
	Escrow     *f2fEscrow `json:"escrow"`
}

type registerResult struct {
	CohortID    string `json:"cohort_id"`
	Seat        int    `json:"seat"`
	KeyID       string `json:"key_id"`
	Fingerprint string `json:"fingerprint"`
	Status      string `json:"status"`
	Idempotent  bool   `json:"idempotent"`
}

type apiClient struct {
	base  string
	token string
	http  *http.Client
}

func newAPIClient(server, token, proxyURL string, timeout time.Duration) (*apiClient, error) {
	parsed, err := validateServer(server)
	if err != nil {
		return nil, err
	}
	if len(token) < 16 || len(token) > maxTokenBytes || strings.ContainsAny(token, "\r\n") {
		return nil, errors.New("invalid token")
	}
	if timeout <= 0 {
		timeout = 30 * time.Second
	}

	transport := &http.Transport{
		Proxy:                 nil, /* environment proxies and no_proxy are never used */
		TLSHandshakeTimeout:   timeout,
		ResponseHeaderTimeout: timeout,
	}
	if proxyURL != "" {
		parsedProxy, err := validateProxy(proxyURL)
		if err != nil {
			return nil, err
		}
		dialer, err := proxy.SOCKS5("tcp", parsedProxy.Host, nil, proxy.Direct)
		if err != nil {
			return nil, fmt.Errorf("SOCKS5 proxy: %w", err)
		}
		if contextDialer, ok := dialer.(proxy.ContextDialer); ok {
			transport.DialContext = contextDialer.DialContext
		} else {
			transport.DialContext = func(_ context.Context, network, address string) (net.Conn, error) {
				return dialer.Dial(network, address)
			}
		}
	}

	return &apiClient{
		base:  strings.TrimRight(parsed.String(), "/"),
		token: token,
		http: &http.Client{
			Transport: transport,
			Timeout:   timeout,
		},
	}, nil
}

func (c *apiClient) do(method, path string, body []byte, maxResponse int64) ([]byte, string, error) {
	var reader io.Reader
	if body != nil {
		reader = bytes.NewReader(body)
	}
	request, err := http.NewRequest(method, c.base+path, reader)
	if err != nil {
		return nil, "", err
	}
	request.Header.Set("Authorization", "Bearer "+c.token)
	request.Header.Set("Accept", "application/json")
	if body != nil {
		request.Header.Set("Content-Type", "application/json")
	}
	response, err := c.http.Do(request)
	if err != nil {
		return nil, "", fmt.Errorf("request failed: %w", err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, maxResponse+1))
	if err != nil {
		return nil, "", err
	}
	if int64(len(data)) > maxResponse {
		return nil, "", fmt.Errorf("response exceeds %d bytes", maxResponse)
	}
	contentType := response.Header.Get("Content-Type")
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		apiErr := &apiError{Status: response.StatusCode, Message: response.Status}
		var parsed apiError
		if json.Unmarshal(data, &parsed) == nil {
			if parsed.Code != "" {
				apiErr.Code = parsed.Code
			}
			if parsed.Message != "" {
				apiErr.Message = parsed.Message
			}
		}
		return nil, contentType, apiErr
	}
	return data, contentType, nil
}

func (c *apiClient) Status() (*f2fStatus, error) {
	data, _, err := c.do(http.MethodGet, "/api/f2f/status", nil, maxStatusResponse)
	if err != nil {
		return nil, err
	}
	var status f2fStatus
	if err := json.Unmarshal(data, &status); err != nil {
		return nil, fmt.Errorf("invalid status response: %w", err)
	}
	return &status, nil
}

func (c *apiClient) Register(publicKey, fingerprint string) (*registerResult, error) {
	body, err := json.Marshal(map[string]string{
		"public_key":  publicKey,
		"fingerprint": fingerprint,
	})
	if err != nil {
		return nil, err
	}
	data, _, err := c.do(http.MethodPost, "/api/f2f/keys", body, maxStatusResponse)
	if err != nil {
		return nil, err
	}
	var result registerResult
	if err := json.Unmarshal(data, &result); err != nil {
		return nil, fmt.Errorf("invalid register response: %w", err)
	}
	return &result, nil
}

func (c *apiClient) Keyring(cohortID string) (string, error) {
	path := "/api/f2f/cohorts/" + url.PathEscape(cohortID) + "/keyring"
	data, contentType, err := c.do(http.MethodGet, path, nil, maxKeyringResponse)
	if err != nil {
		return "", err
	}
	if !strings.HasPrefix(strings.ToLower(contentType), "text/plain") {
		return "", fmt.Errorf("keyring response has unexpected content type %q", contentType)
	}
	return string(data), nil
}
