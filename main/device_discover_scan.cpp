#include "device_discover_scan.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mdns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

// LAN discovery: unauthenticated HTTP GET only, no credentials, no auto-select.
// 1) mDNS (~2.5 s): _http._tcp, and for Loxone also _loxone._tcp. Official
//    whatwatt Go advertises whatwatt-XXXXXX.local / TXT type=100.
// 2) Connect-only /24 pass (or the /24 of self if the prefix is shorter than
//    /23), then GET only hosts that accepted TCP. For whatwatt, try
//    /api/v1/system first (works without a Plus licence); /api/v1/report is
//    a fallback. Store the IPv4 address, never a .local name.

namespace {

constexpr const char* kTag = "discover";
constexpr int kProbeParallel = 16;
constexpr int kConnectMs = 150;
constexpr int kGetTimeoutMs = 500;
constexpr int kMdnsBudgetMs = 2500;
constexpr int kBodyCap = 6144;
constexpr int kMaxHosts = 256;

bool already_have_ip(const DiscoverHit* hits, int n, const char* ip) {
  if (ip == nullptr || ip[0] == '\0') return false;
  const size_t n_ip = strlen(ip);
  for (int i = 0; i < n; ++i) {
    const char* h = hits[i].host;
    if (strncmp(h, ip, n_ip) == 0 && (h[n_ip] == '\0' || h[n_ip] == ':')) return true;
  }
  return false;
}

int64_t now_us() { return esp_timer_get_time(); }

size_t http_get(const char* host, uint16_t port, const char* path, char* body, size_t cap, int* status) {
  if (status) *status = 0;
  if (body && cap) body[0] = '\0';
  if (host == nullptr || path == nullptr || cap < 2) return 0;
  char url[160];
  snprintf(url, sizeof(url), "http://%s:%u%s", host, static_cast<unsigned>(port), path);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = kGetTimeoutMs;
  cfg.method = HTTP_METHOD_GET;
  // Never send Authorization, never follow into a login form.
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) return 0;
  size_t got = 0;
  esp_err_t err = esp_http_client_open(c, 0);
  if (err == ESP_OK) {
    const int hdr = esp_http_client_fetch_headers(c);
    (void)hdr;
    if (status) *status = esp_http_client_get_status_code(c);
    int r = 0;
    while (got + 1 < cap && (r = esp_http_client_read(c, body + got, cap - 1 - got)) > 0) got += static_cast<size_t>(r);
    if (body) body[got] = '\0';
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return got;
}

struct Probe {
  int fd;
  uint32_t ip;
  uint16_t port;
  int64_t start;
};

int tcp_begin(uint32_t ip_host, uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(ip_host);
  const int rc = connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  if (rc == 0) return fd;
  if (errno == EINPROGRESS) return fd;
  close(fd);
  return -1;
}

// 1 = open, 0 = still waiting, -1 = closed / failed.
int tcp_poll(Probe* p) {
  if (p->fd < 0) return -1;
  fd_set w, e;
  FD_ZERO(&w);
  FD_ZERO(&e);
  FD_SET(p->fd, &w);
  FD_SET(p->fd, &e);
  timeval tv = {0, 0};
  const int sel = select(p->fd + 1, nullptr, &w, &e, &tv);
  if (sel > 0 && (FD_ISSET(p->fd, &w) || FD_ISSET(p->fd, &e))) {
    int err = 0;
    socklen_t n = sizeof(err);
    getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &err, &n);
    close(p->fd);
    p->fd = -1;
    return err == 0 ? 1 : -1;
  }
  if (now_us() - p->start >= static_cast<int64_t>(kConnectMs) * 1000) {
    close(p->fd);
    p->fd = -1;
    return -1;
  }
  return 0;
}

void close_probe(Probe* p) {
  if (p->fd >= 0) close(p->fd);
  p->fd = -1;
}

bool ipv4_from_mdns(const mdns_ip_addr_t* addr, char* ip, size_t n) {
  if (ip == nullptr || n < 8) return false;
  ip[0] = '\0';
  for (const mdns_ip_addr_t* a = addr; a != nullptr; a = a->next) {
    if (a->addr.type != ESP_IP_ADDR_TYPE_V4) continue;
    snprintf(ip, n, IPSTR, IP2STR(&a->addr.u_addr.ip4));
    return ip[0] != '\0' && strcmp(ip, "0.0.0.0") != 0;
  }
  return false;
}

