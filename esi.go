package evemarketdump

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"sync"
	"time"
)

const (
	esiRoot           = "https://esi.evetech.net"
	esiRequestTimeout = 7 * time.Second
	esiDateLayout     = "2006-01-02"
	esiTimeLayout     = "2006-01-02T15:04:05Z"
)

type esiResponse[T any] struct {
	data  *T
	pages int
}
type esiError struct {
	message string
	code    int
}

type jsonTimeoutError struct {
	Error   string `json:"error"`
	Timeout int    `json:"timeout"`
}
type jsonError struct {
	Error string `json:"error"`
}
type jsonSsoResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int64  `json:"expires_in"`
	RefreshToken string `json:"refresh_token"`
}

var ErrNoTrailsLeft = errors.New("No trails left")
var ErrImplicitTimeout = errors.New("Esi implicit timeout")
var ErrErrorRateTimeout = errors.New("Esi error rate timeout")
var ErrExplicitTimeout = errors.New("Esi explicit timeout")

var esiTimeout time.Time
var esiTimeoutMu sync.Mutex
var accessToken string
var accessTokenExpiry time.Time
var accessTokenMu sync.Mutex

func esiFetch[T any](
	ctx context.Context,
	method string,
	uri string,
	authenticated bool,
	secrets *ApiSecrets,
	trails int,
) (esiResponse[T], error) {
	// If no tries left, fail the request
	if trails <= 0 {
		return esiResponse[T]{}, ErrNoTrailsLeft
	}

	// Init retry function that will be called in case the request fail
	retry := func(fallbackErr error) (esiResponse[T], error) {
		retryResponse, retryErr := esiFetch[T](ctx, method, uri, authenticated, secrets, trails-1)
		if errors.Is(retryErr, ErrNoTrailsLeft) {
			return esiResponse[T]{}, fmt.Errorf("no trails left: %w", fallbackErr)
		}
		return retryResponse, retryErr
	}

	// Wait for the api to be clear of any timeout
	timeoutCtx, cancel := context.WithTimeout(ctx, 15*time.Minute)
	err := esiClearTimeout(timeoutCtx)
	cancel()
	if err != nil {
		return esiResponse[T]{}, fmt.Errorf("esi timeout clearing: %w", err)
	}

	// Create the request
	request, err := http.NewRequestWithContext(ctx, method, esiRoot+uri, nil)
	if err != nil {
		return esiResponse[T]{}, fmt.Errorf("new request: %w", err)
	}
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("User-Agent", "evemarketbrowser.com - contact me at raphguyader@gmail.com")
	if authenticated {
		if secrets == nil {
			panic("evemarketdump.esiFetch: secrets is nil for authentificated request")
		}
		token, err := acquireSSOToken(ctx, secrets)
		if err != nil {
			return esiResponse[T]{}, fmt.Errorf("acquire SSO token: %w", err)
		}
		request.Header.Set("Authorization", "Bearer "+token)
	}

	// Run the request
	client := &http.Client{
		Timeout: esiRequestTimeout,
	}
	response, err := client.Do(request)
	if err != nil {
		if errors.Is(err, ctx.Err()) {
			return esiResponse[T]{}, fmt.Errorf("esi request: %w", err)
		}
		return retry(fmt.Errorf("http request: %w", err))
	}
	defer response.Body.Close()

	// Handle implicit timeout
	if response.StatusCode == 500 || response.StatusCode == 503 {
		esiSetTimeout(20)

		if isLoggingEnabled(ctx) {
			log.Printf("Esi fetch: 20s implicit esi timeout %d", response.StatusCode)
		}
		return retry(ErrImplicitTimeout)
	}

	// Handle request rate timeout
	if response.StatusCode == 429 {
		// See https://developers.eveonline.com/docs/services/esi/rate-limiting/#token-system
		// NOTE: one day, CCP will maybe add a `Retry-After` header to their
		// responses
		var timeout time.Duration
		retryAfter := response.Header.Get("Retry-After")
		if len(retryAfter) == 0 {
			if isLoggingEnabled(ctx) {
				log.Printf("Esi fetch: No Retry-After provided")
			}
			timeout = 20 * time.Second
		} else {
			secs, err := strconv.Atoi(retryAfter)
			if err != nil {
				if isLoggingEnabled(ctx) {
					log.Printf("Esi fetch: Can't decode Retry-After: '%s'", retryAfter)
				}
				timeout = 20 * time.Second
			} else if secs < 0 || secs > 240 {
				if isLoggingEnabled(ctx) {
					log.Printf("Esi fetch: Retry-After out of range: %ds", secs)
				}
				timeout = 20 * time.Second
			} else {
				timeout = time.Duration(secs) * time.Second
			}
		}
		esiSetTimeout(timeout)

		if isLoggingEnabled(ctx) {
			log.Printf("Esi fetch: %fs request rate timeout", timeout.Seconds())
		}
		return retry(ErrErrorRateTimeout)
	}

	// Handle error rate timeout
	if response.StatusCode == 420 {
		var timeout time.Duration
		secs, err := strconv.Atoi(response.Header.Get("X-Esi-Error-Limit-Reset"))
		if err != nil {
			if isLoggingEnabled(ctx) {
				log.Print("Esi fetch: Can't decode X-Esi-Error-Limit-Reset")
			}
			timeout = 10 * time.Second
		} else if secs < 0 || secs > 120 {
			if isLoggingEnabled(ctx) {
				log.Printf("Esi fetch: X-Esi-Error-Limit-Reset out of range: %ds", secs)
			}
			timeout = 10 * time.Second
		} else {
			timeout = time.Duration(secs) * time.Second
		}
		esiSetTimeout(timeout)

		if isLoggingEnabled(ctx) {
			log.Printf("Esi fetch: %fs error rate timeout", timeout.Seconds())
		}
		return retry(ErrErrorRateTimeout)
	}

	decoder := json.NewDecoder(response.Body)
	// Handle gateway timeout
	if response.StatusCode == 504 {
		var timeoutError jsonTimeoutError
		var timeout time.Duration
		err = decoder.Decode(&timeoutError)
		if err != nil {
			if isLoggingEnabled(ctx) {
				log.Print("Esi fetch: Can't decode esi timeout")
			}
			timeout = 10 * time.Second
		} else if timeoutError.Timeout < 0 || timeoutError.Timeout > 120 {
			if isLoggingEnabled(ctx) {
				log.Printf("Esi fetch: esi timeout out of range: %ds", timeoutError.Timeout)
			}
			timeout = 10 * time.Second
		} else {
			timeout = time.Duration(timeoutError.Timeout) * time.Second
		}
		esiSetTimeout(timeout)

		if isLoggingEnabled(ctx) {
			log.Printf("Esi fetch: %fs explicit esi timeout", timeout.Seconds())
		}
		return retry(ErrExplicitTimeout)
	}

	// Handle esi error
	if response.StatusCode != 200 {
		var error jsonError
		err = decoder.Decode(&error)
		if err != nil {
			log.Printf("Esi fetch: Can't decode esi error")
			return retry(fmt.Errorf("decoding esi error: %w", err))
		}

		return esiResponse[T]{}, &esiError{message: error.Error, code: response.StatusCode}
	}

	var data T
	err = decoder.Decode(&data)
	if err != nil {
		log.Print("Esi fetch: Can't decode 200 response body")
		return retry(fmt.Errorf("decoding response body: %w", err))
	}

	var pages int
	xPages := response.Header.Get("X-Pages")
	if xPages != "" {
		pages, err = strconv.Atoi(xPages)
		if err != nil {
			log.Print("Esi fetch: Can't decode X-Pages")
		} else if pages < 0 || pages > 10000 {
			log.Printf("Esi fetch: X-Pages out of range: %d", pages)
		}
	}

	esiResponse := esiResponse[T]{
		data:  &data,
		pages: pages,
	}

	return esiResponse, nil
}

