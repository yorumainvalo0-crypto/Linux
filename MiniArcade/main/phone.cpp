// Pages for a phone in the same WLAN as the console - see net.h
//
//   /stats     high scores, play times, awards        (GET /api/stats)
//   /settings  name, brightness, sleep, sound, clock, hotspot password, AI key
//                                                     (GET /api/settings, POST /api/set)
//   /screen    live picture of the display            (GET /api/screen)
//   /pad       the screen plus big keys to play with  (POST /api/key)
//   /level     Sokoban level editor, 3 own slots      (GET/POST /api/level)
//   /backup    all saves as a text file and back      (GET /api/backup, POST /api/restore)
//
// Only served in the own WLAN, never on the open setup hotspot. The web
// server has a task of its own; whatever touches the game goes through the
// UI loop (phoneTick from netTick), everything else is done right here.
#include "net.h"
#include "backupfmt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// ---------------- the UI loop answers ----------------
enum { UI_STATS = 1, UI_SETTINGS, UI_SET, UI_SCREEN, UI_LEVEL, UI_LEVELGET };
enum { UIS_IDLE, UIS_WAIT, UIS_RUN, UIS_DONE };

static volatile uint8_t uiState = UIS_IDLE;
static uint8_t          uiKind, uiSlot;
static char             uiOut[6144];              // answer (JSON, screen, level text)
static int              uiLen;
static const char      *uiA, *uiB, *uiErr;        // arguments / error of UI_SET, UI_LEVEL
static portMUX_TYPE     uiMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t uiDone = NULL;

/* Web task: hands a job to the UI loop and waits. The web server runs one
   request at a time, so there is never more than one job. false = the UI
   loop did not come by (the console is not serving the phone right now). */
static bool askUi(uint8_t kind) {
  xSemaphoreTake(uiDone, 0);
  uiKind = kind;
  uiState = UIS_WAIT;
  if (xSemaphoreTake(uiDone, pdMS_TO_TICKS(1500)) == pdTRUE) { uiState = UIS_IDLE; return true; }
  taskENTER_CRITICAL(&uiMux);
  bool running = uiState == UIS_RUN;
  if (!running) uiState = UIS_IDLE;               // never started: take it back
  taskEXIT_CRITICAL(&uiMux);
  if (!running) return false;
  xSemaphoreTake(uiDone, portMAX_DELAY);          // started: it is nearly done
  uiState = UIS_IDLE;
  return true;
}

void phoneTick() {
  taskENTER_CRITICAL(&uiMux);
  bool go = uiState == UIS_WAIT;
  if (go) uiState = UIS_RUN;
  taskEXIT_CRITICAL(&uiMux);
  if (!go) return;
  uiLen = 0; uiErr = NULL;
  switch (uiKind) {
  case UI_STATS:    uiLen = appStats(uiOut, sizeof(uiOut)); break;
  case UI_SETTINGS: uiLen = appSettings(uiOut, sizeof(uiOut)); break;
  case UI_SET:      uiErr = appSet(uiA, uiB); break;
  case UI_SCREEN:   appScreen((uint8_t *)uiOut); uiLen = 1024; break;
  case UI_LEVEL:    uiErr = appLevel(uiSlot, uiA); break;
  case UI_LEVELGET: uiLen = appLevelGet(uiSlot, uiOut, sizeof(uiOut)); break;
  }
  uiState = UIS_DONE;
  xSemaphoreGive(uiDone);
}

// ---------------- helpers ----------------
static esp_err_t text(httpd_req_t *r, const char *status, const char *t) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "text/plain");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_sendstr(r, t);
}

// the phone pages exist in the own WLAN and on the phone hotspot, never on
// the setup hotspot
static bool allowed(httpd_req_t *r) {
  if (netState() == NET_ONLINE || netState() == NET_PLAY) return true;
  text(r, "403 Forbidden", "only in your own WLAN or on the phone hotspot");
  return false;
}