void txt_get(const mdns_result_t* r, const char* key, char* out, size_t n) {
  if (out == nullptr || n == 0) return;
  out[0] = '\0';
  if (r == nullptr || key == nullptr) return;
  for (size_t i = 0; i < r->txt_count; ++i) {
    if (r->txt[i].key != nullptr && strcasecmp(r->txt[i].key, key) == 0 && r->txt[i].value != nullptr) {
      snprintf(out, n, "%s", r->txt[i].value);
      return;
    }
  }
}

void add_hit(DiscoverHit* hits, int* n, DiscoverKind kind, const char* ip, uint16_t port, const char* name) {
  if (ip == nullptr || ip[0] == '\0' || *n >= kDiscoverMaxHits) return;
  DiscoverHit h = {};
  discover_format_host(h.host, sizeof(h.host), ip, port);
  snprintf(h.name, sizeof(h.name), "%s", (name && name[0]) ? name : (kind == kDiscoverWhatwatt ? "whatwatt Go" : "Miniserver"));
  h.port = port;
  h.kind = kind;
  *n = discover_add_hit(hits, *n, kDiscoverMaxHits, h);
}

void consider_mdns_result(DiscoverKind kind, const mdns_result_t* r, DiscoverHit* hits, int* n) {
  if (r == nullptr) return;
  char ip[16] = "";
  if (!ipv4_from_mdns(r->addr, ip, sizeof(ip))) return;
  char typ[16] = "";
  char id[32] = "";
  txt_get(r, "type", typ, sizeof(typ));
  txt_get(r, "id", id, sizeof(id));
  const char* host = r->hostname != nullptr ? r->hostname : "";
  const char* inst = r->instance_name != nullptr ? r->instance_name : "";
  char name[48] = "";
  bool ok = false;
  if (kind == kDiscoverWhatwatt)
    ok = discover_mdns_is_whatwatt(host, inst, typ[0] ? typ : nullptr, id[0] ? id : nullptr, name, sizeof(name));
  else
    ok = discover_mdns_is_loxone(host, inst, name, sizeof(name));
  if (!ok) return;
  uint16_t port = r->port != 0 ? r->port : (kind == kDiscoverLoxone ? static_cast<uint16_t>(80) : static_cast<uint16_t>(80));
  // We only probe HTTP. If mDNS advertised 443, try 80 instead of HTTPS.
  if (port == 443) port = 80;
  add_hit(hits, n, kind, ip, port, name);
  ESP_LOGI(kTag, "mDNS %s -> %s (%s)", host, ip, name);
}

int mdns_collect(DiscoverKind kind, uint32_t budget_ms, DiscoverHit* hits, int n) {
  static bool inited = false;
  static bool ok = false;
  if (!inited) {
    inited = true;
    ok = mdns_init() == ESP_OK;
    if (!ok) ESP_LOGW(kTag, "mdns_init failed, subnet scan only");
  }
  if (!ok || budget_ms == 0) return n;
  const char* services[][2] = {{"_http", "_tcp"}, {"_loxone", "_tcp"}};
  const int nsvc = (kind == kDiscoverLoxone) ? 2 : 1;
  const uint32_t each = budget_ms / static_cast<uint32_t>(nsvc);
  for (int s = 0; s < nsvc; ++s) {
    mdns_result_t* results = nullptr;
    const int64_t t0 = now_us();
    (void)t0;
    const esp_err_t err = mdns_query_ptr(services[s][0], services[s][1], each, 16, &results);
    if (err != ESP_OK || results == nullptr) continue;
    for (const mdns_result_t* r = results; r != nullptr; r = r->next) consider_mdns_result(kind, r, hits, &n);
    mdns_query_results_free(results);
  }
  return n;
}

bool classify_http(DiscoverKind kind, const char* ip, uint16_t port, int64_t deadline, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (now_us() >= deadline) return false;
  char body[kBodyCap];
  int status = 0;
  if (kind == kDiscoverWhatwatt) {
    size_t got = http_get(ip, port, "/api/v1/system", body, sizeof(body), &status);
    if (status == 200 && discover_is_whatwatt(body, got, name, name_n)) return true;
    if (now_us() >= deadline) return false;
    // Fallback: /report needs a Plus licence; still useful when system is down
    // or an older firmware only serves report.
    got = http_get(ip, port, "/api/v1/report", body, sizeof(body), &status);
    return status == 200 && discover_is_whatwatt(body, got, name, name_n);
  }
  size_t got = http_get(ip, port, "/jdev/cfg/api", body, sizeof(body), &status);
  return status == 200 && discover_is_loxone_api(body, got, name, name_n);
}

}  // namespace

