# MiniArcade Server (TV)

YouTube für die **MiniArcade Color** (ESP32-C3 / ESP32-S3 mit 320×240-Display).
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
| `PORT` | 8080 | Port im Container. |
| `HOST_PORT`, `BIND_IP` | 8080, 127.0.0.1 | Wo Docker den Port auf dem Host anbietet. `BIND_IP=0.0.0.0` nur, wenn der Proxy auf einer anderen Maschine läuft. |
| `BASE_PATH` | leer | Unterpfad, z. B. `/tv`, wenn der Proxy den Server unter `https://example.de/tv` anbietet. |
| `CACHE_MINUTES` | 10 | So lange werden Suchen und Listen gemerkt. |
| `THUMB_CACHE_MB` | 50 | Platz für Vorschaubilder im Volume. |
| `YTDLP_UPDATE_HOURS` | 24 | yt-dlp-Update; `0` = nie (auch nicht beim Start). |
| `CATEGORIES` | `Beliebt=;Musik=musik;…` | Kategorien der Startseite: `Name=Suchbegriff;…`. Ein leerer Begriff („Beliebt“) mischt die meistgesehenen der Woche aus allen anderen. |
| `TRUST_PROXY` | 1 | Die Client-Adresse aus `X-Forwarded-For` nehmen (für die Sperren pro Adresse). |
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

Der Proxy muss den Videostrom **ungepuffert** weitergeben (der Server sendet
dafür auch `X-Accel-Buffering: no`) und Anfragen bis ca. 30 s offen lassen.

**nginx** (im `server { … }` mit dem Wildcard-Zertifikat):

```nginx
server {
    listen 443 ssl;
    server_name tv.example.de;
    # ssl_certificate / ssl_certificate_key: das vorhandene Wildcard-Zertifikat
    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
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
2. In der Spieleliste **TV** öffnen. Beim ersten Mal kommt die Tastatur für
   die Server-Adresse, vorbelegt mit `https://` (Tasten `.de`, `.com`, `.net`,
   `del`, `done`). Beispiel: `https://tv.example.de` oder
   `https://example.de/tv`. Später änderbar: TV → Reiter *Setup*, oder
   Settings → *tv server*.
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

## Tests (Entwicklung)

```sh
python3 -m venv /tmp/venv && /tmp/venv/bin/pip install -r requirements-dev.txt
/tmp/venv/bin/python -m pytest          # im Ordner MiniArcadeServer
python3 tests/make_golden.py            # nur nach Änderungen am Format
```

Die Tests laden nichts von YouTube (yt-dlp wird durch `tests/fake_ytdlp.py`
ersetzt). `tests/golden.bin` prüft auch der Simulator der Konsole.
