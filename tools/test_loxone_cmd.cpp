// g++ -std=c++17 -Wall -I main tools/test_loxone_cmd.cpp -o /tmp/tlxcmd && /tmp/tlxcmd
#include <stdio.h>
#include <string.h>

#include "loxone_cmd.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  const char* id = "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01";
  char path[160];

  CHECK(lox_timer_level_ok(0) && lox_timer_level_ok(10) && !lox_timer_level_ok(-1) && !lox_timer_level_ok(11));
  CHECK(lox_override_secs_ok(1) && lox_override_secs_ok(kLoxOverrideUntilCancel) && !lox_override_secs_ok(0));
  CHECK(!lox_override_secs_ok(kLoxOverrideMaxSecs + 1));

  CHECK(lox_duration_seconds(kLoxDur30m) == 1800);
  CHECK(lox_duration_seconds(kLoxDur1h) == 3600);
  CHECK(lox_duration_seconds(kLoxDur2h) == 7200);
  CHECK(lox_duration_seconds(kLoxDur4h) == 14400);
  CHECK(lox_duration_seconds(kLoxDurUntilCancel) == 7 * 24 * 3600);
  CHECK(strcmp(lox_duration_label(kLoxDurUntilCancel), "Bis ich beende") == 0);

  CHECK(lox_path_start_override(id, 0, 1800, path, sizeof(path)));
  CHECK(strcmp(path, "/jdev/sps/io/aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01/startOverride/0/1800") == 0);
  CHECK(lox_path_start_override(id, 10, 3600, path, sizeof(path)));
  CHECK(strstr(path, "/startOverride/10/3600") != nullptr);
  CHECK(lox_path_start_override(id, 3, kLoxOverrideUntilCancel, path, sizeof(path)));
  CHECK(strstr(path, "/startOverride/3/604800") != nullptr);

  CHECK(!lox_path_start_override(id, 11, 1800, path, sizeof(path)));
  CHECK(path[0] == '\0');
  CHECK(!lox_path_start_override(id, 5, 0, path, sizeof(path)));
  CHECK(!lox_path_start_override("a/b", 5, 1800, path, sizeof(path)));
  CHECK(!lox_path_start_override("", 5, 1800, path, sizeof(path)));
  CHECK(!lox_path_start_override(id, 5, 1800, path, 20));  // too short

  CHECK(lox_path_stop_override(id, path, sizeof(path)));
  CHECK(strcmp(path, "/jdev/sps/io/aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01/stopOverride") == 0);
  CHECK(!lox_path_stop_override("bad id!", path, sizeof(path)));

  CHECK(lox_path_named(id, "Pulse", path, sizeof(path)));
  CHECK(strstr(path, "/Pulse") != nullptr && strstr(path, "startOverride") == nullptr);
  CHECK(!lox_path_named(id, "startOverride/5/1800", path, sizeof(path)));  // slash rejected
  CHECK(!lox_path_named(id, "On;Off", path, sizeof(path)));

  char buf[32];
  lox_format_level(0, buf, sizeof(buf));
  CHECK(strcmp(buf, "Aus") == 0);
  lox_format_level(7, buf, sizeof(buf));
  CHECK(strcmp(buf, "7") == 0);

  lox_format_remaining(0, buf, sizeof(buf));
  CHECK(buf[0] == '\0');
  lox_format_remaining(45, buf, sizeof(buf));
  CHECK(strcmp(buf, "Noch 45 s") == 0);
  lox_format_remaining(28 * 60, buf, sizeof(buf));
  CHECK(strcmp(buf, "Noch 28 min") == 0);
  lox_format_remaining(3600, buf, sizeof(buf));
  CHECK(strcmp(buf, "Noch 1 h") == 0);
  lox_format_remaining(3600 + 5 * 60, buf, sizeof(buf));
  CHECK(strcmp(buf, "Noch 1 h 5 min") == 0);

  // loxone_id_ok is unchanged and still rejects slashes
  CHECK(!loxone_id_ok("startOverride/5/1800", 32));
  CHECK(loxone_id_ok("stopOverride", 16));

  if (fails == 0) printf("loxone_cmd ok\n");
  return fails == 0 ? 0 : 1;
}
