// ESP-NOW transport for linkcore.h - see link.h
#include "link.h"
#include <string.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "net.h"

#define LINK_CHANNEL 1            // every console meets on this channel
#define FRIENDS_MAX  10

static LinkCore      core;
static QueueHandle_t rxq = NULL;
static bool          open_ = false;
static char          name_[LK_NAME + 1];
static char          friends[FRIENDS_MAX][5];
static uint8_t       nFriends = 0;
static bool          loaded = false;

struct RxItem { uint8_t mac[6]; int8_t rssi; uint8_t len; uint8_t data[32]; };

uint32_t linkNow() { return (uint32_t)(esp_timer_get_time() / 1000); }
LinkCore &linkCore() { return core; }

// ---------------- name and friends in flash ----------------
static void load() {
  if (loaded) return;
  loaded = true;
  strcpy(name_, "PLAYER");
  nFriends = 0;
  nvs_handle_t h;
  if (nvs_open("link", NVS_READONLY, &h) != ESP_OK) return;
  size_t n = sizeof(name_);
  if (nvs_get_str(h, "name", name_, &n) != ESP_OK || !name_[0]) strcpy(name_, "PLAYER");
  n = sizeof(friends);
  if (nvs_get_blob(h, "fr", friends, &n) == ESP_OK) nFriends = n / sizeof(friends[0]);
  nvs_close(h);
}

static void save() {
  nvs_handle_t h;
  if (nvs_open("link", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "name", name_);
  nvs_set_blob(h, "fr", friends, nFriends * sizeof(friends[0]));
  nvs_commit(h);
  nvs_close(h);
}

const char *linkName() { load(); return name_; }

void linkSetName(const char *n) {
  load();
  memset(name_, 0, sizeof(name_));
  strncpy(name_, n, LK_NAME);
  core.setName(name_);
  save();
}

bool linkIsFriend(const char *code) {
  load();
  for (uint8_t i = 0; i < nFriends; i++) if (!strncmp(friends[i], code, 4)) return true;
  return false;
}

void linkToggleFriend(const char *code) {
  load();
  for (uint8_t i = 0; i < nFriends; i++)
    if (!strncmp(friends[i], code, 4)) {                  // remove
      memmove(friends[i], friends[i + 1], (nFriends - i - 1) * sizeof(friends[0]));
      nFriends--;
      save();
      return;
    }
  if (nFriends == FRIENDS_MAX) {                          // full: the oldest goes
    memmove(friends[0], friends[1], (FRIENDS_MAX - 1) * sizeof(friends[0]));
    nFriends--;
  }
  strncpy(friends[nFriends], code, 4);
  friends[nFriends][4] = 0;
  nFriends++;
  save();
}

uint8_t linkList(uint8_t *idx, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < core.peerCount() && n < max; i++) idx[n++] = i;
  for (uint8_t a = 1; a < n; a++)                         // insertion sort, lists are short
    for (uint8_t b = a; b > 0; b--) {
      const LkPeer &p = core.peer(idx[b - 1]), &q = core.peer(idx[b]);
      bool fp = linkIsFriend(p.code), fq = linkIsFriend(q.code);
      bool swap = (fq && !fp) || (fq == fp && q.rssi > p.rssi);
      if (!swap) break;
      uint8_t t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t;
    }
  return n;
}

// ---------------- radio ----------------
// runs in the Wi-Fi task: only hand the packet over
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!rxq || len <= 0 || len > 32) return;
  RxItem it;
  memcpy(it.mac, info->src_addr, 6);
  it.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : -90;
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  xQueueSend(rxq, &it, 0);                                // full: drop, the protocol repeats
}

static void send(void *, const uint8_t *mac, const uint8_t *data, uint8_t len) {
  static const uint8_t ALL[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  const uint8_t *to = mac ? mac : ALL;
  if (!esp_now_is_peer_exist(to)) {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, to, 6);
    p.channel = 0;                                        // the current one
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    if (esp_now_add_peer(&p) != ESP_OK) return;
  }
  esp_now_send(to, data, len);
}

bool linkOpen() {
  if (open_) return true;
  load();
  if (!rxq) rxq = xQueueCreate(16, sizeof(RxItem));
  if (!rxq || !netRadioLink(LINK_CHANNEL)) return false;
  if (esp_now_init() != ESP_OK) { netStop(); return false; }
  esp_now_register_recv_cb(onRecv);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  core.begin(mac, name_, send, NULL, linkNow(), esp_random());
  xQueueReset(rxq);
  open_ = true;
  return true;
}

void linkClose() {
  if (!open_) return;
  core.cancel();                                          // says goodbye if needed
  vTaskDelay(pdMS_TO_TICKS(30));                          // let the last packets out
  esp_now_deinit();
  netStop();
  open_ = false;
}

void linkTick() {
  if (!open_) return;
  RxItem it;
  uint32_t now = linkNow();
  while (xQueueReceive(rxq, &it, 0) == pdTRUE) core.onPacket(it.mac, it.data, it.len, it.rssi, now);
  core.tick(now);
}
