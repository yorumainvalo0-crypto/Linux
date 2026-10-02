// WLAN, setup hotspot, local update page and GitHub updates - see net.h
#include "net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GITHUB_BASE "https://github.com/" CONFIG_ARCADE_GITHUB_REPO "/releases/latest/download/"
#define AP_IP       "192.168.4.1"         // default address of the ESP hotspot

static volatile NetState state = NET_OFF;
static volatile NetJob   job = JOB_IDLE;
static volatile uint8_t  progress = 0;
static volatile bool     wantConnect = false;   // setup page stored a new network
static volatile uint32_t connectAt = 0;         // ... switch over at this time (ms)
static volatile uint32_t restartAt = 0;         // ms, 0 = no restart planned
static volatile uint8_t  answer = 0;            // upload question: 1 = yes, 2 = no
static portMUX_TYPE      jobLock = portMUX_INITIALIZER_UNLOCKED;

static char ssid[33], pass[65];
static bool cfgLoaded = false;
static char addr[16], apName[20], remoteVer[32], askVer[32], errMsg[48];

// networks found when the setup hotspot opened
#define NETS_MAX 12
static char    nets[NETS_MAX][33];
static volatile uint8_t netsFound = 0;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool baseUp = false, wifiUp = false;
static uint8_t retries = 0;
static httpd_handle_t server = NULL;

static void fail(const char *why) {
  strlcpy(errMsg, why, sizeof(errMsg));
  job = JOB_ERROR;
}

// ---------------- stored network ----------------
static void loadCfg() {
  if (cfgLoaded) return;
  cfgLoaded = true;
  ssid[0] = pass[0] = 0;
  nvs_handle_t h;
  if (nvs_open("net", NVS_READONLY, &h) != ESP_OK) return;
  size_t n = sizeof(ssid);
  if (nvs_get_str(h, "ssid", ssid, &n) != ESP_OK) ssid[0] = 0;
  n = sizeof(pass);
  if (nvs_get_str(h, "pass", pass, &n) != ESP_OK) pass[0] = 0;
  nvs_close(h);
}

static void saveCfg(const char *s, const char *p) {
  strlcpy(ssid, s, sizeof(ssid));
  strlcpy(pass, p, sizeof(pass));
  cfgLoaded = true;
  nvs_handle_t h;
  if (nvs_open("net", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "ssid", ssid);
  nvs_set_str(h, "pass", pass);
  nvs_commit(h);
  nvs_close(h);
}

// ---------------- phone hotspot password ----------------
/* The phone hotspot gets a random 8 digit password the first time; the
   console can roll a new one or switch it off, the phone can set its own. */
static char apPass[65];
static bool apOpen = false, apLoaded = false;

static void apSave() {
  nvs_handle_t h;
  if (nvs_open("net", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "hpw", apPass);
  nvs_set_u8(h, "hop", apOpen ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}

static void apRandom() {
  for (uint8_t i = 0; i < 8; i++) apPass[i] = '0' + esp_random() % 10;
  apPass[8] = 0;
}

static void apLoad() {
  if (apLoaded) return;
  apLoaded = true;
  apPass[0] = 0;
  nvs_handle_t h;
  if (nvs_open("net", NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof(apPass);
    if (nvs_get_str(h, "hpw", apPass, &n) != ESP_OK) apPass[0] = 0;
    uint8_t o = 0;
    if (nvs_get_u8(h, "hop", &o) == ESP_OK) apOpen = o != 0;
    nvs_close(h);
  }
  if (strlen(apPass) < 8) { apRandom(); apSave(); }
}

const char *netApPass() { apLoad(); return apOpen ? "" : apPass; }

bool netApSetPass(const char *p) {
  apLoad();
  size_t n = strlen(p);
  if (n && (n < 8 || n > 63)) return false;             // what WPA2 allows
  for (const char *c = p; *c; c++) if (*c < ' ' || *c > '~') return false;
  if (n) { strlcpy(apPass, p, sizeof(apPass)); apOpen = false; }
  else apOpen = true;
  apSave();                                              // used from the next start of the hotspot
  return true;
}

// the pages that change things only on networks with a password
static bool trusted() { return state == NET_ONLINE || (state == NET_PLAY && !apOpen); }

bool        netHasConfig() { loadCfg(); return ssid[0] != 0; }
const char *netSsid()      { loadCfg(); return ssid; }

void netForget() {
  netStop();
  saveCfg("", "");
}

// ---------------- radio ----------------
static void onEvent(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (state == NET_CONNECTING) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (state != NET_CONNECTING && state != NET_ONLINE) return;   // setup scan etc.
    addr[0] = 0;
    if (retries++ < 5) { state = NET_CONNECTING; esp_wifi_connect(); }
    else state = NET_FAILED;
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    static wifi_ap_record_t rec[NETS_MAX];
    uint16_t n = NETS_MAX;
    if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) n = 0;
    uint8_t k = 0;
    for (uint16_t i = 0; i < n; i++) {
      const char *s = (const char *)rec[i].ssid;
      bool dup = !s[0];
      for (uint8_t j = 0; j < k && !dup; j++) if (!strcmp(s, nets[j])) dup = true;
      if (!dup) strlcpy(nets[k++], s, sizeof(nets[0]));
    }
    netsFound = k;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
    snprintf(addr, sizeof(addr), IPSTR, IP2STR(&e->ip_info.ip));
    retries = 0;
    state = NET_ONLINE;
  }
}

static void radioOn(wifi_mode_t mode) {
  if (!baseUp) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, NULL);
    baseUp = true;
  }
  if (wifiUp) {
    esp_wifi_stop();
  } else {
    wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&c);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);     // the network lives in our own NVS keys
    wifiUp = true;
  }
  esp_wifi_set_mode(mode);
}

