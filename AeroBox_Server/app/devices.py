"""Adresai, kuriais kreipiasi ekranai.

Atsakymų formatai tokie patys kaip adsb.fi, adsbdb ir Open-Meteo. Todėl ekrano programa su serveriu ir su
tiesioginiais šaltiniais kalba vienodai; serveriui ji tik prideda antraštę X-Device-Key.

  POST /v1/register?fw=..&id=<MAC>               → {"id", "key"}; raktas grąžinamas tik šį kartą
  GET  /api/v2/lat/{lat}/lon/{lon}/dist/{nm}     artimiausi lėktuvai (iš feed.py nuotraukos)
  GET  /api/v2/{hex|callsign|registration}/{x}   vienas lėktuvas (sekamas skrydis)
  GET  /v0/{callsign|airline}/{x}                maršrutas / aviakompanija (adsbdb), laikoma duomenų bazėje
  GET  /v1/forecast?latitude=..&longitude=..     orai (Open-Meteo), 15 min talpykla
  GET  /v1/geo                                   apytikslė vieta pagal IP (GeoLite2), be rakto

Atsakymas 503 reiškia „duomenų šiuo metu nėra“: ekranas tada laikinai ima duomenis tiesiai iš šaltinių."""
import ipaddress
import json
import logging
import os
import re
import threading
import time

import httpx
from fastapi import APIRouter, HTTPException, Query, Request
from fastapi.responses import Response

from . import auth, config, db, metrics
from .feed import IDENT_KINDS, feed

log = logging.getLogger("aerobox.devices")
router = APIRouter()
http = httpx.Client(timeout=8.0, headers={"User-Agent": config.USER_AGENT}, follow_redirects=False)

AREA_TTL = 2.0              # s – tos pačios vietos ekranai gauna tą patį jau paruoštą atsakymą
WEATHER_TTL = 900           # s
ROUTE_OK_TTL = 7 * 86400    # s – rastas maršrutas
ROUTE_MISS_TTL = 86400      # s – atsakymas „nerasta“ (404)
ROUTE_SPACING = 0.5         # s tarp užklausų maršrutų šaltiniui (bendra visiems ekranams)


def _json(body: str, status: int = 200) -> Response:
    return Response(body, status_code=status, media_type="application/json", headers={"Cache-Control": "no-store"})


def _no_data() -> HTTPException:
    return HTTPException(503, "no data", headers={"Retry-After": "30"})


# ---------- Talpykla atmintyje ----------

_cache: dict[str, tuple[float, str]] = {}        # raktas → (gavimo laikas, atsakymas)
_key_locks: dict[str, threading.Lock] = {}       # atskiras užraktas kiekvienam talpyklos raktui
_klock = threading.Lock()


def _key_lock(key: str) -> threading.Lock:
    with _klock:
        return _key_locks.setdefault(key, threading.Lock())


def cached(key: str, ttl: float, fetch) -> tuple[float, str] | None:
    """Vienam raktui – viena užklausa: kitos tuo metu atėjusios užklausos laukia ir gauna tą patį atsakymą.

    Jei fetch() nepavyksta, grąžinamas ankstesnis atsakymas (ne senesnis nei 1 val.) – geriau senas nei jokio.
    Grąžina (gavimo laikas, atsakymas) arba None."""
    hit = _cache.get(key)
    if hit and time.time() - hit[0] <= ttl:
        return hit
    with _key_lock(key):
        hit = _cache.get(key)
        if hit and time.time() - hit[0] <= ttl:
            return hit
        value = fetch()
        if value is not None:
            hit = _cache[key] = (time.time(), value)
        return hit if hit and time.time() - hit[0] < 3600 else None


def cache_size() -> int:
    return len(_cache)


def cleanup(now: float) -> None:
    """Kviečiama kas minutę (main.py): pašalina senesnius nei 1 val. atsakymus ir nebereikalingus užraktus."""
    for k in [k for k, v in list(_cache.items()) if now - v[0] > 3600]:
        _cache.pop(k, None)
    with _klock:
        for k in [k for k, lk in _key_locks.items() if k not in _cache and not lk.locked()]:
            del _key_locks[k]
    for k in [k for k, v in list(_routes.items()) if now - v[0] > 3600]:
        _routes.pop(k, None)


# ---------- Registracija ----------

@router.post("/v1/register")
def register(req: Request, fw: str = Query("", max_length=24), id: str = Query("", max_length=17)):
    if fw and not re.fullmatch(r"[0-9A-Za-z.\-_]{1,24}", fw):
        raise HTTPException(400, "bad version")
    try:
        dev_id, key = db.register_device(fw, id)
    except db.Blocked:
        raise HTTPException(403, "device blocked")
    log.info("uzregistruotas skydelis %s (fw %s) is %s", dev_id, fw or "?", auth.client_ip(req))
    return {"id": dev_id, "key": key}


# ---------- Lėktuvai ----------

@router.get("/api/v2/lat/{lat}/lon/{lon}/dist/{dist}")
def area(req: Request, lat: float, lon: float, dist: int):
    auth.device(req)
    if not (-90 <= lat <= 90 and -180 <= lon <= 180 and 1 <= dist <= 250):
        raise HTTPException(400, "bad coordinates")

    def build():
        ac = feed.area(lat, lon, dist)
        if ac is None:
            return None
        return json.dumps({"aircraft": ac, "now": int(time.time() * 1000), "total": len(ac)}, separators=(",", ":"))
    # Raktas apvalinamas iki ~1 km, todėl to paties miesto ekranai dalijasi atsakymu
    hit = cached(f"area:{lat:.2f}:{lon:.2f}:{dist}", AREA_TTL, build)
    if not hit or time.time() - hit[0] > 30:
        raise _no_data()
    return _json(hit[1])


