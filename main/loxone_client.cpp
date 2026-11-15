#include "esp_attr.h"
#include "loxone_client.h"

#include <ArduinoJson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config_load.h"
#include "loxone_err.h"
#include "loxone_host.h"
#include "loxone_cmd.h"
#include "loxone_types.h"
#include "net.h"
#include "settings.h"
#include "strings_de.h"

namespace {

void set_detail(LoxoneHttpResult* result, const char* text) {
  snprintf(result->detail, sizeof(result->detail), "%s", text != nullptr ? text : "");
}

// Host only (no scheme, path or :port). Rejects HTTPS. A leftover "ip:8080"
// in the stored host is split so the URL is never http://ip:8080:port/...
bool normalize_host_of(const char* host, uint16_t default_port, char* out, size_t out_n, uint16_t* port,
                       char* error, size_t error_n) {
  return loxone_host::parse(host, out, out_n, port, error, error_n, default_port);
}

bool normalize_host(char* out, size_t out_n, uint16_t* port, char* error, size_t error_n) {
  return normalize_host_of(settings_loxone().host, settings_loxone().port, out, out_n, port, error, error_n);
}

bool code_is_200(JsonVariant code) {
  if (code.is<int>()) {
    return code.as<int>() == 200;
  }
  if (code.is<const char*>()) {
    return strcmp(code.as<const char*>(), "200") == 0;
  }
  return false;
}

}  // namespace

LoxoneHttpResult loxone_get(LoxoneAuthorizer& auth, const char* path, uint8_t* body, size_t cap,
                            size_t* out_len) {
  LoxoneHttpResult result = {};
  result.ok = false;
  result.status = 0;
  if (out_len != nullptr) {
    *out_len = 0;
  }
  if (path == nullptr || path[0] != '/' || body == nullptr || cap == 0) {
    set_detail(&result, str::kLoxBadRequest);
    return result;
  }
  if (!wifi_connected()) {
    set_detail(&result, str::kLoxWifiDown);
    return result;
  }

  char host[80];
  uint16_t port = settings_loxone().port;
  if (!normalize_host(host, sizeof(host), &port, result.detail, sizeof(result.detail))) {
    return result;
  }
  const char* user = nullptr;
  const char* password = nullptr;
  if (!auth.apply(&user, &password)) {
    snprintf(result.detail, sizeof(result.detail), "Auth scheme \"%s\" is not available", auth.name());
    return result;
  }
  char url[256];
  int nurl = snprintf(url, sizeof(url), "http://%s:%d%s", host, static_cast<int>(port), path);
  if (nurl <= 0 || static_cast<size_t>(nurl) >= sizeof(url)) {
    set_detail(&result, str::kLoxPathTooLong);
    return result;
  }

  // 5 s per connect/read (was 20 s): an absent Miniserver must not block the
  // panel; http_get_raw additionally bounds the whole body to 6x this.
  constexpr int kLoxTimeoutMs = 5000;
  HttpGetResult r = http_get(url, user, password, body, cap, kLoxTimeoutMs);
  int code = r.status;
  result.status = code;
  if (code <= 0) {
    snprintf(result.detail, sizeof(result.detail), "No HTTP response (%s)", r.error);
    return result;
  }
  if (code == 401 || code == 403) {
    set_detail(&result, str::kLoxLoginRejected);
    return result;
  }
  if (!r.ok) {
    set_detail(&result, r.error);
    return result;
  }
  size_t total = r.length;

  if (code != 200) {
    snprintf(result.detail, sizeof(result.detail), "HTTP %d", code);
    return result;
  }
  if (total == 0) {
    set_detail(&result, str::kLoxEmpty);
    return result;
  }
  if (out_len != nullptr) {
    *out_len = total;
  }
  result.ok = true;
  set_detail(&result, "OK");
  return result;
}

LoxoneHttpResult loxone_send_path(LoxoneAuthorizer& auth, const char* path) {
  LoxoneHttpResult result = {};
  uint8_t body[640];
  size_t length = 0;
  result = loxone_get(auth, path, body, sizeof(body), &length);
  if (!result.ok) {
    return result;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) {
    return result;
  }
  JsonVariant code = doc["LL"]["Code"];
  if (code.isNull()) {
    code = doc["LL"]["@Code"];
  }
  if (code.isNull()) {
    return result;
  }
  if (!code_is_200(code)) {
    result.ok = false;
    if (code.is<int>()) {
      snprintf(result.detail, sizeof(result.detail), "Miniserver code %d", code.as<int>());
    } else if (code.is<const char*>()) {
      snprintf(result.detail, sizeof(result.detail), "Miniserver code %s", code.as<const char*>());
    } else {
      set_detail(&result, str::kLoxCommandRejected);
    }
  }
  return result;
}

LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command) {
  return loxone_send_io(auth, action, command, 0, 0);
}

