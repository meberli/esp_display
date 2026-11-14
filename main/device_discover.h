#pragma once
// Classify unauthenticated HTTP GET bodies as a Loxone Miniserver or a
// whatwatt Go. Pure logic, host-tested. The ESP scan (device_discover_scan.cpp)
// never sends credentials and uses GET only.

#include <stddef.h>
#include <stdint.h>

enum DiscoverKind : uint8_t { kDiscoverLoxone = 0, kDiscoverWhatwatt = 1 };

constexpr int kDiscoverMaxHits = 8;
constexpr uint32_t kDiscoverBudgetMs = 8000;

struct DiscoverHit {
  char host[64];  // "192.168.1.10" or "192.168.1.10:8080" (port 80 omitted)
  char name[48];  // short label for the suggestion row (never a secret)
  uint16_t port;
  DiscoverKind kind;
};

// True if body looks like GET /jdev/cfg/api (or apiKey) from a Miniserver.
// Fills name with a short label when name != nullptr. Never copies API keys.
bool discover_is_loxone_api(const char* body, size_t len, char* name, size_t name_n);

// True if body looks like GET /api/v1/report from a whatwatt Go.
bool discover_is_whatwatt_report(const char* body, size_t len, char* name, size_t name_n);

// True if body looks like GET /api/v1/system (available on every whatwatt,
// including without a Plus licence — /report returns 404 in that case).
bool discover_is_whatwatt_system(const char* body, size_t len, char* name, size_t name_n);

// Report or system body. Prefer this when fingerprinting an HTTP response.
bool discover_is_whatwatt(const char* body, size_t len, char* name, size_t name_n);

// mDNS: hostname "whatwatt-XXXXXX", instance containing "whatwatt", or TXT type=100.
// txt_type / txt_id may be nullptr. Fills a short label when name != nullptr.
bool discover_mdns_is_whatwatt(const char* hostname, const char* instance, const char* txt_type,
                               const char* txt_id, char* name, size_t name_n);

// mDNS: hostname or instance contains "miniserver" or "loxone".
bool discover_mdns_is_loxone(const char* hostname, const char* instance, char* name, size_t name_n);

// "192.168.1.10" or "192.168.1.10:8080" (port 80 omitted).
void discover_format_host(char* out, size_t n, const char* ip, uint16_t port);

// IPv4 in host order. Writes usable host addresses of the subnet (skips self,
// the network address and the broadcast). If the prefix is shorter than /23,
// only the /24 that contains self is enumerated (a /16 cannot be covered
// in the scan budget). Returns the number written, capped at cap.
int discover_hosts(uint32_t ip_host, uint32_t mask_host, uint32_t self_host, uint32_t* out, int cap);

// Append a hit if the same host is not already in the list. Returns the new
// count (unchanged if full or duplicate).
int discover_add_hit(DiscoverHit* hits, int n, int cap, const DiscoverHit& hit);