static esp_err_t busy(httpd_req_t *r) {
  return text(r, "503 Service Unavailable",
              "the console does not answer - open its WLAN page or set \"stay online: yes\" there");
}

static int queryInt(httpd_req_t *r, const char *key, int def) {
  char q[64], v[16];
  if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK) return def;
  if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return def;
  return atoi(v);
}

static void urlDecode(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '+') *o++ = ' ';
    else if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; *o++ = (char)strtol(h, NULL, 16); s += 2; }
    else *o++ = *s;
  }
  *o = 0;
}

// reads the whole body (up to max-1 bytes) into buf; -1 = broken or too big
static int readBody(httpd_req_t *r, char *buf, int max) {
  if ((int)r->content_len >= max) return -1;
  int n = 0;
  uint8_t stalls = 0;
  while (n < (int)r->content_len) {
    int k = httpd_req_recv(r, buf + n, r->content_len - n);
    if (k == HTTPD_SOCK_ERR_TIMEOUT && ++stalls < 5) continue;
    if (k <= 0) return -1;
    n += k;
  }
  buf[n] = 0;
  return n;
}

// ---------------- pages ----------------
const char PHONE_LINKS[] =
  "<h2>On this phone</h2><p>"
  "<a href=/stats>Stats and awards</a><br><a href=/settings>Settings</a><br>"
  "<a href=/screen>Screen</a><br><a href=/pad>Controller</a><br>"
  "<a href=/level>Sokoban editor</a><br><a href=/backup>Backup</a></p>"
  "<p class=m>Screen and controller while playing: on the console's WLAN page set "
  "\"stay online: yes\", then leave the page.</p>";

static const char P_HEAD[] =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1,user-scalable=no'><title>MiniArcade</title><style>"
  "body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:12px;background:#111;color:#eee}"
  "a{color:#6cf}h1{font-size:22px;margin:14px 0 6px}label{display:block;margin:10px 0}"
  "table{width:100%;border-collapse:collapse;font-size:14px}td{padding:5px 2px;border-bottom:1px solid #333}"
  "td.r{text-align:right}input,select,button{box-sizing:border-box;padding:10px;margin:4px 0;font-size:16px;"
  "border-radius:8px;border:1px solid #555;background:#222;color:#eee;width:100%}"
  "button,.b{background:#1f7a4d;border:0;color:#fff}.b{display:block;text-align:center;padding:11px;"
  "border-radius:8px;text-decoration:none}.m{color:#9a9a9a;font-size:14px}"
  "canvas{width:100%;image-rendering:pixelated;background:#000;border-radius:6px}"
  "</style></head><body><a href=/>&larr; MiniArcade</a>"
  "<script>function $(i){return document.getElementById(i)}</script>";

static const char P_TAIL[] = "</body></html>";

static const char P_STATS[] =
  "<h1>Stats</h1><p id=t class=m>loading...</p><table id=g></table>"
  "<h1>Awards <span id=n></span></h1><table id=a></table><script>"
  "function tm(s){return s>=3600?Math.floor(s/3600)+'h '+Math.floor(s/60)%60+'m':Math.floor(s/60)+'m'}"
  "function row(t,c,right){var r=t.insertRow();c.forEach(function(x,i){var d=r.insertCell();d.textContent=x;"
  "if(i&&right)d.className='r'});return r}"
  "fetch('/api/stats').then(function(r){return r.json()}).then(function(s){"
  "row($('g'),['game','best','played','time'],1).style.color='#9a9a9a';"
  "s.games.forEach(function(g){row($('g'),[g.n,g.b,g.p+'x',tm(g.s)],1)});var w=0;"
  "s.awards.forEach(function(a){if(a.w)w++;var r=row($('a'),[(a.w?'\\u2605 ':'\\u2606 ')+a.n,a.h]);"
  "if(!a.w)r.style.color='#777'});$('n').textContent=w+'/'+s.awards.length;"
  "$('t').textContent='played in all '+tm(s.total)+' \\u00b7 online '+s.online[0]+' games, '+s.online[1]+' won'"
  "}).catch(function(){$('t').textContent='the console did not answer'})</script>";