// ---------------- captive portal DNS ----------------
/* While the setup hotspot is open every name resolves to the ESP itself,
   so phones notice the "login page" and open the setup page on their own. */
static volatile bool dnsRun = false;
static TaskHandle_t  dnsTaskH = NULL;

static void dnsTask(void *) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  struct timeval tv = { 1, 0 };                  // wake up once a second to see dnsRun
  if (s >= 0) {
    bind(s, (struct sockaddr *)&a, sizeof(a));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }
  static uint8_t b[272];
  while (s >= 0 && dnsRun) {
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(s, b, 256, 0, (struct sockaddr *)&from, &fl);
    if (n < 12 || (b[2] & 0x80) || b[4] || b[5] != 1) continue;   // one question, no answers
    int p = 12;
    while (p < n && b[p]) p += b[p] + 1;          // skip the name labels
    if (p + 5 > n) continue;
    bool isA = b[p + 1] == 0 && b[p + 2] == 1;
    n = p + 5;                                    // end of the question
    b[2] = 0x81; b[3] = 0x80;                     // answer, no error
    b[6] = 0; b[7] = isA ? 1 : 0;
    b[8] = b[9] = b[10] = b[11] = 0;
    if (isA) {
      static const uint8_t ans[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
      memcpy(b + n, ans, sizeof(ans));
      n += sizeof(ans);
    }
    sendto(s, b, n, 0, (struct sockaddr *)&from, fl);
  }
  if (s >= 0) close(s);
  dnsTaskH = NULL;
  vTaskDelete(NULL);
}

static void dnsStart() {
  for (uint8_t i = 0; i < 30 && dnsTaskH; i++) vTaskDelay(pdMS_TO_TICKS(50));  // old one still leaving
  if (dnsTaskH) return;
  dnsRun = true;
  xTaskCreate(dnsTask, "dns", 3072, NULL, 4, &dnsTaskH);
}

static void dnsStop() { dnsRun = false; }

// ---------------- update helpers ----------------
/* The upload must be the app image (miniarcade.bin). The full image starts
   with the bootloader and would not boot from an app slot.               */
static const char *checkImage(const uint8_t *b, size_t n) {
  const size_t at = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
  if (n < at + sizeof(esp_app_desc_t) || b[0] != ESP_IMAGE_HEADER_MAGIC)
    return "not an ESP32 firmware file";
  const esp_image_header_t *h = (const esp_image_header_t *)b;
  if (h->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) return "firmware for another chip";
  esp_app_desc_t d;
  memcpy(&d, b + at, sizeof(d));
  if (d.magic_word != ESP_APP_DESC_MAGIC_WORD) return "full image - use miniarcade.bin";
  if (strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name)))
    return "not a MiniArcade firmware";
  return NULL;
}

static bool busy() {
  return job == JOB_CHECKING || job == JOB_UPDATING || job == JOB_DONE || job == JOB_ASKING;
}

// takes the job slot, so an upload and a GitHub install never meet
static bool claim(NetJob j) {
  bool ok;
  portENTER_CRITICAL(&jobLock);
  ok = !busy();
  if (ok) { job = j; progress = 0; }
  portEXIT_CRITICAL(&jobLock);
  return ok;
}

static void restartSoon() { restartAt = nowMs() + 1500; }

/* true when version a is newer than b: the numbers are compared one by one
   (9.10 > 9.9), and on a tie a final release beats a "-rc" build.       */
static bool verNewer(const char *a, const char *b) {
  while (*a || *b) {
    while (*a && (*a < '0' || *a > '9') && *a != '-') a++;
    while (*b && (*b < '0' || *b > '9') && *b != '-') b++;
    bool ea = !*a || *a == '-', eb = !*b || *b == '-';     // numbers used up?
    if (ea || eb) {
      if (!ea) return true;                                // 9.3.1 > 9.3
      if (!eb) return false;
      return !*a && *b == '-';                             // 9.3 > 9.3-rc1
    }
    unsigned long x = strtoul(a, (char **)&a, 10), y = strtoul(b, (char **)&b, 10);
    if (x != y) return x > y;
  }
  return false;
}

static void clientCfg(esp_http_client_config_t *c, const char *url) {
  memset(c, 0, sizeof(*c));
  c->url = url;
  c->crt_bundle_attach = esp_crt_bundle_attach;
  c->timeout_ms = 15000;
  c->buffer_size = 4096;        // GitHub answers with long headers ...
  c->buffer_size_tx = 2048;     // ... and redirects to a very long signed URL
  c->user_agent = "MiniArcade";
}

// reads version.txt of the latest release into remoteVer
static bool fetchVersion() {
  esp_http_client_config_t cfg;
  clientCfg(&cfg, GITHUB_BASE "version.txt");
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) { fail("out of memory"); return false; }
  bool ok = false;
  int status = 0;
  for (uint8_t hop = 0; hop < 5; hop++) {        // follow the redirect to the file
    if (esp_http_client_open(c, 0) != ESP_OK) { status = -1; fail("no connection to GitHub"); break; }
    if (esp_http_client_fetch_headers(c) < 0) { status = -1; fail("no answer from GitHub"); break; }
    status = esp_http_client_get_status_code(c);
    if (status < 300 || status > 308) break;
    esp_http_client_set_redirection(c);
    int dummy;
    esp_http_client_flush_response(c, &dummy);
    esp_http_client_close(c);
  }
  if (status == 200) {
    char b[sizeof(remoteVer)];
    int n = esp_http_client_read(c, b, sizeof(b) - 1);
    uint8_t k = 0;
    for (int i = 0; i < n && k < sizeof(remoteVer) - 1; i++) {   // keep a plain version string
      char ch = b[i];
      if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          ch == '.' || ch == '-' || ch == '_' || ch == '+')
        remoteVer[k++] = ch;
      else if (ch == '\n' || ch == '\r') break;
    }
    remoteVer[k] = 0;
    ok = k > 0;
    if (!ok) fail("empty version.txt");
  } else if (status == 404) {
    fail("no release on GitHub yet");
  } else if (status > 0) {
    char m[32]; snprintf(m, sizeof(m), "GitHub error %d", status);
    fail(m);
  }
  esp_http_client_cleanup(c);
  return ok;
}

