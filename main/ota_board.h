#pragma once
// Board check for OTA images. The board ID is the CMake project name, which
// the build embeds in esp_app_desc_t.project_name ("loxone_panel" = Guition,
// "loxone_panel_sensecap_d1" = SenseCAP Indicator D1). Pure functions, host
// tested in tools/test_ota_board.cpp.

#include <stddef.h>
#include <stdint.h>

// Image file layout: esp_image_header_t (24) + first segment header (8) +
// esp_app_desc_t: magic at 32, version[32] at 48, project_name[32] at 80.
constexpr size_t kOtaBoardHeaderBytes = 112;

enum OtaBoardResult { kOtaBoardOk = 0, kOtaBoardShort = 1, kOtaBoardNotApp = 2, kOtaBoardMismatch = 3 };

// Checks the first n bytes of an image file against the running board ID.
// image_board (optional, >= 33 bytes) receives the board ID found.
OtaBoardResult ota_board_check_image(const uint8_t* img, size_t n, const char* running_board, char* image_board);

// Compares a project_name field (as in esp_app_desc_t, may lack a NUL).
bool ota_board_same(const char* image_project, const char* running_board);

// Release asset for a board: "<board>.bin".
void ota_board_asset(const char* running_board, char* out, size_t n);