static const char P_SETTINGS[] =
  "<h1>Settings</h1>"
  "<label>Player name (multiplayer)<input id=nm maxlength=6 autocapitalize=characters></label>"
  "<label>Brightness <span id=bv></span>%<input id=br type=range min=1 max=100></label>"
  "<label>Sleep after minutes without a key (0 = never)<input id=sl type=number min=0 max=60></label>"
  "<label><input id=so type=checkbox style=width:auto> Sound</label>"
  "<label>CPU clock<select id=ck><option value=160>160 MHz<option value=80>80 MHz (the battery lasts longer)</select></label>"
  "<p class=m>The clock is used from the next start on.</p>"
  "<h1>Phone hotspot</h1><label>Password (8 or more characters, empty = no password)"
  "<input id=hp maxlength=63 autocomplete=off></label><button onclick=\"set('hpw',$('hp').value)\">save the password</button>"
  "<p class=m>Used from the next start of the hotspot (console: WLAN page, \"phone hotspot\"). "
  "There, LEFT makes a new random password and RIGHT switches the password off and on.</p>"
  "<h1>AI chat</h1><p class=m id=ai></p><label>Claude API key (from console.anthropic.com)"
  "<input id=ak type=password autocomplete=off></label>"
  "<button onclick=\"set('aikey',$('ak').value);$('ak').value='';setTimeout(hs,800)\">save the key</button>"
  "<button onclick=\"set('aikey','');setTimeout(hs,800)\">forget the key</button>"
  "<p class=m>The key stays on the console: it is never shown again and not part of a backup. "
  "Questions are typed on the console (library: AI Chat) and need a WLAN with internet.</p>"
  "<p id=msg class=m></p><script>"
  "function hs(){fetch('/api/hotspot').then(function(r){return r.json()}).then(function(h){$('hp').value=h.pw;"
  "$('ai').textContent=h.ai?'A key is stored.':'No key stored yet.'})}hs();"
  "function set(k,v){fetch('/api/set?k='+k+'&v='+encodeURIComponent(v),{method:'POST'})"
  ".then(function(r){return r.text()}).then(function(t){$('msg').textContent=t})}"
  "fetch('/api/settings').then(function(r){return r.json()}).then(function(s){$('nm').value=s.name;"
  "$('br').value=s.bright;$('bv').textContent=s.bright;$('sl').value=s.sleep;$('so').checked=!!s.sound;"
  "$('so').disabled=!s.buzzer;$('ck').value=s.clock}).catch(function(){$('msg').textContent='the console did not answer'});"
  "$('nm').onchange=function(){set('name',this.value)};$('br').oninput=function(){$('bv').textContent=this.value};"
  "$('br').onchange=function(){set('bright',this.value)};$('sl').onchange=function(){set('sleep',this.value)};"
  "$('so').onchange=function(){set('sound',this.checked?1:0)};$('ck').onchange=function(){set('clock',this.value)}"
  "</script>";

// the display: 1024 bytes in SSD1306 page order, yellow band on top, blue below
static const char P_SCREEN_JS[] =
  "<script>function draw(c,b){var x=c.getContext('2d'),im=x.createImageData(128,64),d=im.data;"
  "for(var y=0;y<64;y++)for(var X=0;X<128;X++){var i=(y*128+X)*4;"
  "if(b[(y>>3)*128+X]>>(y&7)&1){if(y<16){d[i]=255;d[i+1]=214;d[i+2]=60}else{d[i]=110;d[i+1]=205;d[i+2]=255}}"
  "else{d[i]=d[i+1]=d[i+2]=8}d[i+3]=255}x.putImageData(im,0,0)}"
  "function loop(){fetch('/api/screen',{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.arrayBuffer()})"
  ".then(function(a){draw($('cv'),new Uint8Array(a));$('st').textContent='';setTimeout(loop,120)})"
  ".catch(function(){$('st').textContent='no picture - is the console serving the phone?';setTimeout(loop,1500)})}"
  "loop()</script>";

