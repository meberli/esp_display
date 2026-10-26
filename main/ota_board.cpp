#include "ota_board.h"

#include <stdio.h>
#include <string.h>

bool ota_board_same(const char* image_project, const char* running_board) {
  if (image_project == nullptr || running_board == nullptr) return false;
  char a[33];
  memcpy(a, image_project, 32);
  a[32] = '\0';
  return running_board[0] != '\0' && strcmp(a, running_board) == 0;
}

OtaBoardResult ota_board_check_image(const uint8_t* img, size_t n, const char* running_board, char* image_board) {
  if (image_board != nullptr) image_board[0] = '\0';
  if (img == nullptr || n < kOtaBoardHeaderBytes) return kOtaBoardShort;
  uint32_t magic = img[32] | img[33] << 8 | img[34] << 16 | static_cast<uint32_t>(img[35]) << 24;
  if (img[0] != 0xE9 || magic != 0xABCD5432) return kOtaBoardNotApp;
  const char* proj = reinterpret_cast<const char*>(img + 80);
  if (image_board != nullptr) {
    size_t k = 0;
    for (; k < 32 && proj[k] != '\0'; ++k) image_board[k] = (proj[k] >= 32 && proj[k] < 127) ? proj[k] : '?';
    image_board[k] = '\0';
  }
  return ota_board_same(proj, running_board) ? kOtaBoardOk : kOtaBoardMismatch;
}

void ota_board_asset(const char* running_board, char* out, size_t n) { snprintf(out, n, "%s.bin", running_board); }