LoxoneHttpResult loxone_send_io(LoxoneAuthorizer& auth, const char* action, const char* command,
                                int level, uint32_t seconds) {
  LoxoneHttpResult result = {};
  char path[160];
  bool ok = false;
  if (command != nullptr && strcmp(command, "startOverride") == 0) {
    ok = lox_path_start_override(action, level, seconds, path, sizeof(path));
  } else if (command != nullptr && strcmp(command, "stopOverride") == 0) {
    ok = lox_path_stop_override(action, path, sizeof(path));
  } else {
    ok = lox_path_named(action, command, path, sizeof(path));
  }
  if (!ok) {
    set_detail(&result, str::kLoxIdRejected);
    return result;
  }
  return loxone_send_path(auth, path);
}

bool copy_number_text(const char* text, char* out, size_t out_n) {
  if (text == nullptr || out == nullptr || out_n == 0) {
    return false;
  }
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  if (strcasecmp(text, "true") == 0 || strcasecmp(text, "on") == 0) {
    snprintf(out, out_n, "1");
    return true;
  }
  if (strcasecmp(text, "false") == 0 || strcasecmp(text, "off") == 0) {
    snprintf(out, out_n, "0");
    return true;
  }
  char* end = nullptr;
  (void)strtof(text, &end);
  if (end == text) {
    return false;
  }
  size_t n = static_cast<size_t>(end - text);
  if (n >= out_n) {
    n = out_n - 1;
  }
  memcpy(out, text, n);
  out[n] = '\0';
  return true;
}

bool copy_variant_number(JsonVariant value, char* out, size_t out_n) {
  if (value.is<const char*>()) {
    return copy_number_text(value.as<const char*>(), out, out_n);
  }
  if (value.is<bool>()) {
    snprintf(out, out_n, "%d", value.as<bool>() ? 1 : 0);
    return true;
  }
  if (value.is<int>() || value.is<long>()) {
    snprintf(out, out_n, "%ld", static_cast<long>(value.as<long>()));
    return true;
  }
  if (value.is<float>()) {
    snprintf(out, out_n, "%.4f", value.as<float>());
    return true;
  }
  return false;
}

bool copy_object_number(JsonObject object, const char* key, char* out, size_t out_n) {
  if (object.isNull() || !object.containsKey(key)) {
    return false;
  }
  return copy_variant_number(object[key], out, out_n);
}

// /all is either a number or an object of outputs (StatePos for a shutter).
bool copy_all_value(JsonVariant raw, LoxoneKind kind, bool shutter_position_only, char* out, size_t out_n) {
  if (raw.is<JsonObject>()) {
    JsonObject object = raw.as<JsonObject>();
    if (kind == kLoxoneShutter) {
      if (copy_object_number(object, "StatePos", out, out_n) ||
          copy_object_number(object, "position", out, out_n)) {
        return true;
      }
      if (shutter_position_only) {
        return false;
      }
    } else if (copy_object_number(object, "active", out, out_n) ||
               copy_object_number(object, "activeScene", out, out_n) ||
               copy_object_number(object, "position", out, out_n)) {
      return true;
    }
    return copy_object_number(object, "value", out, out_n);
  }
  if (shutter_position_only) {
    return false;
  }
  if (raw.is<const char*>()) {
    const char* text = raw.as<const char*>();
    if (text != nullptr && text[0] == '{') {
      StaticJsonDocument<768> nested;
      if (deserializeJson(nested, text) == DeserializationError::Ok) {
        return copy_all_value(nested.as<JsonVariant>(), kind, false, out, out_n);
      }
    }
  }
  return copy_variant_number(raw, out, out_n);
}

bool fetch_ll_value(LoxoneAuthorizer& auth, const char* id, const char* command, JsonDocument& doc,
                    JsonVariant* raw, LoxoneHttpResult* result) {
  char path[160];
  int wrote = snprintf(path, sizeof(path), "/jdev/sps/io/%s/%s", id, command);
  if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(path)) {
    set_detail(result, str::kLoxPathTooLong);
    return false;
  }
  EXT_RAM_BSS_ATTR static uint8_t body[1200];
  size_t length = 0;
  *result = loxone_get(auth, path, body, sizeof(body), &length);
  if (!result->ok) {
    return false;
  }
  const uint8_t* json = body;
  size_t json_len = length;
  if (json_len >= 3 && json[0] == 0xEF && json[1] == 0xBB && json[2] == 0xBF) {
    json += 3;
    json_len -= 3;
  }
  if (deserializeJson(doc, json, json_len) != DeserializationError::Ok) {
    result->ok = false;
    set_detail(result, str::kLoxStateNotJson);
    return false;
  }
  JsonObject ll = doc["LL"].as<JsonObject>();
  JsonVariant code = ll["Code"];
  if (code.isNull()) {
    code = ll["@Code"];
  }
  if (!code.isNull() && !code_is_200(code)) {
    result->ok = false;
    set_detail(result, str::kLoxStateUnavailable);
    return false;
  }
  *raw = ll["value"];
  if (raw->isNull()) {
    *raw = ll["@value"];
  }
  if (raw->isNull()) {
    result->ok = false;
    set_detail(result, str::kLoxStateNoValue);
    return false;
  }
  return true;
}