static const char P_SCREEN[] =
  "<h1>Screen</h1><canvas id=cv width=128 height=64></canvas><p id=st class=m></p>"
  "<button onclick=shot()>save a picture</button><script>"
  "function shot(){var c=document.createElement('canvas');c.width=512;c.height=256;var g=c.getContext('2d');"
  "g.imageSmoothingEnabled=false;g.drawImage($('cv'),0,0,512,256);var a=document.createElement('a');"
  "a.download='miniarcade-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.png';"
  "a.href=c.toDataURL();a.click()}</script>";

static const char P_PAD[] =
  "<canvas id=cv width=128 height=64></canvas><p id=st class=m></p>"
  "<div style='display:grid;grid-template-columns:repeat(3,1fr);gap:10px;touch-action:none;user-select:none;"
  "-webkit-user-select:none'>"
  "<span></span><button data-k=0>&#9650;</button><span></span>"
  "<button data-k=2>&#9664;</button><button data-k=4>OK</button><button data-k=3>&#9654;</button>"
  "<span></span><button data-k=1>&#9660;</button><span></span></div>"
  "<p class=m>Hold OK = pause, like on the console. Arrow keys and Enter work on a computer.</p>"
  "<style>[data-k]{height:72px;font-size:26px}</style><script>"
  "var held={};function key(k,d){fetch('/api/key?k='+k+'&d='+d,{method:'POST'}).catch(function(){})}"
  "function down(k,b){if(held[k])return;key(k,1);held[k]=setInterval(function(){key(k,1)},250);"
  "if(b)b.style.background='#2fa66a'}"
  "function up(k,b){if(!held[k])return;clearInterval(held[k]);held[k]=0;key(k,0);if(b)b.style.background=''}"
  "document.querySelectorAll('[data-k]').forEach(function(b){var k=b.dataset.k;"
  "b.onpointerdown=function(e){e.preventDefault();b.setPointerCapture(e.pointerId);down(k,b)};"
  "b.onpointerup=b.onpointercancel=b.onlostpointercapture=function(){up(k,b)};"
  "b.oncontextmenu=function(e){e.preventDefault()}});"
  "var KB={ArrowUp:0,ArrowDown:1,ArrowLeft:2,ArrowRight:3,Enter:4};"
  "onkeydown=function(e){if(e.key in KB&&!e.repeat){e.preventDefault();down(KB[e.key])}};"
  "onkeyup=function(e){if(e.key in KB)up(KB[e.key])}</script>";