static void installFromGithub() {
  esp_http_client_config_t cfg;
  clientCfg(&cfg, GITHUB_BASE "miniarcade.bin");
  cfg.timeout_ms = 30000;
  esp_https_ota_config_t oc = {};
  oc.http_config = &cfg;
  esp_https_ota_handle_t h = NULL;
  if (esp_https_ota_begin(&oc, &h) != ESP_OK) { fail("download failed"); return; }

  esp_app_desc_t d;
  if (esp_https_ota_get_img_desc(h, &d) != ESP_OK ||
      strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name))) {
    esp_https_ota_abort(h);
    fail("not a MiniArcade firmware");
    return;
  }
  esp_err_t e;
  while ((e = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    int size = esp_https_ota_get_image_size(h);
    if (size > 0) progress = (uint8_t)((int64_t)esp_https_ota_get_image_len_read(h) * 100 / size);
  }
  if (e != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
    esp_https_ota_abort(h);
    fail("download broken off");
    return;
  }
  if (esp_https_ota_finish(h) != ESP_OK) { fail("image is damaged"); return; }  // also sets the boot slot
  progress = 100;
  job = JOB_DONE;
  restartSoon();
}

static void githubTask(void *arg) {
  bool install = arg != NULL;
  if (install) installFromGithub();
  else if (fetchVersion())
    job = verNewer(remoteVer, netVersion()) ? JOB_NEWER : JOB_UPTODATE;
  vTaskDelete(NULL);
}

static void startGithub(bool install) {
  if (state != NET_ONLINE) { if (!busy()) fail("not connected"); return; }
  if (!claim(install ? JOB_UPDATING : JOB_CHECKING)) return;
  // TLS needs a fair amount of stack
  if (xTaskCreate(githubTask, "github", 8192, install ? (void *)1 : NULL, 5, NULL) != pdPASS)
    fail("out of memory");
}

void netCheckUpdate()   { startGithub(false); }
void netInstallUpdate() { startGithub(true); }

// ---------------- AI chat ----------------
/* A single question to OpenRouter (OpenAI style chat completions), without
   the history - every question starts afresh. Only free models are used:
   the ":free" variants of the ranked list below (or the newer list in the
   repository, MiniArcade/ai-models.txt), with OpenRouter's own random free
   router last. On top, provider.max_price = 0 makes OpenRouter refuse a
   request rather than bill it - so a question can never cost money.    */
#define AI_URL      "https://openrouter.ai/api/v1/chat/completions"
#define AI_LIST_URL "https://raw.githubusercontent.com/" CONFIG_ARCADE_GITHUB_REPO "/main/MiniArcade/ai-models.txt"
#define AI_LAST     "openrouter/free"            // picks any free model that is up
#define AI_KEY_MAX  200
#define AI_RESP_MAX 32768                         // whole JSON answer
#define AI_MODELS   8
#define AI_ID_MAX   64

static const char AI_SYSTEM[] =
  "You are the AI chat of MiniArcade, a tiny handheld game console. Its screen shows about "
  "25 characters by 6 lines at a time; the player scrolls through longer answers with the "
  "arrow keys. The question was typed letter by letter with five buttons, so it is short, "
  "often lowercase and may have typos. Answer in the language of the question, in plain "
  "text without Markdown, tables, emoji or code blocks. Keep it short: usually two to five "
  "sentences, at most about 120 words unless the question really needs more.";

// best first; used until the list from the repository has been read
static const char *const AI_BUILTIN[] = {
  "thinkingmachines/inkling:free",
  "nvidia/nemotron-3-ultra-550b-a55b:free",
  "qwen/qwen3.8-27b:free",
  "google/gemma-4-31b-it:free",
  "nvidia/nemotron-3-super-120b-a12b:free",
};

static volatile AiState aiState = AI_IDLE;
static char    aiQuestion[AI_QUESTION_MAX + 1];
static char    aiText[AI_ANSWER_MAX];
static char    aiModel[AI_ID_MAX];                // the model that answered last
static char    aiList[AI_MODELS][AI_ID_MAX];      // the ranking from the repository
static uint8_t aiListN = 0;
static bool    aiListTried = false;               // once per start

// the stored key, "" when there is none
static void aiKeyLoad(char *out, size_t max) {
  out[0] = 0;
  nvs_handle_t h;
  if (nvs_open("net", NVS_READONLY, &h) != ESP_OK) return;
  size_t n = max;
  if (nvs_get_str(h, "ork", out, &n) != ESP_OK) out[0] = 0;
  nvs_close(h);
}

bool netAiHasKey() {
  char k[AI_KEY_MAX + 1];
  aiKeyLoad(k, sizeof(k));
  bool has = k[0] != 0;
  memset(k, 0, sizeof(k));
  return has;
}

