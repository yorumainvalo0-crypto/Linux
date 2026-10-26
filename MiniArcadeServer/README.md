# MiniArcade Server (TV und Mehrspieler)

YouTube für die **MiniArcade Color** (ESP32-C3 / ESP32-S3 mit 320×240-Display) –
und der Treffpunkt für **Mehrspieler übers Internet** (siehe unten).
Der Server sucht und lädt die Videos mit **yt-dlp**, dekodiert sie mit
**ffmpeg**, rechnet sie auf die 256 Farben der Konsole herunter und schickt
sie in einem eigenen Kachelformat, das die Konsole direkt in ihren
Bildspeicher schreibt. Kein Google-Konto, keine Cookies – und **nur YouTube**:
yt-dlp läuft ausschließlich mit seinen YouTube-Extraktoren, andere Adressen
lehnt der Server ab.

Alles geht **an der Konsole allein** (Adresse, PIN, Suche, Steuerung). Die
Handy-Seite ist ein Extra: bequemer suchen, Video auf die Konsole schicken,
Fernbedienung, Konsolen entfernen.

## Starten

Auf dem Server (Docker mit Compose-Plugin):

```sh
git clone https://github.com/yorumainvalo0-crypto/Linux.git
cd Linux/MiniArcadeServer
cp .env.example .env
nano .env                  # ADMIN_PASSWORD und CONSOLE_PIN setzen
docker compose up -d --build
docker compose logs -f     # läuft? (Strg+C beendet nur die Anzeige)
```

Der Container hört auf `PORT` (Standard 8080) und spricht **nur HTTP** – das
Zertifikat (Wildcard) liegt beim vorhandenen Reverse-Proxy. Ohne Änderung ist
er nur von der Maschine selbst erreichbar (`127.0.0.1:8080`), also genau für
einen Proxy auf demselben Host.

Test: `curl http://127.0.0.1:8080/health` → `ok`
(mit `BASE_PATH=/tv`: `curl http://127.0.0.1:8080/tv/health`).

**Aktualisieren:** `git pull && docker compose up -d --build`.
yt-dlp aktualisiert sich selbst: bei jedem Containerstart und danach alle
`YTDLP_UPDATE_HOURS` Stunden (YouTube ändert sich oft) – ohne Neustart.

## Einstellungen (.env)

| Variable | Standard | Bedeutung |
|---|---|---|
| `ADMIN_PASSWORD` | – (Pflicht) | Passwort der Handy-Seite. Ohne startet der Server nicht. |
| `CONSOLE_PIN` | wird erzeugt | PIN, mit der sich **jede** Konsole einmal koppelt (4–10 Ziffern, besser 8). Fehlt sie, erzeugt der Server eine 8-stellige, speichert sie im Volume und schreibt sie ins Log (`docker compose logs \| grep PIN`). Sie steht auch auf der Handy-Seite. |
| `NET_OPEN` | 1 | Mehrspieler übers Internet **ohne PIN**: jede Konsole holt sich selbst einen Token, der nur für `/api/net/*` gilt (siehe „Mehrspieler übers Internet“). `0` = nur gekoppelte Konsolen (PIN) spielen online; bereits ausgegebene Nur-Mehrspieler-Tokens gelten dann nicht mehr. Das TV braucht immer die PIN. |
| `CHAT_KEEP_DAYS` | 30 | So viele Tage bleiben Chat-Nachrichten gespeichert (siehe „Chat“). |
| `PORT` | 8080 | Port im Container. |
| `HOST_PORT`, `BIND_IP` | 8080, 127.0.0.1 | Wo Docker den Port auf dem Host anbietet. `BIND_IP=0.0.0.0` nur, wenn der Proxy auf einer anderen Maschine läuft. |
| `BASE_PATH` | leer | Unterpfad, z. B. `/tv`, wenn der Proxy den Server unter `https://example.de/tv` anbietet. |
| `CACHE_MINUTES` | 10 | So lange werden Suchen und Listen gemerkt. |
| `THUMB_CACHE_MB` | 50 | Platz für Vorschaubilder im Volume. |
| `YTDLP_UPDATE_HOURS` | 24 | yt-dlp-Update; `0` = nie (auch nicht beim Start). |
| `CATEGORIES` | `Beliebt=;Musik=musik;…` | Kategorien der Startseite: `Name=Suchbegriff;…`. Ein leerer Begriff („Beliebt“) mischt die meistgesehenen der Woche aus allen anderen. |
| `TRUSTED_PROXIES` | leer | IPs/Netze des Reverse-Proxys, mit Komma (z. B. `10.0.0.5` oder `192.168.10.0/24`). Nur bei Anfragen **von dort** gelten `X-Forwarded-For` / `X-Real-IP` als Client-Adresse (PIN-Sperre, Begrenzungen). Leer = die Adresse der Verbindung zählt (dann teilen sich alle Clients die Adresse des Proxys). |
| `TZ` | – | Zeitzone, z. B. `Europe/Berlin` (für „Pause bis …“). |
| `YT_PROXY` | leer | Aller YouTube-Verkehr über diesen Proxy: `socks5://user:pass@host:1080` oder `http://user:pass@host:3128`. Siehe „IP sauber halten“. |
| `YT_SOURCE_ADDRESS` | leer | … oder von dieser zusätzlichen IP (IPv4/IPv6) des Servers aus. |
| `YT_COOLDOWN_HOURS` | 6 | Pause nach einer Bot-Prüfung (siehe unten). |
| `YT_REQUESTS_PER_MIN` | 20 | Höchstens so viele yt-dlp-Aufrufe pro Minute, alle Konsolen zusammen. |
| `YT_THUMBS_PER_MIN` | 60 | Höchstens so viele Vorschaubilder pro Minute. |
| `YT_SLEEP_REQUESTS` | 1 | Sekunden Pause zwischen den Einzelanfragen eines yt-dlp-Aufrufs. |

