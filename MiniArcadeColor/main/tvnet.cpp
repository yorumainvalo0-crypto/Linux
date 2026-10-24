// MiniArcade Color: the TV's connection to the MiniArcade server (see
// tvnet.h). One task, one esp_http_client kept alive between requests.
#include "tvnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

#define TV_TASK_STACK 7168          // the TLS handshake runs on it

static char base[128], token[64];
static esp_http_client_handle_t cli = NULL;
static volatile bool running = false, alive = false;
static TaskHandle_t task = NULL;

// short requests
static char reqPath[256];
static char reqBody[TV_BODY_MAX];             // "" = none
static bool reqPost = false;
static volatile TvReqState reqSt = TVR_IDLE;
static volatile int reqStatus = 0;
static uint8_t *resp = NULL;
static volatile size_t respLen = 0;

// the stream
static uint8_t *ring = NULL;
static volatile uint32_t wr = 0, rdn = 0;     // bytes written / read in total
static volatile bool streamOn = false, streamWait = false;
static volatile int streamStatus = 0;
static volatile uint16_t broken = 0;
static volatile uint32_t total = 0;
static char act[48];
static volatile uint16_t cShown = 0, cLate = 0, cBuf = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t gen = 0;                      // of the last info record
static int32_t  have = -1;                    // seq of the last complete frame in it

static uint32_t heapLow = 0xFFFFFFFF;
static void heapNote(const char *where) {
  uint32_t f = esp_get_free_heap_size();
  if (f < heapLow) heapLow = f;
  printf("tv: %s - heap free %lu, largest block %u, lowest %lu\n", where, (unsigned long)f,
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned long)esp_get_minimum_free_heap_size());
}

static bool clientUp() {
  if (cli) return true;
  esp_http_client_config_t c;
  memset(&c, 0, sizeof(c));
  c.url = base;
  if (!strncmp(base, "https:", 6)) c.crt_bundle_attach = esp_crt_bundle_attach;
  c.timeout_ms = 20000;               // the server may ask YouTube first
  c.buffer_size = 1536;
  c.buffer_size_tx = 768;
  c.user_agent = "MiniArcadeColor";
  c.keep_alive_enable = true;
  cli = esp_http_client_init(&c);
  return cli != NULL;
}

static void clientDown() {
  if (!cli) return;
  esp_http_client_cleanup(cli);
  cli = NULL;
}

/* esp_http_client sends a request's headers and its body in two writes, and
   lwIP's Nagle holds the second one back until the server has acknowledged
   the first - a delayed ACK on the server's side, 40 ms or more on every
   poll. The client does not hand out its socket, so every TCP socket of
   ours gets TCP_NODELAY (listening ones refuse it, which does no harm). */
static void noDelay() {
  for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; fd++) {
    int t = 0;
    socklen_t l = sizeof(t);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &t, &l) == 0 && t == SOCK_STREAM) {
      int one = 1;
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
  }
}

