#include "loxone_structure.h"

#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

struct PsramAllocator {
  void* allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* pointer) { heap_caps_free(pointer); }
  void* reallocate(void* pointer, size_t new_size) {
    return heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

// Feeds LoxAPP3.json to ArduinoJson and yields every 16 KB, so parsing the
// large structure never starves the idle tasks (task watchdog, 5 s).
struct YieldingReader {
  const uint8_t* data;
  size_t length;
  size_t pos;
  int read() {
    if (pos >= length) return -1;
    if ((pos & 0x3FFF) == 0) vTaskDelay(1);
    return data[pos++];
  }
  size_t readBytes(char* out, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) {
      int c = read();
      if (c < 0) break;
      out[i] = static_cast<char>(c);
    }
    return i;
  }
};

void copy_trunc(char* dst, size_t n, const char* src) {
  if (n == 0) {
    return;
  }
  snprintf(dst, n, "%s", src != nullptr ? src : "");
}

bool is_switch_type(const char* type) {
  return strcmp(type, "Switch") == 0 || strcmp(type, "TimedSwitch") == 0 ||
         strcmp(type, "LightController") == 0 || strcmp(type, "Dimmer") == 0;
}

bool is_shutter_type(const char* type) { return strcmp(type, "Jalousie") == 0; }

const char* state_text(JsonObject states, const char* key) {
  if (states.isNull() || key == nullptr || !states.containsKey(key)) {
    return "";
  }
  JsonVariant field = states[key];
  if (field.is<const char*>()) {
    return field.as<const char*>();
  }
  if (field.is<JsonArray>()) {
    JsonVariant first = field[0];
    if (first.is<const char*>()) {
      return first.as<const char*>();
    }
  }
  return "";
}

// states.<key> is a uuid. Some controls publish that uuid as a one-element list.
void copy_state(LoxoneControl* dst, JsonObject ctrl, const char* key, LoxoneStateKind kind) {
  dst->state[0] = '\0';
  dst->state_kind = kLoxoneStateNone;
  const char* id = state_text(ctrl["states"].as<JsonObject>(), key);
  if (!loxone_id_ok(id, sizeof(dst->state))) {
    return;
  }
  copy_trunc(dst->state, sizeof(dst->state), id);
  dst->state_kind = kind;
}

bool read_favorite(JsonObject ctrl) {
  JsonVariant fav = ctrl["isFavorite"];
  if (fav.is<bool>()) {
    return fav.as<bool>();
  }
  if (fav.is<int>()) {
    return fav.as<int>() != 0;
  }
  if (fav.is<const char*>()) {
    const char* text = fav.as<const char*>();
    return strcmp(text, "true") == 0 || strcmp(text, "1") == 0;
  }
  return false;
}

void assign_state(LoxoneControl* dst, JsonObject ctrl, const char* type) {
  if (dst->kind == kLoxoneShutter) {
    copy_state(dst, ctrl, "position", kLoxoneStatePosition);
    return;
  }
  if (dst->kind != kLoxoneSwitch) {
    dst->state[0] = '\0';
    dst->state_kind = kLoxoneStateNone;
    return;
  }
  if (strcmp(type, "Dimmer") == 0) {
    copy_state(dst, ctrl, "position", kLoxoneStatePosition);
  } else if (strcmp(type, "LightController") == 0) {
    copy_state(dst, ctrl, "activeScene", kLoxoneStateScene);
  } else if (strcmp(type, "TimedSwitch") == 0) {
    // 0 = off, -1 = held on, otherwise the stairwell timer is still running.
    copy_state(dst, ctrl, "deactivationDelay", kLoxoneStateScene);
  } else {
    copy_state(dst, ctrl, "active", kLoxoneStateActive);
  }
}

int compare_controls(const void* left, const void* right) {
  const LoxoneControl* a = static_cast<const LoxoneControl*>(left);
  const LoxoneControl* b = static_cast<const LoxoneControl*>(right);
  int room = strcasecmp(a->room, b->room);
  if (room != 0) {
    return room;
  }
  return strcasecmp(a->name, b->name);
}

}  // namespace