func acquireSSOToken(ctx context.Context, secrets *ApiSecrets) (string, error) {
	now := time.Now()
	if now.Before(accessTokenExpiry) {
		return accessToken, nil
	}

	// Create the request
	url := "https://login.eveonline.com/v2/oauth/token"
	var body bytes.Buffer
	fmt.Fprintf(&body, "grant_type=refresh_token&refresh_token=%s", secrets.SsoRefreshToken)
	request, err := http.NewRequestWithContext(ctx, "POST", url, &body)
	if err != nil {
		return "", fmt.Errorf("new request: %w", err)
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	request.Header.Set("User-Agent", "evemarketbrowser.com - contact me at raphguyader@gmail.com")
	request.Header.Set("Authorization", createBasicAuthHeader(secrets.SsoClientId, secrets.SsoClientSecret))

	// Run the request
	client := &http.Client{
		Timeout: esiRequestTimeout,
	}
	response, err := client.Do(request)
	if err != nil {
		return "", fmt.Errorf("http request: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != 200 {
		return "", fmt.Errorf("%d status code", response.StatusCode)
	}

	// Read the response
	var ssoResponse jsonSsoResponse
	decoder := json.NewDecoder(response.Body)
	err = decoder.Decode(&ssoResponse)
	if err != nil {
		return "", fmt.Errorf("unmarshal sso response: %w", err)
	}
	if ssoResponse.TokenType != "Bearer" || ssoResponse.RefreshToken != secrets.SsoRefreshToken {
		return "", fmt.Errorf("unexpected sso repseonse: %v", ssoResponse)
	}

	accessToken = ssoResponse.AccessToken
	accessTokenExpiry = now.Add(time.Duration(ssoResponse.ExpiresIn) * time.Second)
	if time.Now().After(accessTokenExpiry) {
		return "", fmt.Errorf("access token expired faster than flash mcqueen which is already faster than light")
	}

	return accessToken, nil
}

func createBasicAuthHeader(user string, password string) string {
	payload := user + ":" + password
	return "Basic " + base64.StdEncoding.EncodeToString([]byte(payload))
}

func esiSetTimeout(duration time.Duration) {
	esiTimeoutMu.Lock()
	esiTimeout = time.Now().Add(duration)
	esiTimeoutMu.Unlock()
}

// WARN: Call this function in a timeout context to avoid inifite loops
func esiClearTimeout(ctx context.Context) error {
	for {
		esiTimeoutMu.Lock()
		esiTimeoutCopy := esiTimeout
		esiTimeoutMu.Unlock()
		if time.Now().After(esiTimeoutCopy) {
			return nil
		}
		timer := time.NewTimer(time.Until(esiTimeoutCopy))
		select {
		case <-timer.C:
		case <-ctx.Done():
			// NOTE: in go 1.23 it would not be necessary to drain the timer channel
			if !timer.Stop() {
				<-timer.C
			}
			return ctx.Err()
		}
	}
}

func (e *esiError) Error() string {
	return fmt.Sprintf("Esi error %d : %s", e.code, e.message)
}
