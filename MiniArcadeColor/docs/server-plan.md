# Plan: own server (not built yet)

Requirements from the owner, to keep when it gets built:

- Always runs as **Docker** (`docker compose up -d`), one container for the
  server program (Python + ffmpeg + yt-dlp), updated automatically.
- A **wildcard certificate** sits in front already (the owner's reverse
  proxy), so the container itself speaks plain HTTP behind it - no Caddy, no
  own certificate handling. Address and port are given by the owner when it
  is built.
- Hardware: Hetzner, i9-9900K, about 8 GB free.

Parts, one program:

1. **TV**: a phone page to paste a link -> the console plays it. Server
   scales any resolution down, own tile format (8x8 tiles: skip / one colour /
   two colours + mask / 16 colours, palette per scene), timestamps so the
   video always runs at 1x (frames dropped, never slow motion), console
   reports what it manages. Modes: 160x120 doubled (~25-30 fps on the C3) and
   320x240 (~10-20 fps). No sound for now.
2. **Online multiplayer**: lobby + relay for the existing link protocol over
   the internet (friends by console code), turn-based games first, then the
   real-time ones; Skat for three people; correspondence chess.
3. Online high score lists, cloud backup of saves.

Access by a long token per console (set on the console once).
