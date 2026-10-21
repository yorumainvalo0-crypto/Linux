# MiniArcade server: YouTube TV for the MiniArcade Color console.
# Plain HTTP on $PORT - the owner's reverse proxy holds the certificate.
FROM python:3.12-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends ffmpeg ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -m -u 1000 tv \
 && mkdir -p /opt/venv /data /app \
 && chown tv:tv /opt/venv /data /app

USER tv
# the virtual environment belongs to the server's user, so yt-dlp can update itself while running
RUN python -m venv /opt/venv
ENV PATH=/opt/venv/bin:$PATH PYTHONUNBUFFERED=1 DATA_DIR=/data PORT=8080
COPY --chown=tv:tv requirements.txt /app/
RUN pip install --no-cache-dir -r /app/requirements.txt
COPY --chown=tv:tv tvserver /app/tvserver
COPY --chown=tv:tv entrypoint.sh /app/
WORKDIR /app
VOLUME /data
EXPOSE 8080
HEALTHCHECK --interval=60s --timeout=5s --start-period=60s \
  CMD python -c "import os,urllib.request;urllib.request.urlopen('http://127.0.0.1:%s%s/health'%(os.environ.get('PORT','8080'),os.environ.get('BASE_PATH','').rstrip('/')),timeout=4)" || exit 1
ENTRYPOINT ["sh", "/app/entrypoint.sh"]
