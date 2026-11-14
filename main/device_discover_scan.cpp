#include "device_discover_scan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "net.h"

namespace {

constexpr int kProbeParallel = 8;
constexpr int kConnectMs = 80;
constexpr int kGetTimeoutMs = 400;
constexpr int kBodyCap = 6144;
constexpr int kMaxHosts = 256;

struct Conn {
  int fd;
  uint32_t ip_host;
  uint16_t port;
  int64_t start_us;
};

int64_t now_us() { return esp_timer_get_time(); }

void close_conn(Conn* c) {
  if (c->fd >= 0) close(c->fd);
  c->fd = -1;
}

bool start_connect(Conn* c, uint32_t ip_host, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return false;
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(ip_host);
  int r = connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  if (r != 0 && errno != EINPROGRESS) {
    close(fd);
    return false;
  }
  c->fd = fd;
  c->ip_host = ip_host;
  c->port = port;
  c->start_us = now_us();
  return true;
}

// 1 = connected, 0 = still waiting, -1 = failed / timed out.
int poll_connect(Conn* c, int64_t deadline_us) {
  if (c->fd < 0) return -1;
  int64_t t = now_us();
  if (t - c->start_us > static_cast<int64_t>(kConnectMs) * 1000 || t >= deadline_us) {
    close_conn(c);
    return -1;
  }
  fd_set w;
  FD_ZERO(&w);
  FD_SET(c->fd, &w);
  timeval tv = {0, 0};
  int s = select(c->fd + 1, nullptr, &w, nullptr, &tv);
  if (s <= 0) return 0;
  int err = 0;
  socklen_t len = sizeof(err);
  if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
    close_conn(c);
    return -1;
  }
  close_conn(c);  // connected: close the probe socket, GET goes through http_get
  return 1;
}

void ip_text(uint32_t ip_host, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u", (ip_host >> 24) & 0xffu, (ip_host >> 16) & 0xffu, (ip_host >> 8) & 0xffu,
           ip_host & 0xffu);
}

bool probe_get(DiscoverKind kind, uint32_t ip_host, uint16_t port, DiscoverHit* hit) {
  char ip[16];
  ip_text(ip_host, ip, sizeof(ip));
  char url[96];
  if (kind == kDiscoverLoxone) {
    if (port == 80) snprintf(url, sizeof(url), "http://%s/jdev/cfg/api", ip);
    else snprintf(url, sizeof(url), "http://%s:%u/jdev/cfg/api", ip, static_cast<unsigned>(port));
  } else {
    if (port == 80) snprintf(url, sizeof(url), "http://%s/api/v1/report", ip);
    else snprintf(url, sizeof(url), "http://%s:%u/api/v1/report", ip, static_cast<unsigned>(port));
  }
  static uint8_t body[kBodyCap];
  // No user, no password, no extra headers: discovery never sends credentials.
  HttpGetResult r = http_get(url, nullptr, nullptr, body, sizeof(body), kGetTimeoutMs);
  if (r.status != 200 || r.length == 0) return false;  // parse even if the body was truncated
  char name[48] = "";
  const char* json = reinterpret_cast<const char*>(body);
  bool ok = kind == kDiscoverLoxone ? discover_is_loxone_api(json, r.length, name, sizeof(name))
                                    : discover_is_whatwatt_report(json, r.length, name, sizeof(name));
  if (!ok) return false;
  memset(hit, 0, sizeof(*hit));
  hit->port = port;
  hit->kind = kind;
  discover_format_host(hit->host, sizeof(hit->host), ip, port);
  snprintf(hit->name, sizeof(hit->name), "%s", name[0] != '\0' ? name : (kind == kDiscoverLoxone ? "Miniserver" : "whatwatt Go"));
  return true;
}

}  // namespace

int device_discover_scan(DiscoverKind kind, DiscoverHit* out, int cap, uint32_t budget_ms, uint32_t /*gen*/) {
  if (out == nullptr || cap <= 0) return 0;
  if (cap > kDiscoverMaxHits) cap = kDiscoverMaxHits;
  uint32_t ip = 0, mask = 0;
  if (!wifi_sta_ipv4(&ip, &mask)) return -1;
  uint32_t hosts[kMaxHosts];
  int nh = discover_hosts(ip, mask, ip, hosts, kMaxHosts);
  const uint16_t ports[2] = {80, 8080};
  const int nports = kind == kDiscoverLoxone ? 2 : 1;

  int64_t deadline = now_us() + static_cast<int64_t>(budget_ms ? budget_ms : kDiscoverBudgetMs) * 1000;
  Conn conn[kProbeParallel];
  for (int i = 0; i < kProbeParallel; ++i) conn[i].fd = -1;

  int next = 0;  // index into hosts * nports
  const int total = nh * nports;
  int n = 0;
  while (now_us() < deadline && n < cap && (next < total || true)) {
    bool any = false;
    for (int i = 0; i < kProbeParallel; ++i) {
      if (conn[i].fd >= 0) {
        any = true;
        continue;
      }
      if (next >= total || now_us() >= deadline) continue;
      int idx = next++;
      uint32_t h = hosts[idx / nports];
      uint16_t p = ports[idx % nports];
      if (start_connect(&conn[i], h, p)) any = true;
    }
    if (!any) break;

    // Wait a little for sockets to complete instead of spinning.
    int64_t remain = deadline - now_us();
    if (remain <= 0) break;
    fd_set w;
    FD_ZERO(&w);
    int maxfd = -1;
    for (int i = 0; i < kProbeParallel; ++i) {
      if (conn[i].fd >= 0) {
        FD_SET(conn[i].fd, &w);
        if (conn[i].fd > maxfd) maxfd = conn[i].fd;
      }
    }
    if (maxfd >= 0) {
      int wait_ms = kConnectMs;
      if (remain / 1000 < wait_ms) wait_ms = static_cast<int>(remain / 1000);
      if (wait_ms < 0) wait_ms = 0;
      timeval tv = {wait_ms / 1000, (wait_ms % 1000) * 1000};
      select(maxfd + 1, nullptr, &w, nullptr, &tv);
    }

    for (int i = 0; i < kProbeParallel && n < cap && now_us() < deadline; ++i) {
      if (conn[i].fd < 0) continue;
      int st = poll_connect(&conn[i], deadline);
      if (st == 1) {
        DiscoverHit hit = {};
        if (probe_get(kind, conn[i].ip_host, conn[i].port, &hit)) n = discover_add_hit(out, n, cap, hit);
      }
    }
  }
  for (int i = 0; i < kProbeParallel; ++i) close_conn(&conn[i]);
  printf("Discovery: %s, %d device%s in the subnet (%d hosts)\n", kind == kDiscoverLoxone ? "Loxone" : "whatwatt", n,
         n == 1 ? "" : "s", nh);
  return n;
}