// sends the request (with a JSON body if one is given) and reads the answer's
// headers; one more try on a fresh connection (the old one may have been closed)
static bool openReq(const char *path, bool post, const char *body = NULL) {
  static char url[400];
  snprintf(url, sizeof(url), "%s%s", base, path);
  for (int k = 0; k < 2; k++) {
    if (!clientUp()) return false;
    esp_http_client_set_url(cli, url);
    esp_http_client_set_method(cli, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    if (token[0]) esp_http_client_set_header(cli, "X-Token", token);
    int bl = body ? (int)strlen(body) : 0;
    if (bl) esp_http_client_set_header(cli, "Content-Type", "application/json");
    else esp_http_client_delete_header(cli, "Content-Type");
    bool up = esp_http_client_open(cli, bl) == ESP_OK;
    if (up && bl) noDelay();                      // before the body goes out
    if (up && (!bl || esp_http_client_write(cli, body, bl) == bl) && esp_http_client_fetch_headers(cli) >= 0) {
      static bool once = false;
      if (!once) { once = true; heapNote("connected"); }
      return true;
    }
    esp_http_client_close(cli);
  }
  return false;
}

static void doReq() {
  int st = 0;
  size_t n = 0;
  if (openReq(reqPath, reqPost, reqBody)) {
    st = esp_http_client_get_status_code(cli);
    for (int k; n < TV_RESP_MAX - 1 && (k = esp_http_client_read(cli, (char *)resp + n, TV_RESP_MAX - 1 - n)) > 0; ) n += k;
    int dummy;
    if (!esp_http_client_is_complete_data_received(cli) && esp_http_client_flush_response(cli, &dummy) != ESP_OK)
      esp_http_client_close(cli);
  }
  resp[n] = 0;
  respLen = n;
  reqStatus = st;
  reqSt = st == 200 ? TVR_DONE : TVR_ERROR;
}

// follows the records as they pass: which gen and frame the console has
static uint8_t th[9];
static uint8_t thn = 0;
static uint32_t tleft = 0;
static void track(const uint8_t *p, size_t n) {
  while (n) {
    if (thn < 4) {
      th[thn++] = *p++; n--;
      if (thn == 4) { tleft = th[0] | th[1] << 8 | th[2] << 16 | (uint32_t)th[3] << 24; if (!tleft) thn = 0; }
      continue;
    }
    if (thn < 9) { th[thn++] = *p++; n--; tleft--; }
    else { size_t k = n < tleft ? n : tleft; p += k; n -= k; tleft -= k; }
    if (tleft) continue;
    if (th[4] == 'F' && thn >= 8) have = th[6] | th[7] << 8;
    else if (th[4] == 'I' && thn >= 9) {
      uint16_t g = (uint16_t)(th[7] | th[8] << 8);
      if (g != gen) { gen = g; have = -1; }
    }
    thn = 0;
  }
}

static void lost() {
  broken = broken + 1;
  streamWait = true;                  // until the decoder has thrown away what it holds
  if (cli) esp_http_client_close(cli);
  thn = 0;
}

static void doSegment() {
  if (!ring) {
    ring = (uint8_t *)malloc(TV_RING);
    wr = 0; rdn = 0;
    thn = 0;
    if (!ring) { streamStatus = -2; streamWait = true; return; }
    heapNote("stream");
  }
  char path[200], a[48];
  portENTER_CRITICAL(&lock);
  strcpy(a, act);
  act[0] = 0;
  uint16_t s = cShown, l = cLate, b = cBuf;
  cShown = 0; cLate = 0;
  portEXIT_CRITICAL(&lock);
  snprintf(path, sizeof(path), "/api/tv/stream?g=%u&have=%ld&shown=%u&late=%u&buf=%u%s%s", gen, (long)have, s, l, b,
           a[0] ? "&" : "", a);
  if (!openReq(path, false)) { streamStatus = 0; lost(); vTaskDelay(pdMS_TO_TICKS(1000)); return; }
  int st = esp_http_client_get_status_code(cli);
  streamStatus = st;
  if (st != 200) {
    int dummy;
    if (esp_http_client_flush_response(cli, &dummy) != ESP_OK) esp_http_client_close(cli);
    if (a[0]) { portENTER_CRITICAL(&lock); if (!act[0]) strcpy(act, a); portEXIT_CRITICAL(&lock); }
    vTaskDelay(pdMS_TO_TICKS(st == 401 ? 3000 : 1000));
    return;
  }
  uint8_t buf[512];
  for (;;) {
    if (streamOn && TV_RING - (wr - rdn) < sizeof(buf)) {    // the decoder is behind: wait (TCP holds the rest)
      if (!running) { lost(); return; }
      vTaskDelay(1);
      continue;
    }
    int n = esp_http_client_read(cli, (char *)buf, sizeof(buf));
    if (n < 0) { lost(); return; }
    if (n == 0) {
      if (esp_http_client_is_complete_data_received(cli)) return;
      lost();
      return;
    }
    if (streamOn && !streamWait) {
      uint32_t at = wr & (TV_RING - 1), k = TV_RING - at < (uint32_t)n ? TV_RING - at : (uint32_t)n;
      memcpy(ring + at, buf, k);
      memcpy(ring, buf + k, n - k);
      wr += n;
    }
    track(buf, n);
    total += n;
  }
}

static void taskFn(void *) {
  resp = (uint8_t *)malloc(TV_RESP_MAX);
  heapNote("start");
  while (running && resp) {
    if (reqSt == TVR_BUSY) { doReq(); continue; }
    if (streamOn && !streamWait) { doSegment(); continue; }
    if (!streamOn && ring) { free(ring); ring = NULL; }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
  }
  clientDown();
  free(ring); ring = NULL;
  free(resp); resp = NULL;
  if (reqSt == TVR_BUSY) reqSt = TVR_ERROR;
  alive = false;
  task = NULL;
  vTaskDelete(NULL);
}

bool tvNetStart(const char *b, const char *tok) {
  for (int i = 0; alive && i < 2500; i++) vTaskDelay(pdMS_TO_TICKS(10));   // the last one still finishing
  if (alive) return false;
  snprintf(base, sizeof(base), "%s", b);
  size_t n = strlen(base);
  while (n && base[n - 1] == '/') base[--n] = 0;
  snprintf(token, sizeof(token), "%s", tok ? tok : "");
  reqSt = TVR_IDLE;
  streamOn = false; streamWait = false;
  gen = 0; have = -1; total = 0;
  running = true; alive = true;
  if (xTaskCreate(taskFn, "tv", TV_TASK_STACK, NULL, 5, &task) != pdPASS) { running = false; alive = false; return false; }
  /* The radio stays awake while the connection is in use: in the default
     power saving it sleeps between the router's beacons, and every answer
     waits there for the next one - 100 to 300 ms on each request, too slow
     for a real-time game over the internet (and for the TV stream). */
  esp_wifi_set_ps(WIFI_PS_NONE);
  return true;
}

void tvNetStop() {
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);           // back to power saving
  running = false;
  streamOn = false;
  if (task) xTaskNotifyGive(task);
}