bool netAiSetKey(const char *key) {
  while (*key == ' ') key++;
  size_t n = strlen(key);
  while (n && (key[n - 1] == ' ' || key[n - 1] == '\n' || key[n - 1] == '\r')) n--;
  if (n && (n < 20 || n > AI_KEY_MAX)) return false;
  for (size_t i = 0; i < n; i++) {                 // keys are letters, digits, '-' and '_'
    char c = key[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
      return false;
  }
  nvs_handle_t h;
  if (nvs_open("net", NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_erase_key(h, "aik");                        // the Claude key of 10.2 is not used any more
  esp_err_t e;
  if (n) {
    char k[AI_KEY_MAX + 1];
    memcpy(k, key, n);
    k[n] = 0;
    e = nvs_set_str(h, "ork", k);
    memset(k, 0, sizeof(k));
  } else {
    e = nvs_erase_key(h, "ork");
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
  }
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

static void aiFinish(AiState s, const char *text) {
  strlcpy(aiText, text, sizeof(aiText));
  aiState = s;
}

/* The ranking from the repository: one model per line, best first, '#'
   starts a comment. Only ":free" models are taken. Keeps the built-in
   list when the file cannot be read.                                  */
static void aiFetchList() {
  aiListTried = true;
  esp_http_client_config_t cfg;
  clientCfg(&cfg, AI_LIST_URL);
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return;
  static char b[2048];                            // the file, comments included
  int n = 0;
  if (esp_http_client_open(c, 0) == ESP_OK && esp_http_client_fetch_headers(c) >= 0 &&
      esp_http_client_get_status_code(c) == 200) {
    for (int k; n < (int)sizeof(b) - 1 && (k = esp_http_client_read(c, b + n, sizeof(b) - 1 - n)) > 0; ) n += k;
  }
  esp_http_client_cleanup(c);
  b[n] = 0;
  uint8_t got = 0;
  char list[AI_MODELS][AI_ID_MAX];
  char *save = NULL;
  for (char *line = strtok_r(b, "\r\n", &save); line && got < AI_MODELS; line = strtok_r(NULL, "\r\n", &save)) {
    char *h = strchr(line, '#');
    if (h) *h = 0;
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strcspn(line, " \t");
    line[len] = 0;
    if (len < 6 || len >= AI_ID_MAX || strcmp(line + len - 5, ":free")) continue;   // free ones only
    strlcpy(list[got++], line, AI_ID_MAX);
  }
  if (got) { memcpy(aiList, list, sizeof(list)); aiListN = got; }
}

static uint8_t aiCount() { return aiListN ? aiListN : sizeof(AI_BUILTIN) / sizeof(AI_BUILTIN[0]); }
static const char *aiAt(uint8_t i) { return aiListN ? aiList[i] : AI_BUILTIN[i]; }

/* The request body: up to two ranked models and the free router behind
   them - OpenRouter goes down the list when one is busy or down.      */
static char *aiBody(uint8_t first) {
  cJSON *req = cJSON_CreateObject();
  if (!req) return NULL;
  cJSON *models = cJSON_AddArrayToObject(req, "models");
  for (uint8_t i = first; i < first + 2 && i < aiCount(); i++) cJSON_AddItemToArray(models, cJSON_CreateString(aiAt(i)));
  cJSON_AddItemToArray(models, cJSON_CreateString(AI_LAST));
  cJSON_AddNumberToObject(req, "max_tokens", 4096);
  cJSON *rs = cJSON_AddObjectToObject(req, "reasoning");      // think a little, send none of it
  if (rs) { cJSON_AddStringToObject(rs, "effort", "low"); cJSON_AddBoolToObject(rs, "exclude", true); }
  cJSON *pv = cJSON_AddObjectToObject(req, "provider");        // never anything that costs money
  cJSON *mp = pv ? cJSON_AddObjectToObject(pv, "max_price") : NULL;
  if (mp) { cJSON_AddNumberToObject(mp, "prompt", 0); cJSON_AddNumberToObject(mp, "completion", 0);
            cJSON_AddNumberToObject(mp, "request", 0); }
  cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
  const char *role[2] = { "system", "user" }, *text[2] = { AI_SYSTEM, aiQuestion };
  for (uint8_t i = 0; i < 2 && msgs; i++) {
    cJSON *m = cJSON_CreateObject();
    if (!m) break;
    cJSON_AddStringToObject(m, "role", role[i]);
    cJSON_AddStringToObject(m, "content", text[i]);
    cJSON_AddItemToArray(msgs, m);
  }
  char *body = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);
  return body;
}

// the error message of an answer, "" when there is none
static const char *aiErrMsg(const cJSON *root) {
  const cJSON *m = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "message");
  return cJSON_IsString(m) ? m->valuestring : "";
}

// the text of a 200 answer; false when it carries an error instead
static bool aiReadAnswer(const char *json, size_t len) {
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) { aiFinish(AI_ERROR, "The answer could not be read."); return true; }
  const cJSON *choice = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "choices"), 0);
  const cJSON *msg = cJSON_GetObjectItemCaseSensitive(choice, "message");
  const cJSON *text = cJSON_GetObjectItemCaseSensitive(msg, "content");
  const cJSON *fin = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
  const cJSON *model = cJSON_GetObjectItemCaseSensitive(root, "model");
  if (cJSON_IsString(model)) strlcpy(aiModel, model->valuestring, sizeof(aiModel));
  bool ok = true;
  if (cJSON_IsString(text) && text->valuestring[0]) {
    static char out[AI_ANSWER_MAX];
    strlcpy(out, text->valuestring, sizeof(out));
    if (cJSON_IsString(fin) && !strcmp(fin->valuestring, "length") && strlen(out) < sizeof(out) - 5)
      strcat(out, " ...");
    aiFinish(AI_DONE, out);
  } else if (cJSON_GetObjectItemCaseSensitive(root, "error") ||
             cJSON_GetObjectItemCaseSensitive(choice, "error")) {
    ok = false;                                   // the model failed: try the next ones
  } else aiFinish(AI_ERROR, "The model sent no text. Ask again.");
  cJSON_Delete(root);
  return ok;
}