static const char P_LEVEL[] =
  "<h1>Sokoban editor</h1>"
  "<label>Slot<select id=sl><option value=0>own 1<option value=1>own 2<option value=2>own 3</select></label>"
  "<div id=tl style='display:grid;grid-template-columns:repeat(5,1fr);gap:6px'></div>"
  "<div id=gr style='display:grid;grid-template-columns:repeat(14,1fr);gap:2px;margin:10px 0;touch-action:none'></div>"
  "<button onclick=save()>save to the console</button>"
  "<div style='display:grid;grid-template-columns:1fr 1fr;gap:6px'>"
  "<button onclick=clr() style=background:#555>start again</button>"
  "<button onclick=del() style=background:#833>empty the slot</button></div>"
  "<p id=msg class=m></p><p class=m>Pick a tool, then tap or drag over the squares. One player, "
  "as many goals as boxes, walls all round. On the console: Sokoban, then RIGHT past the last level.</p>"
  "<style>#gr div{aspect-ratio:1;border-radius:3px;display:flex;align-items:center;justify-content:center;"
  "font-size:15px}#tl button{padding:8px 0;font-size:15px}</style><script>"
  "var W=14,H=8,g=[],tool='#',T=[['#','wall'],[' ','floor'],['.','goal'],['$','box'],['@','player']];"
  "var C={'#':['#666',''],' ':['#222',''],'.':['#222','\\u25cb'],'$':['#8a5a1c','\\u25a0'],"
  "'*':['#1f7a4d','\\u25a0'],'@':['#222','\\u263a'],'+':['#222','\\u263b']};"
  "function show(){for(var i=0;i<W*H;i++){var d=$('gr').children[i],c=C[g[i]];d.style.background=c[0];d.textContent=c[1]}}"
  "function put(i){var c=g[i],goal=c=='.'||c=='*'||c=='+';"
  "if(tool=='#')g[i]='#';else if(tool==' ')g[i]=' ';else if(tool=='.')g[i]=c=='$'?'*':c=='@'?'+':'.';"
  "else if(tool=='$')g[i]=goal?'*':'$';else{for(var k=0;k<W*H;k++){if(g[k]=='@')g[k]=' ';if(g[k]=='+')g[k]='.'}"
  "g[i]=goal?'+':'@'}show()}"
  "function clr(){for(var y=0;y<H;y++)for(var x=0;x<W;x++)g[y*W+x]=(x&&y&&x<W-1&&y<H-1)?' ':'#';show()}"
  "function text(){var r=[];for(var y=0;y<H;y++){var s='';for(var x=0;x<W;x++)s+=g[y*W+x];r.push(s.replace(/ +$/,''))}"
  "while(r.length&&!r[r.length-1])r.pop();return r.join('|')}"
  "function send(t){$('msg').textContent='saving...';fetch('/api/level?s='+$('sl').value,{method:'POST',body:t})"
  ".then(function(r){return r.text()}).then(function(t){$('msg').textContent=t})}"
  "function save(){send(text())}function del(){send('');clr()}"
  "function load(){fetch('/api/level?s='+$('sl').value).then(function(r){return r.text()}).then(function(t){clr();"
  "if(!t)return;for(var y=0;y<H;y++)for(var x=0;x<W;x++)g[y*W+x]=' ';"
  "t.split('|').forEach(function(s,y){for(var x=0;x<s.length&&x<W&&y<H;x++)g[y*W+x]=s[x]});show()})}"
  "T.forEach(function(t){var b=document.createElement('button');b.textContent=t[1];b.onclick=function(){tool=t[0];"
  "[].forEach.call($('tl').children,function(o){o.style.outline=''});b.style.outline='3px solid #6cf'};$('tl').appendChild(b)});"
  "$('tl').children[0].style.outline='3px solid #6cf';"
  "for(var i=0;i<W*H;i++){var d=document.createElement('div');d.dataset.i=i;$('gr').appendChild(d)}"
  "var paint=0;$('gr').onpointerdown=function(e){paint=1;var i=e.target.dataset.i;if(i)put(+i)};"
  "onpointerup=function(){paint=0};$('gr').onpointermove=function(e){if(!paint)return;"
  "var t=document.elementFromPoint(e.clientX,e.clientY);if(t&&t.dataset.i&&g[t.dataset.i]!=tool)put(+t.dataset.i)};"
  "$('sl').onchange=load;clr();load()</script>";

static const char P_BACKUP[] =
  "<h1>Backup</h1><a class=b href=/api/backup download=miniarcade-backup.txt>download the backup</a>"
  "<p class=m>High scores, stats and awards, the Mine world, own Sokoban levels, name, friends and "
  "settings - not the WLAN password.</p>"
  "<h1>Restore</h1><input type=file id=f accept=.txt><button onclick=up()>restore</button>"
  "<p id=msg class=m></p><p class=m>Everything on the console is replaced by the file. Confirm with OK "
  "on the console's WLAN page, then it restarts.</p><script>"
  "function up(){var f=$('f').files[0];if(!f)return;$('msg').textContent='now press OK on the console (WLAN page)...';"
  "fetch('/api/restore',{method:'POST',body:f}).then(function(r){return r.text()}).then(function(t){$('msg').textContent=t})"
  ".catch(function(){$('msg').textContent='connection lost'})}</script>";