@router.get("/api/v2/{kind}/{ident}")
def one(req: Request, kind: str, ident: str):
    auth.device(req)
    if kind not in IDENT_KINDS or not re.fullmatch(r"[0-9A-Za-z\-]{2,12}", ident):
        raise HTTPException(400, "bad request")
    ac = feed.one(kind, ident)
    if ac is None:
        raise _no_data()
    return _json(json.dumps({"ac": ac, "now": int(time.time() * 1000), "total": len(ac)}, separators=(",", ":")))


# ---------- Maršrutai ir aviakompanijos (adsbdb) ----------

_routes: dict[str, tuple[float, tuple[int, str]]] = {}   # greita kopija atmintyje (1 val.), kad nereikėtų bazės
_route_lock = threading.Lock()
_route_last = 0.0                                        # kada suplanuota paskutinė užklausa šaltiniui


def _route_known(k: str) -> tuple[int, str] | None:
    """Jau žinomas atsakymas: iš atminties arba iš bazės. Rastas maršrutas galioja 7 d., „nerasta“ – 1 d."""
    m = _routes.get(k)
    if m and time.time() - m[0] < 3600:
        return m[1]
    row = db.cache_get(k, ROUTE_OK_TTL)
    if row and (row[0] == 200 or db.cache_get(k, ROUTE_MISS_TTL)):
        if len(_routes) > 50000:
            _routes.clear()
        _routes[k] = (time.time(), row)
        return row
    return None


@router.get("/v0/{kind}/{ident}")
def route(req: Request, kind: str, ident: str):
    global _route_last
    auth.device(req)
    if kind not in ("callsign", "airline") or not re.fullmatch(r"[0-9A-Za-z]{2,8}", ident):
        raise HTTPException(400, "bad request")
    k = f"adsbdb:{kind}:{ident.upper()}"
    row = _route_known(k)
    if row:
        return _json(row[1], row[0])
    with _key_lock(k):                                    # tam pačiam skrydžiui – viena užklausa visiems
        row = _route_known(k)                             # kol laukėme, kita užklausa galėjo jau parsiųsti
        if row:
            return _json(row[1], row[0])
        with _route_lock:                                 # šaltinio neperkrauname: užklausos eilėje su tarpais
            wait = _route_last + ROUTE_SPACING - time.time()
            if wait > 2.0:                                # eilė per ilga – ekranas paklaus vėliau
                raise HTTPException(503, "busy", headers={"Retry-After": "5"})
            _route_last = max(time.time(), _route_last + ROUTE_SPACING)
        if wait > 0:
            time.sleep(wait)
        metrics.upstream["adsbdb"] += 1
        try:
            r = http.get(f"{config.ROUTES_URL}/{kind}/{ident}")
            if r.status_code in (200, 404) and len(r.content) < 20000:
                db.cache_put(k, r.status_code, r.text)
                return _json(r.text, r.status_code)
        except httpx.HTTPError as e:
            log.warning("marsrutu saltinis: %s", type(e).__name__)
    old = db.cache_get(k, ROUTE_OK_TTL)                   # šaltinis neatsakė – grąžiname ankstesnį atsakymą
    if old:
        return _json(old[1], old[0])
    raise HTTPException(502, "route source error")


# ---------- Orai ----------

@router.get("/v1/forecast")
def forecast(req: Request, latitude: float, longitude: float, current: str = Query("", max_length=120)):
    auth.device(req)
    if not (-90 <= latitude <= 90 and -180 <= longitude <= 180) or not re.fullmatch(r"[a-z0-9_,]{0,120}", current):
        raise HTTPException(400, "bad request")
    lat, lon = round(latitude, 1), round(longitude, 1)    # ~10 km tikslumas: vienam miestui – viena užklausa

    def fetch():
        metrics.upstream["weather"] += 1
        params = {"latitude": f"{lat:.1f}", "longitude": f"{lon:.1f}", "current": current}
        if config.WEATHER_KEY:
            params["apikey"] = config.WEATHER_KEY
        try:
            r = http.get(config.WEATHER_URL, params=params)
            return r.text if r.status_code == 200 and len(r.content) < 20000 else None
        except httpx.HTTPError as e:
            log.warning("oru saltinis: %s", type(e).__name__)
            return None
    hit = cached(f"wx:{lat}:{lon}:{current}", WEATHER_TTL, fetch)
    if not hit:
        raise HTTPException(502, "weather source not answering")
    return _json(hit[1])


# ---------- Vieta pagal IP (pirmajam įjungimui) ----------

try:
    import maxminddb
    _geoip = maxminddb.open_database(config.GEOIP_DB) if os.path.exists(config.GEOIP_DB) else None
except Exception:
    _geoip = None


def geoip_ready() -> bool:
    return _geoip is not None


@router.get("/v1/geo")
def geo(req: Request):
    """Apytikslė ekrano vieta pagal IP; rakto nereikia. Ekranas jos klausia pirmą kartą įjungtas, kol
    savininkas dar nenurodė vietos. IP niekur neišsaugomas – paieška vyksta vietiniame GeoLite2 faile."""
    metrics.count_device_request()
    rec = None
    try:
        ip = auth.client_ip(req)
        if _geoip and ipaddress.ip_address(ip).is_global:
            rec = _geoip.get(ip)
    except ValueError:
        pass
    loc = (rec or {}).get("location") or {}
    if "latitude" not in loc:
        raise HTTPException(404, "location not found")
    return {"lat": loc["latitude"], "lon": loc["longitude"],
            "city": ((rec.get("city") or {}).get("names") or {}).get("en", ""),
            "country": (rec.get("country") or {}).get("iso_code", "")}
