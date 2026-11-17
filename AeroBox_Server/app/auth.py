"""Kas kreipiasi į serverį.

  device(req)         ekranas: antraštė X-Device-Key → ekrano ID (kitaip 401)
  require_admin(req)  administratorius: antraštė X-Admin-Token (kitaip 404, lyg adreso nebūtų)

Kliento IP adresą iš X-Forwarded-For nustato uvicorn (--proxy-headers), bet tik tada, kai užklausa ateina iš
Caddy tinklo (FORWARDED_ALLOW_IPS docker-compose.yml faile). Todėl čia šių antraščių neskaitome: iš interneto
atėjusią antraštę galima suklastoti."""
import hmac
import logging
import re
import threading
import time

from fastapi import HTTPException, Request

from . import config, db, metrics

log = logging.getLogger("aerobox.auth")
_lock = threading.Lock()
_buckets: dict[str, tuple[float, float]] = {}   # ekrano ID → (likęs užklausų kiekis, kada skaičiuota)
_bad_admin: dict[str, list[float]] = {}          # IP → klaidingų administratoriaus bandymų laikai
_UA_VERSION = re.compile(r"AeroBox/([0-9A-Za-z.\-_]{1,24})")   # programos versija iš User-Agent


def client_ip(req: Request) -> str:
    return req.client.host if req.client else "?"


def device(req: Request) -> str:
    """Ekrano ID pagal jo raktą. Nežinomas raktas → 401; gavęs 401, ekranas persiregistruoja pats."""
    metrics.count_device_request()
    dev = db.device_for_key(req.headers.get("x-device-key", ""))
    if not dev:
        metrics.rejected["bad_key"] += 1
        raise HTTPException(401, "unknown device")
    # Apsauga nuo sugedusio ekrano („token bucket“, žr. config.DEVICE_RATE): ribojamas tik šis ekranas
    now = time.time()
    with _lock:
        left, last = _buckets.get(dev, (float(config.DEVICE_BURST), now))
        left = min(float(config.DEVICE_BURST), left + (now - last) * config.DEVICE_RATE)
        _buckets[dev] = (left - 1 if left >= 1 else left, now)
    if left < 1:
        metrics.rejected["rate_limited"] += 1
        raise HTTPException(429, "too many requests", headers={"Retry-After": "10"})
    m = _UA_VERSION.match(req.headers.get("user-agent") or "")
    db.touch(dev, m.group(1) if m else None)
    return dev


def is_admin(req: Request) -> bool:
    """Ar užklausa turi teisingą administratoriaus raktą ir ateina iš leidžiamo IP.
    Po ADMIN_BAD_TRIES klaidingų bandymų per 15 min tas IP gauna 429."""
    ip = client_ip(req)
    if not config.ip_allowed(ip, config.ADMIN_ALLOW_IPS):
        return False
    now = time.time()
    with _lock:
        tries = [t for t in _bad_admin.get(ip, ()) if now - t < 900]
    if len(tries) >= config.ADMIN_BAD_TRIES:
        raise HTTPException(429, "too many attempts", headers={"Retry-After": "900"})
    token = req.headers.get("x-admin-token", "")
    if hmac.compare_digest(token.encode(), config.ADMIN_TOKEN.encode()):   # pastovaus laiko palyginimas
        return True
    if token:
        with _lock:
            _bad_admin[ip] = tries + [now]
        metrics.rejected["bad_admin"] += 1
        log.warning("neteisingas administratoriaus raktas is %s", ip)
    return False


def require_admin(req: Request) -> None:
    if not is_admin(req):
        raise HTTPException(404)


def cleanup(now: float) -> None:
    """Kviečiama kas minutę (main.py): pašalina senus įrašus, kad atmintis neaugtų."""
    with _lock:
        for k in [k for k, v in _buckets.items() if now - v[1] > 900]:
            del _buckets[k]
        for k in [k for k, v in _bad_admin.items() if not v or now - v[-1] > 900]:
            del _bad_admin[k]
