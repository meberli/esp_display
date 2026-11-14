#pragma once
// Classify unauthenticated HTTP GET bodies as a Loxone Miniserver or a
// whatwatt Go. Pure logic, host-tested. The ESP scan (device_discover_scan.cpp)
// never sends credentials and uses GET only.

#include <stddef.h>
#include <stdint.h>

enum DiscoverKind : uint8_t { kDiscoverLoxone = 0, kDiscoverWhatwatt = 1 };

constexpr int kDiscoverMaxHits = 8;
constexpr uint32_t kDiscoverBudgetMs = 5000;

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

// "192.168.1.10" or "192.168.1.10:8080" (port 80 omitted).
void discover_format_host(char* out, size_t n, const char* ip, uint16_t port);

// IPv4 in host order. Writes usable host addresses of the subnet (skips self,
// the network address and the broadcast). If the prefix is shorter than /23,
// only the /24 that contains self is enumerated (the 5 s budget cannot cover
// a /16). Returns the number written, capped at cap.
int discover_hosts(uint32_t ip_host, uint32_t mask_host, uint32_t self_host, uint32_t* out, int cap);

// Append a hit if the same host is not already in the list. Returns the new
// count (unchanged if full or duplicate).
int discover_add_hit(DiscoverHit* hits, int n, int cap, const DiscoverHit& hit);