bool loxone_parse_structure(const uint8_t* json, size_t length, LoxoneControl* out, size_t cap, size_t* shown,
                            size_t* supported, char* server, size_t server_n, char* error, size_t error_n) {
  if (shown != nullptr) {
    *shown = 0;
  }
  if (supported != nullptr) {
    *supported = 0;
  }
  if (server != nullptr && server_n > 0) {
    server[0] = '\0';
  }
  if (json == nullptr || length == 0 || out == nullptr || cap == 0) {
    snprintf(error, error_n, "Empty structure");
    return false;
  }

  size_t capacity = length * 2 + 4096;
  if (capacity < 8192) {
    capacity = 8192;
  }
  PsramJsonDocument doc(capacity);
  if (doc.capacity() == 0) {
    snprintf(error, error_n, "Not enough memory for LoxAPP3.json");
    return false;
  }

  const uint8_t* body = json;
  size_t body_len = length;
  if (body_len >= 3 && body[0] == 0xEF && body[1] == 0xBB && body[2] == 0xBF) {
    body += 3;
    body_len -= 3;
  }

  YieldingReader reader{body, body_len, 0};
  DeserializationError err = deserializeJson(doc, reader);
  if (err) {
    snprintf(error, error_n, "JSON parse error (%s)", err.c_str());
    return false;
  }

  if (!doc.containsKey("controls") && doc.containsKey("LL")) {
    JsonVariant code = doc["LL"]["Code"];
    if (code.is<const char*>()) {
      snprintf(error, error_n, "Miniserver code %s", code.as<const char*>());
    } else if (code.is<int>()) {
      snprintf(error, error_n, "Miniserver code %d", code.as<int>());
    } else {
      snprintf(error, error_n, "Miniserver did not return a structure");
    }
    return false;
  }

  if (server != nullptr && server_n > 0) {
    copy_trunc(server, server_n, doc["msInfo"]["msName"] | "");
  }

  JsonObject controls = doc["controls"].as<JsonObject>();
  if (controls.isNull()) {
    snprintf(error, error_n, "LoxAPP3.json has no controls");
    return false;
  }

  size_t count = 0;
  size_t total = 0;
  for (JsonPair kv : controls) {
    JsonObject ctrl = kv.value().as<JsonObject>();
    const char* type = ctrl["type"] | "";
    LoxoneKind kind;
    if (is_switch_type(type)) {
      kind = kLoxoneSwitch;
    } else if (is_shutter_type(type)) {
      kind = kLoxoneShutter;
    } else if (strcmp(type, "Pushbutton") == 0) {
      kind = kLoxonePulse;
    } else {
      continue;
    }
    total++;

    const char* action = ctrl["uuidAction"] | "";
    if (!loxone_id_ok(action, sizeof(out[0].action))) {
      action = kv.key().c_str();
    }
    if (!loxone_id_ok(action, sizeof(out[0].action))) {
      continue;
    }
    if (count >= cap) {
      continue;
    }

    const char* name = ctrl["name"] | "";
    if (name[0] == '\0') {
      name = type;
    }
    const char* room = "";
    const char* room_id = ctrl["room"] | "";
    if (room_id[0] != '\0') {
      room = doc["rooms"][room_id]["name"] | "";
    }

    copy_trunc(out[count].name, sizeof(out[count].name), name);
    copy_trunc(out[count].room, sizeof(out[count].room), room);
    copy_trunc(out[count].action, sizeof(out[count].action), action);
    out[count].kind = kind;
    out[count].favorite = read_favorite(ctrl);
    assign_state(&out[count], ctrl, type);
    count++;
  }

  if (count > 1) {
    qsort(out, count, sizeof(LoxoneControl), compare_controls);
  }
  if (shown != nullptr) {
    *shown = count;
  }
  if (supported != nullptr) {
    *supported = total;
  }
  return true;
}