## Nie ein YouTube-Login

Der Server meldet sich **nie** bei YouTube an: keine Cookies, kein Konto,
kein OAuth, kein Passwort. Die yt-dlp-Optionen dafür (`--cookies`,
`--cookies-from-browser`, `--username`, `--password`, `--netrc` …) werden im
Code verweigert (`tvserver/egress.py`, `FORBIDDEN`), yt-dlp läuft mit
`--ignore-config --no-cookies --no-cookies-from-browser`, der HTTP-Teil
speichert keine Cookies. Weder Konsole noch Handy-Seite bieten eine
Anmeldung bei YouTube an. Empfiehlt yt-dlp in einer Fehlermeldung Cookies,
wird das ignoriert – stattdessen greift die Pause (unten).

## IP sauber halten (optional)

YouTube merkt sich Adressen von Rechenzentren, die ohne Anmeldung viel
abrufen, und antwortet dann mit „Sign in to confirm you're not a bot“.
Das ist eine **interne Markierung bei YouTube/Google** – keine öffentliche
Spam-Blacklist; E-Mail, Webseiten und andere Dienste auf der Haupt-IP sind
davon nicht betroffen, aber YouTube vom Server aus dann eine Zeit lang
schon. Damit die Haupt-IP gar nicht erst auffällt:

- **Aller** YouTube-Verkehr (Suche/Listen über yt-dlp, Vorschaubilder,
  Videodaten) läuft über **einen** Weg: `YT_PROXY` oder `YT_SOURCE_ADDRESS`.
  ffmpeg spricht nie selbst mit YouTube – es liest über ein internes Relay
  des Servers (`/_media/…`, nur von 127.0.0.1), das die Daten über diesen
  Weg holt.
- Beides ist **optional**. Ist keins gesetzt, geht der Server direkt ins
  Netz (eine kurze Info-Zeile im Log) – dann kann YouTube eben die Server-IP
  markieren; mehr passiert nicht.
- Schonend: Suchen und Listen werden `CACHE_MINUTES` gemerkt, Vorschaubilder
  auf der Platte, höchstens `YT_REQUESTS_PER_MIN` yt-dlp-Aufrufe pro Minute,
  Pausen innerhalb von yt-dlp, ein Video pro Konsole, kein Vorladen.
