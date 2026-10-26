#include "co4_oidc.h"

#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
namespace {
struct PsramAlloc {
  void* allocate(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
using Doc = BasicJsonDocument<PsramAlloc>;
}  // namespace
#else
using Doc = DynamicJsonDocument;
#endif

namespace {
bool put(char* dst, size_t n, const char* v) {
  if (v == nullptr) v = "";
  size_t l = strlen(v);
  if (l >= n) {
    dst[0] = '\0';
    return false;
  }
  memcpy(dst, v, l + 1);
  return true;
}

bool https(const char* s) { return strncmp(s, "https://", 8) == 0; }
}  // namespace

void co4_copy_utf8(char* dst, size_t n, const char* src) {
  if (n == 0) return;
  size_t l = src != nullptr ? strlen(src) : 0;
  if (l >= n) {
    l = n - 1;
    while (l > 0 && (static_cast<unsigned char>(src[l]) & 0xC0) == 0x80) --l;  // l = start of a cut sequence
  }
  if (l > 0) memcpy(dst, src, l);
  dst[l] = '\0';
}

bool co4_discovery_url(const char* realm, char* out, size_t n) {
  size_t l = strlen(realm);
  while (l > 0 && realm[l - 1] == '/') --l;
  int w = snprintf(out, n, "%.*s/.well-known/openid-configuration", static_cast<int>(l), realm);
  return l > 0 && w > 0 && static_cast<size_t>(w) < n && https(out);
}

bool co4_parse_discovery(const char* json, size_t len, Co4Oidc* out) {
  memset(out, 0, sizeof(*out));
  StaticJsonDocument<192> filter;
  filter["device_authorization_endpoint"] = true;
  filter["token_endpoint"] = true;
  filter["end_session_endpoint"] = true;
  filter["revocation_endpoint"] = true;
  Doc doc(2048);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  bool ok = put(out->device_ep, sizeof(out->device_ep), doc["device_authorization_endpoint"] | "") &&
            put(out->token_ep, sizeof(out->token_ep), doc["token_endpoint"] | "");
  put(out->end_session_ep, sizeof(out->end_session_ep), doc["end_session_endpoint"] | "");
  put(out->revocation_ep, sizeof(out->revocation_ep), doc["revocation_endpoint"] | "");
  // Only https endpoints (a tampered discovery must not send tokens in clear).
  if (out->end_session_ep[0] != '\0' && !https(out->end_session_ep)) out->end_session_ep[0] = '\0';
  if (out->revocation_ep[0] != '\0' && !https(out->revocation_ep)) out->revocation_ep[0] = '\0';
  return ok && https(out->device_ep) && https(out->token_ep);
}

bool co4_parse_device_auth(const char* json, size_t len, Co4DeviceAuth* out) {
  memset(out, 0, sizeof(*out));
  Doc doc(2048);
  if (deserializeJson(doc, json, len)) return false;
  bool ok = put(out->device_code, sizeof(out->device_code), doc["device_code"] | "") &&
            put(out->user_code, sizeof(out->user_code), doc["user_code"] | "") &&
            put(out->verify_uri, sizeof(out->verify_uri), doc["verification_uri"] | "");
  put(out->verify_complete, sizeof(out->verify_complete), doc["verification_uri_complete"] | "");
  if (out->verify_complete[0] == '\0') put(out->verify_complete, sizeof(out->verify_complete), out->verify_uri);
  out->interval_s = doc["interval"] | 5;
  if (out->interval_s < 5) out->interval_s = 5;
  out->expires_s = doc["expires_in"] | 600;
  if (out->expires_s <= 0) out->expires_s = 600;
  return ok && out->device_code[0] != '\0' && out->user_code[0] != '\0' && https(out->verify_uri);
}

Co4TokErr co4_tok_err(const char* e) {
  if (e == nullptr || e[0] == '\0') return Co4TokErr::None;
  if (strcmp(e, "authorization_pending") == 0) return Co4TokErr::Pending;
  if (strcmp(e, "slow_down") == 0) return Co4TokErr::SlowDown;
  if (strcmp(e, "expired_token") == 0) return Co4TokErr::Expired;
  if (strcmp(e, "access_denied") == 0) return Co4TokErr::Denied;
  if (strcmp(e, "invalid_grant") == 0) return Co4TokErr::InvalidGrant;
  return Co4TokErr::Other;
}

bool co4_parse_token(const char* json, size_t len, Co4TokenOut* out) {
  if (out->access_cap > 0) out->access[0] = '\0';
  if (out->refresh_cap > 0) out->refresh[0] = '\0';
  out->expires_in = 0;
  out->err = Co4TokErr::Other;
  StaticJsonDocument<128> filter;
  filter["access_token"] = true;
  filter["refresh_token"] = true;
  filter["expires_in"] = true;
  filter["error"] = true;
  Doc doc(len + 512);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  const char* e = doc["error"] | "";
  if (e[0] != '\0') {
    out->err = co4_tok_err(e);
    return true;
  }
  if (!put(out->access, out->access_cap, doc["access_token"] | "") ||
      !put(out->refresh, out->refresh_cap, doc["refresh_token"] | "")) {
    if (out->access_cap > 0) out->access[0] = '\0';
    if (out->refresh_cap > 0) out->refresh[0] = '\0';
    return false;
  }
  if (out->access[0] == '\0') return false;
  out->expires_in = doc["expires_in"] | 300;
  out->err = Co4TokErr::None;
  return true;
}

bool co4_form_add(char* buf, size_t cap, const char* key, const char* value) {
  size_t l = strlen(buf);
  auto add = [&](char c) {
    if (l + 1 >= cap) return false;
    buf[l++] = c;
    buf[l] = '\0';
    return true;
  };
  auto enc = [&](const char* s) {
    static const char kHex[] = "0123456789ABCDEF";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
      unsigned char c = *p;
      bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                   c == '_' || c == '.' || c == '~';
      if (plain) {
        if (!add(static_cast<char>(c))) return false;
      } else if (c == ' ') {
        if (!add('+')) return false;
      } else if (!add('%') || !add(kHex[c >> 4]) || !add(kHex[c & 15])) {
        return false;
      }
    }
    return true;
  };
  size_t start = l;
  bool ok = (l == 0 || add('&')) && enc(key) && add('=') && enc(value);
  if (!ok) buf[start] = '\0';  // never leave half a pair
  return ok;
}

void co4_poll_init(Co4Poll* p, const Co4DeviceAuth& a, int64_t now_ms) {
  p->interval_s = a.interval_s < 5 ? 5 : a.interval_s;
  p->deadline_ms = now_ms + static_cast<int64_t>(a.expires_s) * 1000;
}

Co4PollStep co4_poll_step(Co4Poll* p, Co4TokErr e, int64_t now_ms) {
  switch (e) {
    case Co4TokErr::None: return Co4PollStep::Done;
    case Co4TokErr::Denied: return Co4PollStep::Denied;
    case Co4TokErr::Expired: return Co4PollStep::Expired;
    case Co4TokErr::SlowDown:
      p->interval_s += 5;
      if (p->interval_s > 60) p->interval_s = 60;
      break;
    case Co4TokErr::Pending: break;
    case Co4TokErr::InvalidGrant:  // device code no longer valid
      return Co4PollStep::Expired;
    case Co4TokErr::Other: return Co4PollStep::Fail;
  }
  return now_ms + static_cast<int64_t>(p->interval_s) * 1000 > p->deadline_ms ? Co4PollStep::Expired
                                                                               : Co4PollStep::Wait;
}

int64_t co4_refresh_at(int64_t now_ms, int expires_in_s) {
  if (expires_in_s <= 0) expires_in_s = 60;
  int64_t life = static_cast<int64_t>(expires_in_s) * 1000;
  int64_t at = now_ms + life * 7 / 10;
  if (now_ms + life - at < 30000) at = now_ms + life - 30000;
  if (at < now_ms + 10000) at = now_ms + 10000;
  return at;
}