void tvNetToken(const char *tok) { snprintf(token, sizeof(token), "%s", tok); }

bool tvReq(const char *path, bool post, const char *json) {
  if (!running || reqSt == TVR_BUSY) return false;
  snprintf(reqPath, sizeof(reqPath), "%s", path);
  if (json && strlen(json) >= sizeof(reqBody)) return false;
  snprintf(reqBody, sizeof(reqBody), "%s", json ? json : "");
  reqPost = post || json;
  reqSt = TVR_BUSY;
  if (task) xTaskNotifyGive(task);
  return true;
}
TvReqState tvReqState() { return reqSt; }
int tvReqStatus() { return reqStatus; }
const uint8_t *tvReqBody(size_t *len) { if (len) *len = respLen; return resp; }
void tvReqClear() { if (reqSt != TVR_BUSY) reqSt = TVR_IDLE; }

bool tvStreamStart() {
  if (!running) return false;
  act[0] = 0;
  streamWait = false;
  streamStatus = 200;
  streamOn = true;
  if (task) xTaskNotifyGive(task);
  return true;
}
void tvStreamStop() { streamOn = false; }
void tvStreamAct(const char *a) {
  portENTER_CRITICAL(&lock);
  snprintf(act, sizeof(act), "%s", a);
  portEXIT_CRITICAL(&lock);
}
void tvStreamCount(uint16_t shown, uint16_t late, uint16_t bufMs) {
  portENTER_CRITICAL(&lock);
  cShown += shown; cLate += late; cBuf = bufMs;
  portEXIT_CRITICAL(&lock);
}
size_t tvStreamAvail() { return ring && streamOn ? wr - rdn : 0; }
size_t tvStreamPeek(uint8_t *dst, size_t n) {
  size_t av = tvStreamAvail();
  if (n > av) n = av;
  for (size_t i = 0; i < n; i++) dst[i] = ring[(rdn + i) & (TV_RING - 1)];
  return n;
}
size_t tvStreamRead(uint8_t *dst, size_t n) {
  size_t av = tvStreamAvail();
  if (n > av) n = av;
  uint32_t at = rdn & (TV_RING - 1), k = TV_RING - at < n ? TV_RING - at : n;
  memcpy(dst, ring + at, k);
  memcpy(dst + k, ring, n - k);
  rdn += n;
  return n;
}
int tvStreamStatus() { return streamStatus; }
uint16_t tvStreamBroken() { return broken; }
void tvStreamReset() {
  rdn = wr;
  streamWait = false;
  if (task) xTaskNotifyGive(task);
}
uint32_t tvStreamBytes() { return total; }

// the same 4 characters as LinkCore::codeOf (MiniArcade/main/linkcore.h)
const char *tvConsoleCode() {
  static char code[5];
  if (!code[0]) {
    static const char A[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    for (uint8_t i = 0; i < 4; i++) { code[i] = A[h & 31]; h >>= 5; }
  }
  return code;
}

uint32_t tvFreeHeap(uint32_t *lowest) {
  if (lowest) *lowest = esp_get_minimum_free_heap_size();
  return esp_get_free_heap_size();
}

uint32_t tvLargestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }
