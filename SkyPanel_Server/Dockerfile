# AeroBox serveris. Priklausomybės diegiamos tiksliai tų versijų, kurios įrašytos uv.lock
# (tų pačių, su kuriomis praeina testai). Du etapai: build – priklausomybės, antras – tik tai, ko reikia paleisti.
FROM python:3.13-slim AS build
COPY --from=ghcr.io/astral-sh/uv:0.8 /uv /bin/uv
WORKDIR /srv
ENV UV_COMPILE_BYTECODE=1 UV_LINK_MODE=copy
COPY pyproject.toml uv.lock ./
RUN uv sync --frozen --no-dev --no-install-project

FROM python:3.13-slim
RUN useradd --system --uid 10001 --home /srv --shell /usr/sbin/nologin aerobox \
 && mkdir -p /data/firmware && chown -R aerobox /data
WORKDIR /srv
COPY --from=build /srv/.venv /srv/.venv
COPY app app
ENV PATH="/srv/.venv/bin:$PATH" PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 \
    FIRMWARE_DIR=/data/firmware GEOIP_DB=/data/geoip/GeoLite2-City.mmdb
USER aerobox
EXPOSE 8080
HEALTHCHECK --interval=15s --timeout=4s --start-period=20s --retries=3 \
  CMD python -c "import urllib.request,os;urllib.request.urlopen('http://127.0.0.1:'+os.environ.get('PORT','8080')+'/health',timeout=3)"
# Viename konteineryje – vienas procesas. Daugiau galios – daugiau api konteinerių (API_REPLICAS .env faile).
CMD ["sh", "-c", "exec uvicorn app.main:app --host 0.0.0.0 --port ${PORT:-8080} --proxy-headers --no-access-log --timeout-keep-alive 75 --limit-concurrency 2000"]
