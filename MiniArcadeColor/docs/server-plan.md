# Plan: own server

Requirements from the owner:

- Always runs as **Docker** (`docker compose up -d`), one container for the
  server program (Python + ffmpeg + yt-dlp), updated automatically.
- A **wildcard certificate** sits in front already (the owner's reverse
  proxy), so the container itself speaks plain HTTP behind it - no Caddy, no
  own certificate handling. Address and port are given by the owner when it
  is built.
- Hardware: Hetzner, i9-9900K, about 8 GB free.

Parts, one program:

1. **TV** - built (see below).
2. **Online multiplayer**: lobby + relay for the existing link protocol over
   the internet (friends by console code), turn-based games first, then the
   real-time ones; Skat for three people; correspondence chess.
3. Online high score lists, cloud backup of saves.

Access by a long token per console.

## What is built: TV (`MiniArcadeServer/`, console app "TV")

- Server: aiohttp + yt-dlp + ffmpeg + numpy in Docker (`MiniArcadeServer/`,
  German README with setup, reverse proxy, env variables). Plain HTTP on
  `PORT`, optional `BASE_PATH`. yt-dlp (with deno for YouTube's JavaScript)
  is updated at every start and every `YTDLP_UPDATE_HOURS`.
- **YouTube only, no login ever**: video ids and youtube.com / youtu.be
  links only, yt-dlp limited to `youtube,youtube:search,youtube:search_url`,
  cookie / account options refused in code.
- **Own IP kept clean (optional)**: all YouTube traffic (yt-dlp, thumbnails, video data
  through an internal relay that ffmpeg reads) goes out via `YT_PROXY`
  (http/socks5, e.g. a box at home) or `YT_SOURCE_ADDRESS`; unset = direct,
  only an info line (the owner accepts a flagged IP). Caching, a limit of
  yt-dlp calls per minute, `--sleep-requests`, one stream per console; a bot
  check / 403 / 429 stops all YouTube requests for `YT_COOLDOWN_HOURS` with a
  message on console and phone page.
- **Pairing**: one server PIN (`CONSOLE_PIN`, else generated and logged)
  typed on any console; the console gets its own long token (only its hash
  is stored, removable on the phone page). 5 wrong PINs lock pairing for 10
  minutes per address and per console code.
- **Console alone**: address keyboard for URLs ("https://", .de/.com/.net),
  connection test (`/api/ping`), PIN digit entry, search keyboard with the
  last 4 searches, all playback on the keys. Settings line "tv server". The
  phone page (password `ADMIN_PASSWORD`) is optional: search, play on a
  console, remote control, list/remove consoles, test picture.
- **Lists**: categories as week/most-viewed searches (`CATEGORIES`, "Beliebt"
  merges them), search, history and favourites per console in a JSON file
  on the volume; thumbnails 80 x 45 already as console colour indices
  (Floyd-Steinberg over the 256 colours), cached on disk with a size cap.
- **Stream**: ffmpeg (<= 480p source, any size scaled) to 160 x 120 ("fluent",
  max 25 fps) or 320 x 240 ("sharp", max 15 fps), ordered Bayer dithering in
  the 6x7x6 cube, 8 x 8 tiles: skip / fill / flat / two colours + mask / four
  colours / raw, unchanged tiles skipped, busy tiles turned into four colours
  when the quality level allows. No deflate: the ROM's tinfl links, but its
  state (~11 kB) and a whole-frame output buffer do not fit next to WLAN,
  TLS and the 75 kB frame buffer; the tiles already do most of it.
  Records `[u32 len][payload]`, info records (state, gen, pos, title) and
  frames (seq, timestamp). The console asks for segments of 0.5 s over one
  kept-alive HTTPS connection; each request carries the report (shown /
  dropped frames, buffer) and a key action. The server paces to the wall
  clock, drops late frames before encoding (never slow motion), lowers or
  raises fps and quality, resyncs with a key frame after a loss and stops
  ffmpeg after 15 s without requests.
- **Tests**: pytest (YouTube validation, palette = console colours, tile
  round trip with a Python decoder mirroring the C one, golden frames,
  pairing with PIN and lockout, auth, streaming in real time, no cookies,
  proxy for yt-dlp and video data, cool-down). Console simulator:
  `tvdecode` (golden frames through the C decoder, skip tiles, clock) and
  `tvui` (address typed, PIN, list with thumbnails, playback with keys,
  a lost connection, search, back).
- Not tested here: real YouTube, the real reverse proxy, the board's heap
  with TLS (logged on the USB console and shown under TV > Setup).