- Kommt doch eine Bot-Prüfung (oder 403/429), stellt der Server **alle**
  YouTube-Anfragen für `YT_COOLDOWN_HOURS` ein (kein erneuter Versuch).
  Konsole und Handy-Seite zeigen „YouTube blockiert gerade diese Verbindung –
  Pause bis …“. Ein Neustart des Containers hebt die Pause auf.

**Variante A – kleiner Proxy zu Hause** (Raspberry Pi o. ä. am Heimanschluss):

```sh
# auf dem Pi: SOCKS5-Proxy mit Passwort (Docker)
docker run -d --name socks --restart unless-stopped -p 1080:1080 \
  -e PROXY_USER=arcade -e PROXY_PASSWORD=langes-passwort serjs/go-socks5-proxy
```

Dann entweder im Router Port 1080 (TCP) an den Pi weiterleiten (am besten
nur für die IP des Servers freigeben) und `YT_PROXY=socks5://arcade:langes-passwort@<Heim-DynDNS>:1080`
setzen – oder (sicherer, ohne offenen Port) Server und Pi per **WireGuard**
verbinden und den Proxy nur im Tunnel anbieten:
`YT_PROXY=socks5://arcade:langes-passwort@10.8.0.2:1080`.
Ein HTTP-Proxy geht genauso (z. B. tinyproxy/3proxy mit Passwort):
`YT_PROXY=http://user:pass@10.8.0.2:3128`.

**Variante B – zusätzliche IP:** Bei Hetzner eine weitere IPv4 oder einfach
eine Adresse aus dem eigenen IPv6-/64-Netz für YouTube nehmen und
`YT_SOURCE_ADDRESS=2a01:4f8:…::77` setzen. Die Adresse muss am Server
konfiguriert sein, und der Container braucht dann `network_mode: host`
(in `docker-compose.yml` ergänzen, `ports:` entfällt), damit er sie sieht.
Fällt diese Adresse auf, bleibt die Haupt-IP trotzdem unberührt.

Daten (Konsolen mit ihren Tokens – nur als Hash –, Verlauf, Favoriten,
Vorschaubilder, Cookie-Schlüssel, ggf. die erzeugte PIN) liegen im Docker-Volume
`miniarcade-data` (`/data` im Container).

## Hinter dem Reverse-Proxy

**Proxy auf einer anderen Maschine** (TLS endet dort): in `.env`
`BIND_IP` auf die Adresse dieses Servers setzen, die der Proxy erreicht
(z. B. die private IP, sonst `0.0.0.0`), und `TRUSTED_PROXIES` auf die
Adresse des Proxys, wie sie hier ankommt:

```sh
BIND_IP=10.0.0.2          # dieser Server im privaten Netz (oder 0.0.0.0)
HOST_PORT=8080
TRUSTED_PROXIES=10.0.0.5  # der Proxy
```

Der Port darf **nur vom Proxy** erreichbar sein (sonst könnte jemand den
Proxy umgehen und falsche `X-Forwarded-For` schicken – die werden dann zwar
ignoriert, aber unverschlüsselt sollte niemand anderes rankommen). Achtung:
Docker-Portfreigaben umgehen `ufw`; deshalb die Regel in die Kette
`DOCKER-USER` (oder in die Hetzner-Cloud-Firewall bzw. nur das private Netz):

```sh
iptables -I DOCKER-USER -p tcp --dport 8080 ! -s 10.0.0.5 -j DROP
```

Der Proxy muss die Client-Adresse mitschicken: nginx
`proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;` und
`proxy_set_header X-Real-IP $remote_addr;`, Traefik und Caddy setzen
`X-Forwarded-For` von selbst.

Der Proxy muss den Videostrom **ungepuffert** weitergeben (der Server sendet
dafür auch `X-Accel-Buffering: no`) und Anfragen bis ca. 30 s offen lassen.

**nginx** (im `server { … }` mit dem Wildcard-Zertifikat):

```nginx
server {
    listen 443 ssl;
    server_name tv.example.de;
    # ssl_certificate / ssl_certificate_key: das vorhandene Wildcard-Zertifikat
    location / {
        proxy_pass http://127.0.0.1:8080;     # Proxy auf anderer Maschine: http://10.0.0.2:8080
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_buffering off;
        proxy_read_timeout 60s;
    }
}
```

