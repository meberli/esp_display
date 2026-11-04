// Small text helpers for the settings UI, header-only for host tests (tools/test_ui_text.cpp).
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ui_text {
// True when s has visible content (not only spaces or the middle dot U+00B7 separator).
inline bool has_text(const char* s) {
  if (s == nullptr) return false;
  for (; *s != '\0'; ++s) {
    if (*s == ' ') continue;
    if (static_cast<unsigned char>(s[0]) == 0xC2 && static_cast<unsigned char>(s[1]) == 0xB7) { ++s; continue; }
    return true;
  }
  return false;
}

// Joins the non-empty parts with " · " (empty/blank parts never produce a double separator).
inline void join(char* out, size_t n, const char* const* parts, int count) {
  if (n == 0) return;
  out[0] = '\0';
  size_t len = 0;
  for (int i = 0; i < count; ++i) {
    if (!has_text(parts[i])) continue;
    // Trim blanks and stray separators at both ends of the part ("ssid ·" must not double up).
    const char* a = parts[i];
    const char* e = a + strlen(a);
    for (;;) {
      if (*a == ' ') { ++a; continue; }
      if (static_cast<unsigned char>(a[0]) == 0xC2 && static_cast<unsigned char>(a[1]) == 0xB7) { a += 2; continue; }
      break;
    }
    for (;;) {
      if (e > a && e[-1] == ' ') { --e; continue; }
      if (e - a >= 2 && static_cast<unsigned char>(e[-2]) == 0xC2 && static_cast<unsigned char>(e[-1]) == 0xB7) { e -= 2; continue; }
      break;
    }
    int w = snprintf(out + len, n - len, "%s%.*s", len > 0 ? " \xC2\xB7 " : "", static_cast<int>(e - a), a);
    if (w < 0 || static_cast<size_t>(w) >= n - len) { out[n - 1] = '\0'; return; }
    len += static_cast<size_t>(w);
  }
}

// Display form of an URL: drops the scheme and "www.", and for GitHub/known hosts keeps host/owner/repo.
// "https://github.com/meberli/esp_display/releases/latest/download/x.bin" -> "github.com/meberli/esp_display"
inline void url_short(char* out, size_t n, const char* url) {
  if (n == 0) return;
  out[0] = '\0';
  if (url == nullptr) return;
  const char* p = strstr(url, "://");
  p = p != nullptr ? p + 3 : url;
  if (strncmp(p, "www.", 4) == 0) p += 4;
  // Keep at most host + 2 path segments, without query.
  size_t keep = 0;
  int slashes = 0;
  for (; p[keep] != '\0' && p[keep] != '?' && p[keep] != '#'; ++keep) {
    if (p[keep] == '/' && ++slashes == 3) break;
  }
  while (keep > 0 && p[keep - 1] == '/') --keep;
  bool more = p[keep] != '\0' && strcmp(p + keep, "/") != 0;
  const char* tail = !more ? "" : p[keep] == '/' ? "/\xE2\x80\xA6" : "\xE2\x80\xA6";
  snprintf(out, n, "%.*s%s", static_cast<int>(keep), p, tail);
}
}  // namespace ui_text
