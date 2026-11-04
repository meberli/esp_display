#pragma once
// Co4 login: Keycloak OAuth 2.0 Device Authorization Grant (RFC 8628) for a
// public client (no client secret, no password on the panel). Pure parsing and
// state logic, no network and no ESP-IDF: host-testable (tools/test_co4_oidc.cpp).
//
// Tokens are secrets: never log or display them. Callers only report set/unset.

#include <stddef.h>
#include <stdint.h>

// Endpoints from OIDC discovery (<realm>/.well-known/openid-configuration).
struct Co4Oidc {
  char device_ep[160];
  char token_ep[160];
  char end_session_ep[160];  // Keycloak logout (POST client_id + refresh_token)
  char revocation_ep[160];   // RFC 7009, optional
};
bool co4_parse_discovery(const char* json, size_t len, Co4Oidc* out);
// "<realm>/.well-known/openid-configuration" (realm without trailing slash).
bool co4_discovery_url(const char* realm, char* out, size_t n);

struct Co4DeviceAuth {
  char device_code[256];  // secret-ish: never shown or logged
  char user_code[16];
  char verify_uri[128];
  char verify_complete[200];  // QR code content
  int interval_s;
  int expires_s;
};
bool co4_parse_device_auth(const char* json, size_t len, Co4DeviceAuth* out);

enum class Co4TokErr : uint8_t {
  None,          // tokens issued
  Pending,       // authorization_pending
  SlowDown,      // slow_down: interval += 5 s
  Expired,       // expired_token (device code)
  Denied,        // access_denied
  InvalidGrant,  // invalid_grant: refresh token revoked / expired -> "Anmeldung abgelaufen"
  Other,         // any other error or unreadable answer
};
Co4TokErr co4_tok_err(const char* error);

// Token answer. access/refresh point to caller buffers (PSRAM); empty if missing.
struct Co4TokenOut {
  char* access;
  size_t access_cap;
  char* refresh;
  size_t refresh_cap;
  int expires_in;  // access token lifetime, s
  Co4TokErr err;
};
// false if the JSON is unreadable or a token does not fit (err = Other then).
bool co4_parse_token(const char* json, size_t len, Co4TokenOut* out);

// application/x-www-form-urlencoded: appends "&key=value" (no "&" when empty).
bool co4_form_add(char* buf, size_t cap, const char* key, const char* value);

// Token endpoint / device endpoint bodies for the public client: only
// client_id, never client_secret, never code_challenge (no PKCE). buf is
// overwritten; false (buf "") if it does not fit or an argument is empty.
bool co4_form_device(char* buf, size_t cap, const char* client_id, const char* scope);
bool co4_form_poll(char* buf, size_t cap, const char* client_id, const char* device_code);
bool co4_form_refresh(char* buf, size_t cap, const char* client_id, const char* refresh_token);

// Device-code polling: RFC 8628 3.5. Interval at least 5 s.
struct Co4Poll {
  int interval_s;
  int64_t deadline_ms;
};
enum class Co4PollStep : uint8_t { Wait, Done, Expired, Denied, Fail };
void co4_poll_init(Co4Poll* p, const Co4DeviceAuth& a, int64_t now_ms);
Co4PollStep co4_poll_step(Co4Poll* p, Co4TokErr e, int64_t now_ms);

// Refresh timing: renew at 70 % of the access token lifetime, at least 30 s
// before it expires and not sooner than 10 s from now.
int64_t co4_refresh_at(int64_t now_ms, int expires_in_s);

// Copy that never cuts a UTF-8 sequence (a half character draws as a box).
void co4_copy_utf8(char* dst, size_t n, const char* src);