Mit Unterpfad (`BASE_PATH=/tv`): `location /tv/ { proxy_pass http://127.0.0.1:8080; … }`
(ohne Pfad hinter dem Port, damit `/tv/…` unverändert ankommt).

**Caddy:** `tv.example.de { reverse_proxy 127.0.0.1:8080 { flush_interval -1 } }`

**Traefik** (Labels in `docker-compose.yml` ergänzen, Port-Freigabe dann weglassen):
`traefik.http.routers.tv.rule=Host(\`tv.example.de\`)`,
`traefik.http.services.tv.loadbalancer.server.port=8080` – Traefik streamt
ohne weitere Einstellung.

Hinweis Speicher: Die Konsole (ESP32-C3) hat wenig RAM. TLS-Datensätze bis
16 kB gehen, kleinere sind besser – bei nginx z. B. `ssl_buffer_size 4k;`
im `server`-Block (optional).

## Konsole einrichten (ohne Handy)

1. Konsole ins WLAN bringen (Settings → wlan and update).
2. In der Spieleliste **TV** öffnen. Die Firmware bringt eine Server-Adresse
   mit (Kconfig `ARCADE_DEFAULT_SERVER`, Standard `https://vid.dev.a-b-cloud.de`).
   Ist sie leer gebaut, kommt beim ersten Mal die Tastatur für die Adresse,
   vorbelegt mit `https://` (Tasten `.de`, `.com`, `.net`, `del`, `done`).
   Beispiel: `https://tv.example.de` oder `https://example.de/tv`. Änderbar:
   TV → Reiter *Setup*, oder Settings → *tv server* (leer = die eingebaute).
3. Die Konsole testet die Verbindung (`/api/ping`) und fragt dann die
   **Server-PIN** ab: OBEN/UNTEN ändert die Ziffer, LINKS/RECHTS wechselt die
   Stelle, `-` beendet eine kürzere PIN, OK koppelt. Die Konsole bekommt
   einen eigenen, langen Token (im Flash gespeichert, die PIN selbst nicht).
   Nach 5 falschen PINs ist das Koppeln für diese Adresse/Konsole 10 Minuten
   gesperrt.
4. Entkoppeln: TV → *Setup* → *unpair*, oder auf der Handy-Seite die Konsole
   entfernen (ihr Token gilt dann nicht mehr).

Optional geht beides auch über die Handy-Seite **der Konsole**
(`http://<IP der Konsole>/settings`, Abschnitt „TV“).

## Bedienung an der Konsole

Liste: OBEN/UNTEN wählen, LINKS/RECHTS Reiter (Kategorien, Search,
Favourites, History, Setup), OK → *play* / *favourite* / *back*, OK lang = zurück.
Suche: Tastatur an der Konsole, die letzten 4 Suchen bleiben gemerkt.

Video: OK Pause/weiter, LINKS/RECHTS −/+10 s, OBEN *sharp* (320×240, weniger
Bilder/s), UNTEN *fluent* (160×120 doppelt, mehr Bilder/s), OK lang = zurück
zur Liste. Mit Settings → *show fps* zeigt die Konsole Bilder/s und kB/s.

## Handy-Seite (optional)

`https://tv.example.de/` → Passwort (`ADMIN_PASSWORD`) → YouTube suchen oder
Link einfügen, „Auf Konsole abspielen“ (die Konsole muss TV geöffnet haben),
Fernbedienung, Liste der Konsolen (entfernen), Testbild senden. Die Seite
zeigt auch die Server-PIN.

## Wie es funktioniert

- Die Konsole holt den Strom in Abschnitten von ½ s (`/api/tv/stream`), eine
  TLS-Verbindung, Keep-Alive. Jede Anfrage meldet, wie viele Bilder gezeigt
  und verworfen wurden; der Server passt Bildrate und Qualität an.
- Echtzeit: Jedes Bild trägt seine Videozeit. Zu spät → wird verworfen (nie
  Zeitlupe). Pause/Spulen/Modus starten ffmpeg an der neuen Stelle neu.
- Kacheln 8×8: unverändert / eine Farbe / zwei Farben + Maske / vier Farben /
  roh. Format: `tvserver/codec.py`, Decoder der Konsole: `MiniArcadeColor/main/tvcodec.h`.
