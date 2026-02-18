package evemarketdump

import "context"

// ApiSecrets are required to fetch player strcture info (via the
// /universe/structures api endpoint).
//
// To get your clientId and clientSecret create and application at
// https://developers.eveonline.com/applications (CCP likes to change the url
// of this page from time to time). The refresh token allows authentifiation to
// an EVE character. To get the refresh token your need to follow the steps of
// SSO authentification until you recive, see
// https://docs.esi.evetech.net/docs/sso/web_based_sso_flow.html.
//
// A git push request with a bash script that simplify the request of getting
// refresh token for a character would be much appreciated.
type ApiSecrets struct {
	SsoClientId     string
	SsoClientSecret string
	SsoRefreshToken string
}

type loggingKeyType struct{}

var loggingKey = loggingKeyType{}

// returns a context that can be passed to library function to enable logging
func EnableLogging(ctx context.Context) context.Context {
	return context.WithValue(ctx, loggingKey, true)
}

func isLoggingEnabled(ctx context.Context) bool {
	v, ok := ctx.Value(loggingKey).(bool)
	return ok && v
}