int32_t timer_override_from_all(JsonObject ll) {
  if (ll.isNull()) {
    return -1;
  }
  if (ll.containsKey("override")) {
    char buf[32];
    if (copy_variant_number(ll["override"], buf, sizeof(buf))) {
      char* end = nullptr;
      long v = strtol(buf, &end, 10);
      if (end != buf && v >= 0 && v <= static_cast<long>(kLoxOverrideMaxSecs)) {
        return static_cast<int32_t>(v);
      }
    }
  }
  for (JsonPair kv : ll) {
    const char* key = kv.key().c_str();
    if (strncmp(key, "output", 6) != 0) {
      continue;
    }
    JsonObject o = kv.value().as<JsonObject>();
    if (o.isNull()) {
      continue;
    }
    const char* name = o["name"] | "";
    if (strcmp(name, "Rt") != 0 && strcasecmp(name, "override") != 0) {
      continue;
    }
    char buf[32];
    if (!copy_object_number(o, "value", buf, sizeof(buf))) {
      continue;
    }
    char* end = nullptr;
    long v = strtol(buf, &end, 10);
    if (end != buf && v >= 0 && v <= static_cast<long>(kLoxOverrideMaxSecs)) {
      return static_cast<int32_t>(v);
    }
  }
  return -1;
}

void apply_timer_override(JsonDocument& doc, char* value, size_t value_n, int32_t* override_s) {
  int32_t ov = timer_override_from_all(doc["LL"].as<JsonObject>());
  if (override_s != nullptr) {
    *override_s = ov;
  }
  if (ov >= 0 && value != nullptr && value_n > 0) {
    size_t used = strlen(value);
    if (used + 2 < value_n) {
      snprintf(value + used, value_n - used, " %ld", static_cast<long>(ov));
    }
  }
}

LoxoneHttpResult loxone_read_state(LoxoneAuthorizer& auth, const char* action, const char* state,
                                   LoxoneKind kind, char* value, size_t value_n, int32_t* override_s) {
  LoxoneHttpResult result = {};
  if (value == nullptr || value_n == 0) {
    set_detail(&result, str::kLoxStateIdRejected);
    return result;
  }
  value[0] = '\0';
  if (override_s != nullptr) {
    *override_s = -1;
  }

  EXT_RAM_BSS_ATTR static StaticJsonDocument<1536> doc;
  char fallback[48];
  fallback[0] = '\0';
  bool have_action = loxone_id_ok(action, 40);

  if (have_action) {
    const char* queries[] = {"all", "All"};
    for (const char* query : queries) {
      JsonVariant raw;
      LoxoneHttpResult all = {};
      if (!fetch_ll_value(auth, action, query, doc, &raw, &all)) {
        result = all;
        // A rejected login must not be retried as /All or /state: each call
        // is another failed attempt and extends a Miniserver lockout.
        if (result.status == 401 || result.status == 403) return result;
        continue;
      }
      if (kind == kLoxoneShutter && copy_all_value(raw, kind, true, value, value_n)) {
        set_detail(&all, "OK");
        return all;
      }
      if (kind != kLoxoneShutter && copy_all_value(raw, kind, false, value, value_n)) {
        if (kind == kLoxoneTimer) {
          apply_timer_override(doc, value, value_n, override_s);
        }
        set_detail(&all, "OK");
        return all;
      }
      if (kind == kLoxoneShutter && fallback[0] == '\0') {
        copy_all_value(raw, kind, false, fallback, sizeof(fallback));
      }
      result = all;
      break;
    }
  }

  if (loxone_id_ok(state, 40)) {
    JsonVariant raw;
    LoxoneHttpResult direct = {};
    if (fetch_ll_value(auth, state, "state", doc, &raw, &direct) &&
        copy_variant_number(raw, value, value_n)) {
      set_detail(&direct, "OK");
      return direct;
    }
    result = direct;
    if (result.status == 401 || result.status == 403) return result;
  }

  if (fallback[0] != '\0') {
    snprintf(value, value_n, "%s", fallback);
    result.ok = true;
    set_detail(&result, "OK");
    return result;
  }
  if (result.detail[0] == '\0') {
    set_detail(&result, str::kLoxStateUnavailable);
  }
  result.ok = false;
  return result;
}

LoxoneHttpResult loxone_test(const LoxoneConfig& c) {
  LoxoneHttpResult result = {};
  if (!wifi_connected()) {
    set_detail(&result, str::kLoxWifiDown);
    return result;
  }
  char host[80];
  uint16_t port = c.port != 0 ? c.port : static_cast<uint16_t>(80);
  if (!normalize_host_of(c.host, port, host, sizeof(host), &port, result.detail, sizeof(result.detail))) {
    return result;
  }
  char url[192];
  snprintf(url, sizeof(url), "http://%s:%d/jdev/sps/LoxAPPversion3", host, static_cast<int>(port));
  uint8_t body[256];
  HttpGetResult r = http_get(url, c.user, c.pass, body, sizeof(body), 10000);
  result.status = r.status;
  if (r.status == 200) {
    result.ok = true;
    set_detail(&result, "OK");
  } else {
    loxone_err::test_msg(r.status, r.error, result.detail, sizeof(result.detail));
  }
  return result;
}