- ffmpeg wird beendet, wenn eine Konsole 15 s nichts mehr abholt.

## Mehrspieler übers Internet

Die Spiele, die zwei Konsolen sonst per Funk (ESP-NOW) nebeneinander spielen,
gehen auch übers Internet – über diesen Server. Er vermittelt nur: Lobby,
Freunde, Herausforderungen, und er reicht die Spielzüge weiter. Gerechnet
wird weiter auf beiden Konsolen.

**Einrichten:** nichts. Die Firmware kennt eine eingebaute Server-Adresse
(Kconfig `ARCADE_DEFAULT_SERVER`, Standard `https://vid.dev.a-b-cloud.de`;
an der Konsole änderbar wie bisher: TV → *Setup* oder Settings → *tv
server*). Bei „Online → Internet (server)“ ohne Token fragt die Konsole
selbst `POST /api/net/hello` (Konsolencode und Name) und bekommt einen
eigenen Token – keine Adresse tippen, keine PIN. Dieser Token gilt **nur
für den Mehrspieler-Teil** (`/api/net/*`); das TV (Listen, Videos) braucht
weiter die Kopplung mit `CONSOLE_PIN`. Koppelt man eine solche Konsole
später fürs TV, bleibt es dieselbe Konsole (Freundescode und Freunde
bleiben), sie bekommt nur einen neuen, vollen Token. Eine fürs TV
gekoppelte Konsole spielt mit ihrem Token sofort online.

Mit `NET_OPEN=0` antwortet `/api/net/hello` mit 403; dann führt die Konsole
wie früher durch die Adress- und PIN-Eingabe (dieselbe wie beim TV), und
Nur-Mehrspieler-Tokens von vorher gelten nicht mehr. Gegen Massenanmeldungen:
höchstens 5 neue Konsolen auf einmal und dann 20 pro Stunde **pro
Client-Adresse** (hinter dem Proxy zählt die echte Adresse nur mit
`TRUSTED_PROXIES`, sonst teilen sich alle die des Proxys), und höchstens
500 Nur-Mehrspieler-Konsolen im Volume (die älteste fällt heraus).
Konsolen ohne Namen heißen „MA“ + Konsolencode (z. B. `MAQX7K`); der Name
ist in der Lobby änderbar. Auf der Handy-Seite stehen sie als „nur
Mehrspieler (ohne PIN)“ und lassen sich entfernen.

**An der Konsole:** Online → *Internet (server)*. Die Lobby zeigt sofort
**alle** Konsolen dieses Servers, die gerade online sind (letzte Abfrage
vor weniger als 30 s) – ohne Codes, ohne Freunde –, mit Name, frei/spielt
und dem Spiel, das sie gern spielen würden; freie zuerst. Darunter grau die
Konsolen, die in den letzten 10 Minuten da waren („offline“, vor wie vielen
Minuten), dann Freunde, die offline sind. OK auf einer freien Konsole
fordert sie heraus (30 s Zeit zum Antworten) – jede, ohne Freundschaft.
Eigene Zeile, OK: Name, Wunschspiel und – nebensächlich – *friends: my
code* (der Freundescode) und *friends: add by code*. RECHTS auf einer
Konsole: als Freund markieren / entfernen (Freunde stehen unter den freien
zuerst und bleiben auch offline in der Liste).

- **Freundescodes:** 6 Zeichen aus `23456789ABCDEFGHJKMNPQRSTUVWXYZ` (kein
  0/O, 1/I/L), einmal pro Konsole erzeugt und mit der Freundesliste in
  `state.json` im Volume gespeichert (`fc`, `friends`). Freundschaft gilt in
  beide Richtungen, höchstens 16 Freunde. Wird eine Konsole auf der
  Handy-Seite entfernt, verschwindet sie auch aus allen Freundeslisten.
- **Nur im Speicher:** Anwesenheit, Herausforderungen, laufende Spiele und
  alle Spielnachrichten. Nichts davon landet auf der Platte; ein Neustart des
  Containers beendet laufende Spiele.

**API** (alles mit `X-Token` der Konsole, unter `BASE_PATH`):