static esp_err_t page(httpd_req_t *r, const char *body, const char *extra = NULL) {
  if (!allowed(r)) return ESP_OK;
  httpd_resp_set_type(r, "text/html");
  httpd_resp_send_chunk(r, P_HEAD, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(r, body, HTTPD_RESP_USE_STRLEN);
  if (extra) httpd_resp_send_chunk(r, extra, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(r, P_TAIL, HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t statsPage(httpd_req_t *r)    { return page(r, P_STATS); }
static esp_err_t settingsPage(httpd_req_t *r) { return page(r, P_SETTINGS); }
static esp_err_t screenPage(httpd_req_t *r)   { return page(r, P_SCREEN, P_SCREEN_JS); }
static esp_err_t padPage(httpd_req_t *r)      { return page(r, P_PAD, P_SCREEN_JS); }
static esp_err_t levelPage(httpd_req_t *r)    { return page(r, P_LEVEL); }
static esp_err_t backupPage(httpd_req_t *r)   { return page(r, P_BACKUP); }

// ---------------- API ----------------
static esp_err_t json(httpd_req_t *r, uint8_t kind) {
  if (!allowed(r)) return ESP_OK;
  if (!askUi(kind)) return busy(r);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, uiOut, uiLen);
}
static esp_err_t statsGet(httpd_req_t *r)    { return json(r, UI_STATS); }
static esp_err_t settingsGet(httpd_req_t *r) { return json(r, UI_SETTINGS); }

static esp_err_t setPost(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  char q[256], k[16], v[200];                     // v: a hotspot password, URL-encoded
  if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
      httpd_query_key_value(q, "k", k, sizeof(k)) != ESP_OK ||
      httpd_query_key_value(q, "v", v, sizeof(v)) != ESP_OK) return text(r, "400 Bad Request", "k and v missing");
  urlDecode(v);
  if (!strcmp(k, "hpw") && netState() == NET_PLAY && !netApPass()[0])   // open hotspot: not from strangers
    return text(r, "403 Forbidden", "switch the password on at the console first (RIGHT on the hotspot page)");
  if (!strcmp(k, "aikey") && netState() == NET_PLAY && !netApPass()[0])
    return text(r, "403 Forbidden", "switch the hotspot password on at the console first");
  if (!strcmp(k, "aikey"))                        // stored in net.cpp, never read back out
    return netAiSetKey(v) ? text(r, "200 OK", v[0] ? "key saved - it is not shown again" : "key forgotten")
                          : text(r, "400 Bad Request", "that does not look like an API key");
  if (!strcmp(k, "hpw"))                          // the hotspot lives in net.cpp, not in the games
    return netApSetPass(v) ? text(r, "200 OK", v[0] ? "saved - used from the next hotspot start"
                                                    : "no password from the next hotspot start")
                           : text(r, "400 Bad Request", "8 to 63 plain characters, or empty");
  uiA = k; uiB = v;
  if (!askUi(UI_SET)) return busy(r);
  if (uiErr) return text(r, "400 Bad Request", uiErr);
  return text(r, "200 OK", "saved");
}

static esp_err_t hotspotGet(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  char b[112], esc[70];
  size_t k = 0;
  for (const char *p = netApPass(); *p && k < sizeof(esc) - 3; p++) {   // JSON-safe
    if (*p == '"' || *p == '\\') esc[k++] = '\\';
    esc[k++] = *p;
  }
  esc[k] = 0;
  snprintf(b, sizeof(b), "{\"pw\":\"%s\",\"ai\":%d}", esc, netAiHasKey() ? 1 : 0);   // never the key itself
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_sendstr(r, b);
}

static esp_err_t screenGet(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  if (!askUi(UI_SCREEN)) return busy(r);
  httpd_resp_set_type(r, "application/octet-stream");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, uiOut, 1024);
}

// keys go straight in: a flag per key, the UI loop reads it with the others
static esp_err_t keyPost(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  int k = queryInt(r, "k", -1), d = queryInt(r, "d", 0);
  if (k < 0 || k > 4) return text(r, "400 Bad Request", "k 0..4");
  appKey((uint8_t)k, d != 0);
  return text(r, "200 OK", "ok");
}

static esp_err_t levelGet(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  uiSlot = (uint8_t)queryInt(r, "s", 0);
  if (!askUi(UI_LEVELGET)) return busy(r);
  httpd_resp_set_type(r, "text/plain");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, uiOut, uiLen);
}

