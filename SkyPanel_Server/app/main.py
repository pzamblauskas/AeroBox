"""AeroBox serveris – įėjimo taškas (uvicorn app.main:app).

Moduliai:
  config.py    nustatymai iš .env ir paleidimo patikra
  feed.py      lėktuvų duomenų rinkėjas ir tiekėjai (serverio šerdis)
  devices.py   adresai ekranams
  auth.py      ekrano ir administratoriaus atpažinimas
  updates.py   programos atnaujinimai
  admin.py     valdymo puslapis /admin ir jo duomenys
  cluster.py   core + api procesai (mastelio didinimas)
  db.py        duomenų bazė
  metrics.py   skaitikliai ir žurnalas /admin puslapiui

Ką daro kiekvienas vaidmuo (ROLE): all – viską; core – tik rinkėjas ir /internal/*;
api – viską, išskyrus rinkėją (duomenis gauna iš core)."""
import logging
import threading
import time
from contextlib import asynccontextmanager

import anyio
from fastapi import FastAPI, Request

from . import admin, auth, cluster, config, db, devices, metrics, updates
from .feed import feed

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s %(message)s")
logging.getLogger("httpx").setLevel(logging.WARNING)
for _name in ("", "uvicorn"):                    # visi įrašai patenka ir į /admin žurnalą
    logging.getLogger(_name).addHandler(metrics.RING)
log = logging.getLogger("aerobox")

ADMIN_CSP = ("default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
             "img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")


def _housekeeping() -> None:
    """Foninė priežiūra: kas minutę – atminties valymas; kas 5 min – suvestinė žurnale;
    kartą per parą – senų maršrutų šalinimas iš bazės."""
    minute = 0
    while True:
        time.sleep(60)
        minute += 1
        try:
            now = time.time()
            auth.cleanup(now)
            devices.cleanup(now)
            if config.ROLE != "api" and minute % 5 == 0:
                f = feed.stats()
                log.info("suvestine: %d zonu, %d lektuvu atmintyje, %d uzklausu per min tiekejui",
                         f["zones_active"], f["aircraft_in_memory"], f["upstream_calls_last_min"])
            if config.ROLE != "api" and minute % 1440 == 0:
                log.info("is talpyklos pasalinta %d senu irasu", db.cache_purge(30))
        except Exception:
            log.exception("valymas")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Paleidimas (iki yield) ir sustabdymas (po yield)."""
    db.init()
    db.start_flusher()
    if config.ROLE == "api":
        cluster.start_sync(devices.cache_size)
    else:
        feed.start()
    threading.Thread(target=_housekeeping, name="housekeeping", daemon=True).start()
    # Sinchroniniai adresai vykdomi gijų telkinyje; numatytųjų 40 gijų tūkstančiams ekranų per mažai
    anyio.to_thread.current_default_thread_limiter().total_tokens = 200
    log.info("%s serveris %s paleistas: ROLE=%s, tiekejas %s, DB %s", config.BRAND, config.VERSION, config.ROLE,
             feed.p.name, db.engine.dialect.name)
    if config.PROVIDER == "free":
        log.warning("nemokami lektuvu saltiniai: parduodamam produktui reikia licencijuoto tiekejo")
    if config.ROLE != "core" and not devices.geoip_ready():
        log.warning("GeoLite2 failo nera (%s): /v1/geo atsakys 404, skydelio vieta bus ivedama ranka",
                    config.GEOIP_DB)
    yield
    db.flush()                                   # sustabdant įrašoma dar neįrašyta ekranų veikla


app = FastAPI(title=config.BRAND, lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None)


@app.middleware("http")
async def measure_and_protect(request: Request, call_next):
    """Kiekvienai užklausai: atsakymo laikas ir kodas /admin statistikai, saugumo antraštės."""
    t0 = time.perf_counter()
    try:
        resp = await call_next(request)
    except Exception:
        log.exception("klaida %s %s", request.method, request.url.path)
        raise
    path = request.url.path
    if not path.startswith("/internal/"):
        metrics.count_response(time.perf_counter() - t0, resp.status_code)
        if resp.status_code >= 500 and resp.status_code != 503:      # 503 – įprastas „duomenų dar nėra“
            log.warning("%s %s -> %d", request.method, path, resp.status_code)
    resp.headers["X-Content-Type-Options"] = "nosniff"
    resp.headers["Referrer-Policy"] = "no-referrer"
    if path == "/admin" or path == "/v1/stats" or path.startswith("/v1/admin"):
        resp.headers["X-Frame-Options"] = "DENY"
        resp.headers["Cache-Control"] = "no-store"
        resp.headers["Content-Security-Policy"] = ADMIN_CSP
    return resp


@app.get("/health")
def health():
    """Docker ir išorinei stebėsenai: ar procesas gyvas."""
    return {"ok": True, "role": config.ROLE}


# Kurie adresai registruojami, priklauso nuo vaidmens: core iš išorės pasiekiamas tik per /internal/*
if config.ROLE == "core":
    app.include_router(cluster.router)
else:
    app.include_router(devices.router)
    app.include_router(updates.router)
    app.include_router(admin.router)