| Anfrage | Zweck |
|---|---|
| `POST /api/net/poll` (JSON) | Die Konsole meldet sich (`n` Name, `g` Wunschspiel, `rtt` ihre Antwortzeit, `l` = Listen mitschicken), schickt ihre Nachrichten (`sid`, `m: [[nr, "hex"], …]`, bis 16 à 32 Byte) und bestätigt die erhaltenen (`got`). Antwort: eigener Code `me`, eine Herausforderung `inv`, Stand der eigenen `out`, das Spiel `ses` (mit `se` = `left`/`gone`/`lost`, wenn es vorbei ist), `ack`, die Nachrichten der Gegenseite `msg`, auf Wunsch `lob` und `fr`. Ist nichts Neues da, hält der Server die Anfrage bis `wait` ms (höchstens 25 s) und antwortet sofort, wenn etwas kommt. |
| `POST /api/net/hello?code=QX7K&name=…` | ohne Token, nur mit `NET_OPEN=1`: neuer Nur-Mehrspieler-Token (`token`, `me`); 403 wenn geschlossen, 429 bei zu vielen pro Adresse |
| `GET /api/net/me` | eigener Freundescode und Name |
| `POST /api/net/invite?to=CODE&g=SPIEL` | herausfordern (404 unbekannt, 409 `offline`/`busy`/`self`) |
| `POST /api/net/answer?iid=…&yes=1` | annehmen (Antwort enthält das Spiel) oder ablehnen |
| `POST /api/net/cancel?oid=…` | eigene Herausforderung zurücknehmen |
| `POST /api/net/leave?sid=…` | Spiel verlassen |
| `POST /api/net/friend?c=CODE&on=1` | Freund hinzufügen (`on=0` entfernen) |

**Zustellung:** Die Nachrichten eines Spiels sind je Richtung durchnummeriert.
Die Konsole wiederholt unbestätigte, der Server nimmt jede Nummer genau
einmal und in Reihenfolge; die Gegenseite meldet mit `got`, was sie hat, und
bekommt Verlorenes einfach noch einmal.

**Listen:** Mit `l=1` kommen `lob` (online, freie zuerst, höchstens 16) und
`fr` (Freunde); mit `l=2` zusätzlich `rc`: bis zu 8 Konsolen, die in den
letzten 10 Minuten online waren (`m` = vor so vielen Minuten). Ältere
Konsolen schicken `l=1` und sehen alles wie bisher. So geht weder eine verlorene Anfrage
noch eine verlorene Antwort verloren.

**Zeitgrenzen:** online 30 s nach der letzten Abfrage; Herausforderung 30 s;
wer im Spiel 12 s nicht abfragt, ist weg (die andere Konsole erfährt `gone`,
er selbst später `lost`); verlassene Spiele werden nach 30 s aufgeräumt.

**Grenzen gegen Missbrauch:** pro Konsole 40 Anfragen/s (80 auf einmal), 40
neue Nachrichten/s (80 auf einmal), höchstens 64 wartende Nachrichten je
Seite, 1 Herausforderung/s (8 auf einmal), Freundescodes 1 pro 5 s (5 auf
einmal – Raten lohnt nicht). Darüber: 429.

**Echtzeitspiele** (Pong, Snake, Pac-Man, Bomberman) laufen auf beiden
Konsolen im Gleichschritt (20 ms pro Takt); eine Eingabe zählt erst einige
Takte später. Übers Internet wählt der Server diese Verzögerung aus den
gemeldeten Antwortzeiten beider Konsolen: 1,25 × (beide zusammen) + 40 ms,
mindestens 6, höchstens 24 Takte (120–480 ms). Bei je 50 ms Antwortzeit sind
das 180 ms. Bis etwa 200 ms Antwortzeit laufen die Spiele in voller
Geschwindigkeit, darüber werden sie langsamer. Rundenspiele
(4 gewinnt, Tic Tac Toe, Schiffe versenken, Dame, Mau-Mau, Schach) merken
die Verzögerung kaum.