static esp_err_t levelPost(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  static char body[256];
  if (readBody(r, body, sizeof(body)) < 0) return text(r, "400 Bad Request", "level too big");
  uiSlot = (uint8_t)queryInt(r, "s", 0);
  uiA = body;
  if (!askUi(UI_LEVEL)) return busy(r);
  if (uiErr) return text(r, "400 Bad Request", uiErr);
  return text(r, "200 OK", body[0] ? "saved - on the console: Sokoban, then RIGHT past the last level"
                                   : "slot is empty now");
}

// ---------------- backup ----------------
static const char *const BK_NS[] = { "arcade", "link" };   // not "net": the WLAN password stays

static esp_err_t backupGet(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  char *line = (char *)malloc(1600);
  uint8_t *blob = (uint8_t *)malloc(1100);
  if (!line || !blob) { free(line); free(blob); return text(r, "500 Internal Server Error", "out of memory"); }
  httpd_resp_set_type(r, "text/plain");
  httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=\"miniarcade-backup.txt\"");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  snprintf(line, 1600, "%s\n# firmware %s\n", BK_HEAD, netVersion());
  httpd_resp_send_chunk(r, line, HTTPD_RESP_USE_STRLEN);
  for (const char *ns : BK_NS) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) continue;
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY, &it);
    while (e == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      int n = -1;
      if (info.type == NVS_TYPE_U16) {
        uint16_t v;
        if (nvs_get_u16(h, info.key, &v) == ESP_OK) n = bkLine(line, 1600, ns, info.key, BK_U16, &v, 2);
      } else if (info.type == NVS_TYPE_STR) {
        size_t len = 1100;
        if (nvs_get_str(h, info.key, (char *)blob, &len) == ESP_OK) n = bkLine(line, 1600, ns, info.key, BK_STR, blob, 0);
      } else if (info.type == NVS_TYPE_BLOB) {
        size_t len = 1100;
        if (nvs_get_blob(h, info.key, blob, &len) == ESP_OK) n = bkLine(line, 1600, ns, info.key, BK_BLOB, blob, (int)len);
      }
      if (n > 0) httpd_resp_send_chunk(r, line, n);
      e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);
  }
  free(line);
  free(blob);
  return httpd_resp_send_chunk(r, NULL, 0);
}

static volatile bool    askRestore = false;
static volatile uint8_t restoreAnswer = 0;   // 1 = yes, 2 = no

bool phoneAsking()         { return askRestore; }
void phoneAnswer(bool yes) { restoreAnswer = yes ? 1 : 2; askRestore = false; }