int device_discover_scan(DiscoverKind kind, DiscoverHit* hits, int cap, uint32_t budget_ms, uint32_t /*gen*/) {
  if (hits == nullptr || cap <= 0) return 0;
  if (cap > kDiscoverMaxHits) cap = kDiscoverMaxHits;
  memset(hits, 0, sizeof(hits[0]) * static_cast<size_t>(cap));
  if (budget_ms == 0) budget_ms = kDiscoverBudgetMs;
  const int64_t start = now_us();
  const int64_t deadline = start + static_cast<int64_t>(budget_ms) * 1000;

  int n = 0;
  const uint32_t mdns_ms = budget_ms > kMdnsBudgetMs + 1500 ? kMdnsBudgetMs : budget_ms / 3;
  n = mdns_collect(kind, mdns_ms, hits, n);
  ESP_LOGI(kTag, "mDNS hits=%d after %d ms", n, static_cast<int>((now_us() - start) / 1000));

  esp_netif_t* net = nullptr;
  while ((net = esp_netif_next(net)) != nullptr) {
    const char* ifk = esp_netif_get_ifkey(net);
    if (ifk == nullptr || strncmp(ifk, "WIFI_STA", 8) != 0) continue;
    break;
  }
  if (net == nullptr) {
    ESP_LOGW(kTag, "no STA netif, mDNS-only (%d)", n);
    return n > 0 ? n : -1;
  }
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(net, &info) != ESP_OK || info.ip.addr == 0) {
    ESP_LOGW(kTag, "no STA IP, mDNS-only (%d)", n);
    return n > 0 ? n : -1;
  }
  const uint32_t self = ntohl(info.ip.addr);
  const uint32_t mask = ntohl(info.netmask.addr);

  uint32_t hosts[kMaxHosts];
  const int host_n = discover_hosts(self & mask, mask, self, hosts, kMaxHosts);
  const uint16_t ports[2] = {80, 8080};
  const int port_n = (kind == kDiscoverLoxone) ? 2 : 1;

  struct OpenEp {
    uint32_t ip;
    uint16_t port;
  };
  OpenEp open[64];
  int open_n = 0;

  Probe slots[kProbeParallel];
  for (int i = 0; i < kProbeParallel; ++i) slots[i].fd = -1;
  int next = 0;
  const int jobs = host_n * port_n;
  int inflight = 0;
  int done = 0;

  (void)jobs;
  (void)done;

  while (now_us() < deadline && (next < jobs || inflight > 0)) {
    for (int i = 0; i < kProbeParallel && next < jobs && now_us() < deadline; ++i) {
      if (slots[i].fd >= 0) continue;
      // skip already-known IPs
      bool launched = false;
      while (next < jobs && !launched) {
        const int job = next++;
        const int hi = job / port_n;
        const int pi = job % port_n;
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (hosts[hi] >> 24) & 0xFFu, (hosts[hi] >> 16) & 0xFFu,
                 (hosts[hi] >> 8) & 0xFFu, hosts[hi] & 0xFFu);
        if (already_have_ip(hits, n, ip)) continue;
        slots[i].ip = hosts[hi];
        slots[i].port = ports[pi];
        slots[i].start = now_us();
        slots[i].fd = tcp_begin(slots[i].ip, slots[i].port);
        if (slots[i].fd >= 0) {
          inflight++;
          launched = true;
        }
      }
    }
    for (int i = 0; i < kProbeParallel; ++i) {
      if (slots[i].fd < 0) continue;
      const int st = tcp_poll(&slots[i]);
      if (st == 0) continue;
      inflight--;
      done++;
      if (st == 1 && open_n < static_cast<int>(sizeof(open) / sizeof(open[0]))) {
        open[open_n].ip = slots[i].ip;
        open[open_n].port = slots[i].port;
        open_n++;
      }
    }
    if (inflight > 0) usleep(2000);
  }
  for (int i = 0; i < kProbeParallel; ++i) close_probe(&slots[i]);

  for (int i = 0; i < open_n && n < cap && now_us() < deadline; ++i) {
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (open[i].ip >> 24) & 0xFFu, (open[i].ip >> 16) & 0xFFu,
             (open[i].ip >> 8) & 0xFFu, open[i].ip & 0xFFu);
    if (already_have_ip(hits, n, ip)) continue;
    char name[48] = "";
    if (!classify_http(kind, ip, open[i].port, deadline, name, sizeof(name))) continue;
    add_hit(hits, &n, kind, ip, open[i].port, name);
  }

  ESP_LOGI(kTag, "done kind=%d hits=%d open=%d in %d ms", static_cast<int>(kind), n, open_n,
           static_cast<int>((now_us() - start) / 1000));
  return n;
}