**nginx:** nichts Neues. Es gibt kein WebSocket; die Abfragen sind kurze
HTTPS-Anfragen auf der vorhandenen Keep-Alive-Verbindung, gehalten höchstens
25 s – das passt zum vHost oben (`proxy_http_version 1.1`,
`proxy_set_header Connection ""`, `proxy_buffering off`,
`proxy_read_timeout 60s`). Die Mehrspieler-Grenzen zählen pro Konsole
(Token), nicht pro Adresse; `TRUSTED_PROXIES` braucht es für die PIN-Sperre
beim Koppeln und für die Grenze neuer Konsolen ohne PIN (`/api/net/hello`).

## Chat

Die Konsolen schreiben sich über diesen Server Nachrichten („Chat“ in der
Spieleliste). Es gilt derselbe Token wie für den Mehrspieler-Teil (auch
der ohne PIN).

- **Ein Name pro Konsole:** 3–12 Zeichen aus Buchstaben, Ziffern, `-` und
  `_`, eindeutig (Groß-/Kleinschreibung zählt gleich). Der Name hängt am
  Token der Konsole und an einer Geräte-ID (Hash der MAC, die MAC selbst
  sieht der Server nicht). Kommt dasselbe Gerät mit neuem Token wieder
  (z. B. nach „Erase Flash“), wird das alte Konto mit seinen Nachrichten
  gelöscht – nie übergeben, eine kopierte Geräte-ID liest also nichts mit.
  Wird eine Konsole auf der Handy-Seite entfernt, verschwindet auch ihr
  Chat-Konto.
- **Verlauf auf dem Server:** `chat.db` (SQLite) im Volume, Nachrichten
  `CHAT_KEEP_DAYS` Tage (Standard 30), höchstens 500 pro zwei Personen.
  Gespeichert werden nur ASCII und ä ö ü Ä Ö Ü ß, höchstens 120 Byte pro
  Nachricht.
- **Blockieren:** Ein blockierter Name kann einem nichts mehr schicken (und
  man ihm auch nicht); seine Nachrichten zählen nicht als ungelesen.
- **Grenzen pro Konsole:** 1 Nachricht pro Sekunde (10 auf einmal), Namen
  ändern selten (5 auf einmal, dann alle 20 s).

| Anfrage (POST, JSON, mit `X-Token`) | Zweck |
|---|---|
| `/api/chat/me` | eigener Name `n` (leer = noch keiner) und `u` ungelesene |
| `/api/chat/name` `{n, dev}` | Namen nehmen oder ändern; 409 `taken` / `bad` |
| `/api/chat/list` | die Chats, neuester zuerst: `n`, `u` ungelesen, `o` online, `b` blockiert, `m` Minuten seit der letzten Nachricht |
| `/api/chat/people` `{q, off}` | alle mit Namen, alphabetisch ab `q`, je 20 (`more`) |
| `/api/chat/msgs` `{n, before, after}` | bis 20 Nachrichten mit `n` (`i` id, `m` 1 = eigene, `a` Minuten alt, `x` Text), markiert sie als gelesen |
| `/api/chat/send` `{n, x}` | senden; 409 `blocked` / `empty`, 429 zu schnell |
| `/api/chat/block` `{n, on}` | blockieren / freigeben |

Ohne Chat-Namen antworten list, people, msgs, send und block mit 409
`noname`; ein unbekannter Name gibt 404.

## Tests (Entwicklung)

```sh
python3 -m venv /tmp/venv && /tmp/venv/bin/pip install -r requirements-dev.txt
/tmp/venv/bin/python -m pytest          # im Ordner MiniArcadeServer
python3 tests/make_golden.py            # nur nach Änderungen am Format
```

Die Tests laden nichts von YouTube (yt-dlp wird durch `tests/fake_ytdlp.py`
ersetzt). `tests/test_net.py` prüft den Mehrspieler-Teil (Anmeldung, `NET_OPEN` an/aus,
Reichweite der Tokens, Grenze pro Adresse, Lobby, Freunde, Herausforderungen, Reihenfolge der Nachrichten, Zeitgrenzen und
Begrenzungen) mit einer verstellbaren Uhr; `tests/test_chat.py` den Chat (Namen, ein Konto
pro Gerät, Verlauf, gelesen/ungelesen, Blockieren, Aufräumen). `tests/golden.bin` prüft auch der Simulator der Konsole.