// what went wrong, from the status and OpenRouter's error message
static void aiReadError(int status, const char *json, size_t len) {
  cJSON *root = cJSON_ParseWithLength(json, len);
  const char *msg = root ? aiErrMsg(root) : "";
  const char *what;
  if (status == 401)                       what = "The API key was not accepted. Type it in again.";
  else if (status == 402)                  what = "OpenRouter wants credit for this - no free model took the question.";
  else if (status == 429)                  what = "The free limit is used up for now. Try again later (free models: about 50 questions a day).";
  else if (strstr(msg, "data policy"))     what = "Free models need a setting: openrouter.ai, Settings > Privacy, allow free model training/logging.";
  else if (status >= 500)                  what = "OpenRouter is busy right now. Try again in a minute.";
  else                                     what = "No free model could answer.";
  static char b[AI_ANSWER_MAX];
  snprintf(b, sizeof(b), "%s\n\n(error %d) %s", what, status, msg);
  if (root) cJSON_Delete(root);
  aiFinish(AI_ERROR, b);
}

/* One request with the models from rank "first" on. Returns the status,
   0 when the connection failed; resp gets the body.                  */
static int aiPost(uint8_t first, char **resp, size_t *rn) {
  *resp = NULL;
  *rn = 0;
  char *body = aiBody(first);
  if (!body) return 0;
  esp_http_client_config_t cfg;
  clientCfg(&cfg, AI_URL);
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 120000;                        // free models can be slow
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  int status = 0;
  if (c) {
    char auth[AI_KEY_MAX + 8];
    strcpy(auth, "Bearer ");
    aiKeyLoad(auth + 7, sizeof(auth) - 7);
    esp_http_client_set_header(c, "Authorization", auth);   // the client keeps its own copy
    memset(auth, 0, sizeof(auth));
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "X-Title", "MiniArcade");
    int len = strlen(body);
    int64_t size = -1;
    if (esp_http_client_open(c, len) == ESP_OK && esp_http_client_write(c, body, len) == len)
      size = esp_http_client_fetch_headers(c);
    if (size >= 0) {
      size_t cap = size > 0 && size < AI_RESP_MAX ? (size_t)size + 1 : AI_RESP_MAX;
      char *r = (char *)malloc(cap);                // taken after the TLS handshake
      size_t n = 0;
      if (r) {
        for (;;) {
          if (n >= cap - 1) break;                  // full (or all of it, as announced)
          int k = esp_http_client_read(c, r + n, cap - 1 - n);
          if (k <= 0) break;
          n += k;
        }
        r[n] = 0;
        *resp = r;
        *rn = n;
        status = esp_http_client_get_status_code(c);
      }
    }
    esp_http_client_cleanup(c);
  }
  cJSON_free(body);
  return status;
}

static void aiTask(void *) {
  if (!aiListTried) aiFetchList();
  aiModel[0] = 0;
  // a ranked model that is gone gives 400/404 for the whole request: then the next two
  for (uint8_t first = 0; ; first += 2) {
    char *resp;
    size_t n;
    int status = aiPost(first, &resp, &n);
    bool more = first + 2 < aiCount();
    if (!status) { aiFinish(AI_ERROR, "No connection to OpenRouter. Does this WLAN reach the internet?"); break; }
    if (status == 200) {
      bool done = aiReadAnswer(resp, n);
      free(resp);
      if (done) break;
      if (!more) { aiFinish(AI_ERROR, "No free model could answer. Try again in a minute."); break; }
      continue;
    }
    if ((status == 400 || status == 404) && more && !strstr(resp, "data policy")) { free(resp); continue; }
    aiReadError(status, resp, n);
    free(resp);
    break;
  }
  vTaskDelete(NULL);
}

bool netAiAsk(const char *question) {
  if (aiState == AI_ASKING) return false;
  if (state != NET_ONLINE) { aiFinish(AI_ERROR, "Not connected to the WLAN."); return false; }
  if (!netAiHasKey()) { aiFinish(AI_ERROR, "No API key stored."); return false; }
  strlcpy(aiQuestion, question, sizeof(aiQuestion));
  aiState = AI_ASKING;
  // TLS and the JSON answer need a fair amount of stack
  if (xTaskCreate(aiTask, "ai", 10240, NULL, 5, NULL) != pdPASS) {
    aiFinish(AI_ERROR, "Out of memory.");
    return false;
  }
  return true;
}

AiState     netAiState()  { return aiState; }
const char *netAiAnswer() { return aiText; }
const char *netAiModel()  { return aiModel; }

// ---------------- web page ----------------
static const char PAGE_HEAD[] =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'><title>MiniArcade</title><style>"
  "body{font-family:sans-serif;max-width:440px;margin:0 auto;padding:16px;background:#111;color:#eee}"
  "h1{font-size:24px;margin:8px 0}h2{font-size:17px;margin:28px 0 6px}"
  "input,button{width:100%;box-sizing:border-box;padding:11px;margin:5px 0;font-size:16px;"
  "border-radius:8px;border:1px solid #555;background:#222;color:#eee}"
  "button{background:#1f7a4d;border:0;color:#fff}button:disabled{background:#444}"
  "progress{width:100%;height:16px}.m{color:#9a9a9a;font-size:14px}"
  "</style></head><body><h1>MiniArcade</h1>";

static const char PAGE_UPDATE[] =
  "<h2>Update from GitHub</h2>"
  "<button id=c onclick=gh('check')>check for update</button>"
  "<button id=i onclick=gh('install') hidden>install</button><p id=g class=m></p>"
  "<h2>Update from a file</h2><p class=m>miniarcade.bin from a release or from build/</p>"
  "<input type=file id=f accept=.bin><button onclick=up()>flash</button>"
  "<progress id=p max=100 value=0></progress><p id=u class=m></p>";

static const char PAGE_WIFI[] =
  "<h2>WLAN</h2><form method=post action=/save>"
  "<input name=s list=nets placeholder='network name' maxlength=32 required>"
  "<datalist id=nets>";

