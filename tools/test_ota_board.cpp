// Host test for the OTA board check:
//   g++ -std=c++17 -I main tools/test_ota_board.cpp main/ota_board.cpp -o t && ./t
#include <cstdio>
#include <cstring>
#include <vector>

#include "ota_board.h"

static std::vector<uint8_t> image(const char* project, const char* version = "0.9.7") {
  std::vector<uint8_t> b(256, 0);
  b[0] = 0xE9;
  b[32] = 0x32; b[33] = 0x54; b[34] = 0xCD; b[35] = 0xAB;
  memcpy(&b[48], version, strlen(version));
  memcpy(&b[80], project, strlen(project) > 32 ? 32 : strlen(project));
  return b;
}

int main() {
  int fails = 0;
  auto expect = [&](bool c, const char* what) { if (!c) { printf("FAIL %s\n", what); ++fails; } };
  char found[33];
  auto g = image("loxone_panel");
  auto d = image("loxone_panel_sensecap_d1");
  expect(ota_board_check_image(g.data(), g.size(), "loxone_panel", found) == kOtaBoardOk, "guition on guition");
  expect(strcmp(found, "loxone_panel") == 0, "found name");
  expect(ota_board_check_image(d.data(), d.size(), "loxone_panel_sensecap_d1", found) == kOtaBoardOk, "d1 on d1");
  expect(ota_board_check_image(d.data(), d.size(), "loxone_panel", found) == kOtaBoardMismatch, "d1 image on guition");
  expect(strcmp(found, "loxone_panel_sensecap_d1") == 0, "found d1 name");
  expect(ota_board_check_image(g.data(), g.size(), "loxone_panel_sensecap_d1", found) == kOtaBoardMismatch,
         "guition image on d1");
  // Prefix must not match: "loxone_panel" vs "loxone_panel_sensecap_d1".
  expect(!ota_board_same("loxone_panel_sensecap_d1", "loxone_panel"), "prefix 1");
  expect(!ota_board_same("loxone_panel\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", "loxone_panel_sensecap_d1"), "prefix 2");
  auto full = image("abcdefghijklmnopqrstuvwxyz012345");  // 32 chars, no NUL
  expect(ota_board_check_image(full.data(), full.size(), "abcdefghijklmnopqrstuvwxyz012345", found) == kOtaBoardOk,
         "32-char name");
  expect(ota_board_check_image(g.data(), 100, "loxone_panel", found) == kOtaBoardShort, "short");
  auto bad = g; bad[0] = 0;
  expect(ota_board_check_image(bad.data(), bad.size(), "loxone_panel", found) == kOtaBoardNotApp, "not app");
  auto bad2 = g; bad2[34] = 0;
  expect(ota_board_check_image(bad2.data(), bad2.size(), "loxone_panel", found) == kOtaBoardNotApp, "bad magic");
  expect(!ota_board_same("", ""), "empty running");
  char asset[48];
  ota_board_asset("loxone_panel", asset, sizeof(asset));
  expect(strcmp(asset, "loxone_panel.bin") == 0, "asset guition");
  ota_board_asset("loxone_panel_sensecap_d1", asset, sizeof(asset));
  expect(strcmp(asset, "loxone_panel_sensecap_d1.bin") == 0, "asset d1");
  printf("%d failures\n", fails);
  return fails;
}