static esp_err_t restorePost(httpd_req_t *r) {
  if (!allowed(r)) return ESP_OK;
  const int MAXB = 16384, MAXE = 128;
  char *body = (char *)malloc(MAXB);
  uint8_t *arena = (uint8_t *)malloc(MAXB);
  BkEntry *ent = (BkEntry *)malloc(sizeof(BkEntry) * MAXE);
  const uint8_t **data = (const uint8_t **)malloc(sizeof(uint8_t *) * MAXE);
  auto done = [&](const char *status, const char *t) {
    free(body); free(arena); free(ent); free(data);
    return text(r, status, t);
  };
  if (!body || !arena || !ent || !data) return done("500 Internal Server Error", "out of memory");
  if (readBody(r, body, MAXB) < 0) return done("400 Bad Request", "file broken or too big");

  // check the whole file before anything is touched
  int n = 0, used = 0;
  bool head = false;
  for (char *line = strtok(body, "\n"); line; line = strtok(NULL, "\n")) {
    if (!head) {
      if (strncmp(line, BK_HEAD, strlen(BK_HEAD))) return done("400 Bad Request", "not a MiniArcade backup");
      head = true;
      continue;
    }
    if (line[0] == '#' || line[0] == '\r' || !line[0]) continue;    // comments, empty lines
    if (n >= MAXE) return done("400 Bad Request", "too many entries");
    BkEntry &e = ent[n];
    if (!bkParse(line, e, arena + used, MAXB - used)) return done("400 Bad Request", "a line of the file is broken");
    bool ok = false;
    for (const char *ns : BK_NS) if (!strcmp(e.ns, ns)) ok = true;
    if (!ok) return done("400 Bad Request", "unknown part in the file");
    data[n] = arena + used;
    if (e.type == BK_BLOB) used += e.len;
    n++;
  }
  if (!head || !n) return done("400 Bad Request", "the file is empty");

  // somebody has to say yes on the console itself
  restoreAnswer = 0;
  askRestore = true;
  for (uint16_t t = 0; t < 300 && !restoreAnswer; t++) vTaskDelay(pdMS_TO_TICKS(100));   // 30 s
  askRestore = false;
  if (restoreAnswer != 1) return done("409 Conflict", "not confirmed on the console (open its WLAN page)");

  for (const char *ns : BK_NS) {                   // the parts in the file replace the old ones
    bool inFile = false;
    for (int i = 0; i < n; i++) if (!strcmp(ent[i].ns, ns)) inFile = true;
    if (!inFile) continue;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return done("500 Internal Server Error", "flash not writable");
    nvs_erase_all(h);
    for (int i = 0; i < n; i++) {
      const BkEntry &e = ent[i];
      if (strcmp(e.ns, ns)) continue;
      if (e.type == BK_U16)  nvs_set_u16(h, e.key, e.u16);
      if (e.type == BK_STR)  nvs_set_str(h, e.key, e.text);
      if (e.type == BK_BLOB) nvs_set_blob(h, e.key, data[i], e.len);
    }
    nvs_commit(h);
    nvs_close(h);
  }
  netRestartLater();                                // everything is read fresh at the start
  return done("200 OK", "restored - the console restarts");
}

// ---------------- registration ----------------
void phoneRegister(void *server) {
  if (!uiDone) uiDone = xSemaphoreCreateBinary();
  static const httpd_uri_t uris[] = {
    { "/stats",        HTTP_GET,  statsPage,    NULL },
    { "/settings",     HTTP_GET,  settingsPage, NULL },
    { "/screen",       HTTP_GET,  screenPage,   NULL },
    { "/pad",          HTTP_GET,  padPage,      NULL },
    { "/level",        HTTP_GET,  levelPage,    NULL },
    { "/backup",       HTTP_GET,  backupPage,   NULL },
    { "/api/stats",    HTTP_GET,  statsGet,     NULL },
    { "/api/settings", HTTP_GET,  settingsGet,  NULL },
    { "/api/set",      HTTP_POST, setPost,      NULL },
    { "/api/hotspot",  HTTP_GET,  hotspotGet,   NULL },
    { "/api/screen",   HTTP_GET,  screenGet,    NULL },
    { "/api/key",      HTTP_POST, keyPost,      NULL },
    { "/api/level",    HTTP_GET,  levelGet,     NULL },
    { "/api/level",    HTTP_POST, levelPost,    NULL },
    { "/api/backup",   HTTP_GET,  backupGet,    NULL },
    { "/api/restore",  HTTP_POST, restorePost,  NULL },
  };
  for (const httpd_uri_t &u : uris) httpd_register_uri_handler((httpd_handle_t)server, &u);
}