static const char PAGE_TAIL[] =
  "</datalist><input name=p type=password placeholder=password maxlength=64>"
  "<button>save and connect</button></form>"
  "<script>"
  "function $(i){return document.getElementById(i)}"
  "var T=['','checking GitHub...','up to date','','installing','done - MiniArcade restarts','error: ','press OK on the MiniArcade to install'];"
  "function show(s){var g=$('g');if(!g)return;var t=T[s.job];"
  "if(s.job==2)t+=' ('+s.ver+')';if(s.job==3)t='online: '+s.remote+'  (this: '+s.ver+')';"
  "if(s.job==4)t+=' '+s.pct+'%';if(s.job==6)t+=s.err;g.textContent=t;"
  "$('i').hidden=s.job!=3;$('i').textContent='install '+s.remote;"
  "if(s.job==1||s.job==4||s.job==7)setTimeout(poll,1000);if(s.job==5)setTimeout(function(){location.reload()},9000)}"
  "function poll(){fetch('/status').then(function(r){return r.json()}).then(show)}"
  "function gh(a){fetch('/gh?do='+a,{method:'POST'}).then(poll)}"
  "function up(){var f=$('f').files[0];if(!f)return;var x=new XMLHttpRequest();"
  "x.open('POST','/update');x.upload.onprogress=function(e){if(e.lengthComputable)$('p').value=e.loaded*100/e.total};"
  "x.onload=function(){$('u').textContent=x.responseText;poll();if(x.status==200)setTimeout(function(){location.reload()},9000)};"
  "x.onerror=function(){$('u').textContent='connection lost'};$('u').textContent='uploading - then confirm with OK on the MiniArcade';"
  "x.send(f);setTimeout(poll,1500)}"
  "if($('g'))poll();"
  "</script></body></html>";

static void chunk(httpd_req_t *r, const char *s) { httpd_resp_send_chunk(r, s, HTTPD_RESP_USE_STRLEN); }

// appends s to the page with the HTML special characters escaped
static void chunkEsc(httpd_req_t *r, const char *s) {
  char b[80];
  size_t k = 0;
  for (; *s; s++) {
    const char *e = NULL;
    if (*s == '&') e = "&amp;"; else if (*s == '<') e = "&lt;";
    else if (*s == '>') e = "&gt;"; else if (*s == '"') e = "&quot;"; else if (*s == '\'') e = "&#39;";
    if (k > sizeof(b) - 8) { b[k] = 0; chunk(r, b); k = 0; }
    if (e) { strcpy(b + k, e); k += strlen(e); } else b[k++] = *s;
  }
  b[k] = 0;
  chunk(r, b);
}

// networks found when the hotspot opened - scanning again per page load
// would take the hotspot off its channel while the phone uses it
static void chunkNetworks(httpd_req_t *r) {
  for (uint8_t i = 0; i < netsFound; i++) {
    chunk(r, "<option value=\"");
    chunkEsc(r, nets[i]);
    chunk(r, "\">");
  }
}

static esp_err_t pageGet(httpd_req_t *r) {
  httpd_resp_set_type(r, "text/html");
  chunk(r, PAGE_HEAD);
  char b[96];
  if (state == NET_SETUP)
    snprintf(b, sizeof(b), "<p class=m>firmware %s &middot; setup hotspot</p>", netVersion());
  else if (state == NET_PLAY)
    snprintf(b, sizeof(b), "<p class=m>firmware %s &middot; phone hotspot</p>", netVersion());
  else
    snprintf(b, sizeof(b), "<p class=m>firmware %s &middot; WLAN ", netVersion());
  chunk(r, b);
  if (state != NET_SETUP && state != NET_PLAY) { chunkEsc(r, netSsid()); chunk(r, "</p>"); }
  if (state == NET_ONLINE) chunk(r, PAGE_UPDATE);
  if (state == NET_ONLINE || state == NET_PLAY) chunk(r, PHONE_LINKS);
  chunk(r, PAGE_WIFI);
  if (state == NET_SETUP) chunkNetworks(r);
  chunk(r, PAGE_TAIL);
  httpd_resp_send_chunk(r, NULL, 0);
  return ESP_OK;
}

static esp_err_t statusGet(httpd_req_t *r) {
  char b[256];
  snprintf(b, sizeof(b), "{\"job\":%u,\"pct\":%u,\"ver\":\"%.31s\",\"remote\":\"%.31s\",\"err\":\"%.47s\"}",
           (unsigned)job, (unsigned)progress, netVersion(), remoteVer, errMsg);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_sendstr(r, b);
}

static esp_err_t githubPost(httpd_req_t *r) {
  char q[24], v[12];
  bool install = httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
                 httpd_query_key_value(q, "do", v, sizeof(v)) == ESP_OK && !strcmp(v, "install");
  startGithub(install);
  return httpd_resp_sendstr(r, "ok");
}

static void urlDecode(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '+') *o++ = ' ';
    else if (*s == '%' && s[1] && s[2]) {
      char h[3] = { s[1], s[2], 0 };
      *o++ = (char)strtol(h, NULL, 16);
      s += 2;
    } else *o++ = *s;
  }
  *o = 0;
}

static esp_err_t savePost(httpd_req_t *r) {
  char body[300], s[100], p[200];
  int n = 0;
  if (r->content_len >= sizeof(body)) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "too long");
  uint8_t stalls = 0;
  while (n < (int)r->content_len) {
    int k = httpd_req_recv(r, body + n, r->content_len - n);
    if (k == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 3) continue;   // do not wait forever
    if (k <= 0) return ESP_FAIL;
    n += k;
  }
  body[n] = 0;
  if (httpd_query_key_value(body, "s", s, sizeof(s)) != ESP_OK) s[0] = 0;
  if (httpd_query_key_value(body, "p", p, sizeof(p)) != ESP_OK) p[0] = 0;
  urlDecode(s);
  urlDecode(p);
  size_t ls = strlen(s), lp = strlen(p);
  if (!ls || ls > 32 || lp > 64 || (lp && lp < 8))
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "network name or password has a wrong length");
  saveCfg(s, p);
  httpd_resp_set_type(r, "text/html");
  chunk(r, PAGE_HEAD);
  chunk(r, "<p>Saved. MiniArcade now connects to <b>");
  chunkEsc(r, s);
  chunk(r, "</b>.</p><p class=m>The display shows the new address of this page.</p></body></html>");
  httpd_resp_send_chunk(r, NULL, 0);
  connectAt = nowMs() + 1500;         // give the page time to reach the phone
  wantConnect = true;                 // switched over from the UI loop
  return ESP_OK;
}

