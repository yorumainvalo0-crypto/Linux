#!/bin/sh
# yt-dlp up to date at every start (YouTube changes often); the server
# updates it again every YTDLP_UPDATE_HOURS. Offline: the installed one is used.
if [ "${YTDLP_UPDATE_HOURS:-24}" != "0" ]; then
  timeout 120 pip install -q -U --no-cache-dir "yt-dlp[default]" || echo "yt-dlp update failed - using the installed version"
fi
exec python -m tvserver