static esp_err_t sendText(httpd_req_t *r, const char *status, const char *text) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "text/plain");
  return httpd_resp_sendstr(r, text);
}

static esp_err_t uploadFail(httpd_req_t *r, char *buf, const char *why) {
  free(buf);
  fail(why);
  return sendText(r, "400 Bad Request", why);
}

// receives the raw file body and writes it into the free app slot
static esp_err_t updatePost(httpd_req_t *r) {
  // open hotspots reach everybody around - no firmware from there
  if (!trusted()) return sendText(r, "403 Forbidden", "updates only over your own WLAN or a hotspot with a password");
  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  size_t total = r->content_len;
  if (!part) return sendText(r, "500 Internal Server Error", "no update slot - flash once by cable");
  if (total < 1024 || total > part->size) return sendText(r, "400 Bad Request", "file size does not fit");
  if (!claim(JOB_UPDATING)) return sendText(r, "409 Conflict", "another update is running");
  char *buf = (char *)malloc(4096);
  if (!buf) return uploadFail(r, buf, "out of memory");

  const size_t head = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
  size_t got = 0;
  uint8_t stalls = 0;
  while (got < head) {                            // first the header, to check the file
    int n = httpd_req_recv(r, buf + got, head - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 5) continue;
    if (n <= 0) return uploadFail(r, buf, "upload broken off");
    got += n;
  }
  const char *why = checkImage((const uint8_t *)buf, got);
  if (why) return uploadFail(r, buf, why);

  // somebody has to press OK on the console itself before anything is written
  {
    esp_app_desc_t d;
    memcpy(&d, buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(d));
    strlcpy(askVer, d.version, sizeof(askVer));
  }
  answer = 0;
  job = JOB_ASKING;
  for (uint16_t t = 0; t < 300 && !answer; t++) vTaskDelay(pdMS_TO_TICKS(100));   // 30 s
  if (answer != 1) return uploadFail(r, buf, "not confirmed on the MiniArcade");
  job = JOB_UPDATING;

  esp_ota_handle_t h;
  if (esp_ota_begin(part, total, &h) != ESP_OK) return uploadFail(r, buf, "cannot erase the update slot");
  esp_err_t e = esp_ota_write(h, buf, got);
  size_t done = got;
  stalls = 0;
  while (e == ESP_OK && done < total) {
    size_t want = total - done;
    int n = httpd_req_recv(r, buf, want < 4096 ? want : 4096);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 5) continue;
    if (n <= 0) { e = ESP_FAIL; break; }
    stalls = 0;
    e = esp_ota_write(h, buf, n);
    done += n;
    progress = (uint8_t)(done * 100 / total);
  }
  if (e != ESP_OK) { esp_ota_abort(h); return uploadFail(r, buf, "upload broken off"); }
  if (esp_ota_end(h) != ESP_OK) return uploadFail(r, buf, "image is damaged");
  if (esp_ota_set_boot_partition(part) != ESP_OK) return uploadFail(r, buf, "cannot switch to the new image");
  free(buf);
  progress = 100;
  job = JOB_DONE;
  sendText(r, "200 OK", "update ok - MiniArcade restarts");
  restartSoon();
  return ESP_OK;
}

// anything else (captive portal checks of phones) lands on the main page
static esp_err_t redirectGet(httpd_req_t *r) {
  httpd_resp_set_status(r, "302 Found");
  httpd_resp_set_hdr(r, "Location", state == NET_SETUP ? "http://" AP_IP "/" : "/");
  return httpd_resp_send(r, NULL, 0);
}

static void startServer() {
  if (server) return;
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.stack_size = 6144;
  c.lru_purge_enable = true;
  c.max_uri_handlers = 24;                        // with the phone pages
  if (httpd_start(&server, &c) != ESP_OK) { server = NULL; return; }
  static const httpd_uri_t uris[] = {
    { "/",       HTTP_GET,  pageGet,     NULL },
    { "/status", HTTP_GET,  statusGet,   NULL },
    { "/gh",     HTTP_POST, githubPost,  NULL },
    { "/save",   HTTP_POST, savePost,    NULL },
    { "/update", HTTP_POST, updatePost,  NULL },
  };
  for (const httpd_uri_t &u : uris) httpd_register_uri_handler(server, &u);
  phoneRegister(server);
  static const httpd_uri_t rest = { "/*", HTTP_GET, redirectGet, NULL };   // last: catches the rest
  httpd_register_uri_handler(server, &rest);
}

static void stopServer() {
  if (server) httpd_stop(server);
  server = NULL;
}

/* Many C3 SuperMini boards have a poorly matched antenna and fail to join
   a network at full power - a lower transmit power helps.               */
static void txPower() {
#if CONFIG_ARCADE_WIFI_TX_POWER > 0
  esp_wifi_set_max_tx_power(CONFIG_ARCADE_WIFI_TX_POWER);
#endif
}

// ---------------- public ----------------
/* Radio for the multiplayer page: station mode on a fixed channel without
   joining a network, so ESP-NOW reaches the other consoles directly.     */
bool netRadioLink(uint8_t channel) {
  if (busy()) return false;
  dnsStop();
  stopServer();
  state = NET_OFF;                                // no reconnects from the event handler
  radioOn(WIFI_MODE_STA);
  if (esp_wifi_start() != ESP_OK) return false;
  txPower();
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  return true;
}

void netConnect() {
  loadCfg();
  if (!ssid[0]) { state = NET_FAILED; return; }
  dnsStop();
  radioOn(WIFI_MODE_STA);
  wifi_config_t wc = {};
  memcpy(wc.sta.ssid, ssid, strlen(ssid));
  memcpy(wc.sta.password, pass, strlen(pass));
  wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  retries = 0;
  addr[0] = 0;
  state = NET_CONNECTING;
  esp_wifi_start();                               // STA_START then joins the network
  txPower();
  startServer();
}

static void apNameFill() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(apName, sizeof(apName), "MiniArcade-%02X%02X", mac[4], mac[5]);
}

/* The phone hotspot: the phone joins the console directly (no router, no
   internet) and gets the phone pages at 192.168.4.1.                    */
void netPlay() {
  apLoad();
  state = NET_PLAY;                               // first, so no reconnect is tried
  dnsStop();
  radioOn(WIFI_MODE_AP);
  apNameFill();
  wifi_config_t ac = {};
  memcpy(ac.ap.ssid, apName, strlen(apName));
  ac.ap.ssid_len = strlen(apName);
  ac.ap.channel = 1;
  ac.ap.max_connection = 4;
  if (apOpen) ac.ap.authmode = WIFI_AUTH_OPEN;
  else {
    ac.ap.authmode = WIFI_AUTH_WPA2_PSK;
    memcpy(ac.ap.password, apPass, strlen(apPass));
  }
  esp_wifi_set_config(WIFI_IF_AP, &ac);
  esp_wifi_start();
  txPower();
  strlcpy(addr, AP_IP, sizeof(addr));
  startServer();
}

void netApNewPass()    { apLoad(); apRandom(); apOpen = false; apSave(); if (state == NET_PLAY) netPlay(); }
void netApToggleOpen() { apLoad(); apOpen = !apOpen;             apSave(); if (state == NET_PLAY) netPlay(); }

void netSetup() {
  state = NET_SETUP;                              // first, so the disconnect is not retried
  dnsStop();
  radioOn(WIFI_MODE_APSTA);                       // STA part only scans for networks
  apNameFill();
  wifi_config_t ac = {};
  memcpy(ac.ap.ssid, apName, strlen(apName));
  ac.ap.ssid_len = strlen(apName);
  ac.ap.channel = 1;
  ac.ap.authmode = WIFI_AUTH_OPEN;
  ac.ap.max_connection = 2;
  esp_wifi_set_config(WIFI_IF_AP, &ac);
  state = NET_SETUP;
  esp_wifi_start();
  txPower();
  netsFound = 0;
  wifi_scan_config_t sc = {};
  esp_wifi_scan_start(&sc, false);                // once, before a phone joins
  strlcpy(addr, AP_IP, sizeof(addr));
  dnsStart();
  startServer();
}

void netStop() {
  if (busy()) return;                             // never cut an update short
  dnsStop();
  stopServer();
  state = NET_OFF;                                // first, so the disconnect is not retried
  if (wifiUp) esp_wifi_stop();                    // radio off; the driver stays set up
  addr[0] = 0;
  job = JOB_IDLE;
}

void netTick() {
  if (wantConnect && (int32_t)(nowMs() - connectAt) >= 0) { wantConnect = false; netConnect(); }
  if (restartAt && (int32_t)(nowMs() - restartAt) >= 0) esp_restart();
  if (server) phoneTick();                        // a phone page waiting for the games
}

void netRestartLater() { restartSoon(); }

const char *netAskVersion() { return askVer; }
void        netAnswer(bool yes) { answer = yes ? 1 : 2; }

void netRestart() { esp_restart(); }

NetState    netState()         { return state; }
const char *netAddress()       { return addr; }
const char *netApName()        { return apName; }
const char *netVersion()       { return esp_app_get_description()->version; }
NetJob      netJob()           { return job; }
const char *netRemoteVersion() { return remoteVer; }
uint8_t     netProgress()      { return progress; }
const char *netError()         { return errMsg; }

// ---------------- firmware slots ----------------
static void slotInfo(FwSlot &s, const esp_partition_t *p, bool check) {
  memset(&s, 0, sizeof(s));
  esp_app_desc_t d;
  if (!p || esp_ota_get_partition_description(p, &d) != ESP_OK) return;
  if (strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name))) return;
  s.present = true;
  strlcpy(s.version, d.version, sizeof(s.version));
  esp_ota_img_states_t st;
  bool known = esp_ota_get_state_partition(p, &st) == ESP_OK;   // not found = flashed by cable
  s.pending  = known && st == ESP_OTA_IMG_PENDING_VERIFY;
  s.bootable = !(known && (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED));
  if (s.bootable && check) {                      // a broken off upload leaves half an image
    esp_partition_pos_t pos = { p->address, p->size };
    esp_image_metadata_t md;
    s.bootable = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) == ESP_OK;
  }
}

void fwSlots(FwSlot s[2]) {
  slotInfo(s[0], esp_ota_get_running_partition(), false);
  slotInfo(s[1], esp_ota_get_next_update_partition(NULL), true);
}

bool fwStartOther() {
  if (busy()) return false;
  // it started and the keys work - keep it choosable instead of "broken"
  if (fwPending()) fwConfirm();
  const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
  if (!p || esp_ota_set_boot_partition(p) != ESP_OK) return false;   // also checks the image
  esp_restart();
  return true;
}

void fwConfirm() { esp_ota_mark_app_valid_cancel_rollback(); }

bool fwPending() {
  esp_ota_img_states_t st;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
         st == ESP_OTA_IMG_PENDING_VERIFY;
}
