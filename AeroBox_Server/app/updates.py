"""Programos atnaujinimai ekranams.

  POST   /v1/admin/firmware?notes=..    (administratorius) užklausos turinys – firmware.bin; versija nuskaitoma iš failo
  GET    /v1/admin/releases             (administratorius) versijų sąrašas
  DELETE /v1/admin/firmware/{version}   (administratorius) versija nebesiūloma, failas ištrinamas
  GET    /v1/update?fw=1.2.3            (ekranas) naujesnė to paties varianto versija arba {"version": ""}
  GET    /v1/firmware/{version}         (ekranas) programos failas

Variantas – versijos dalis po brūkšnelio („1.2.3-fw“ → „fw“). Ekranas gauna tik savo varianto versijas.
Ekranas tikrina failo dydį ir SHA-256, o diegia tik tada, kai savininkas savo puslapyje paspaudžia „Update“.
Failai saugomi FIRMWARE_DIR (Docker tomas „firmware“), jų aprašai – lentelėje releases."""
import hashlib
import logging
import os
import re
import struct
import time

from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import FileResponse
from sqlalchemy import text

from . import auth, config, db, metrics

router = APIRouter()
log = logging.getLogger("aerobox.updates")
MAX_SIZE = 4 * 1024 * 1024                       # ekrano OTA skirsnio dydis – 4 MB
VERSION_RE = re.compile(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,4}(-[a-z0-9]{1,8})?")


def _variant(v: str) -> str:
    return v.split("-", 1)[1] if "-" in v else ""


def _vkey(v: str) -> tuple:
    """Versija palyginimui: „1.10.2-fw“ → (1, 10, 2)."""
    return tuple(int(p) if p.isdigit() else 0 for p in v.split("-", 1)[0].split("."))


def _path(version: str) -> str:
    # version visada patikrinta VERSION_RE, todėl kelio apėjimas („../“) neįmanomas
    return os.path.join(config.FIRMWARE_DIR, version + ".bin")


def parse_app(data: bytes) -> dict:
    """Patikrina, ar tai ESP32 programos failas, ir nuskaito jo duomenis.

    Failas prasideda baitu 0xE9; po 32 baitų eina esp_app_desc_t (projekto vardas, kompiliavimo data).
    Versija imama iš programoje esančios User-Agent eilutės „AeroBox/1.2.3 (ESP32-S3)“, nes esp_app_desc_t
    laukelyje yra kompiliatoriaus PROJECT_VER, o ne ekrano programos versija."""
    if len(data) < 32 + 256 or data[0] != 0xE9:
        raise HTTPException(400, "Not an ESP32 app file. Use firmware.bin (not firmware_merged.bin).")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:   # esp_app_desc_t magic
        raise HTTPException(400, "App description not found. Use firmware.bin.")
    m = re.search(rb"AeroBox/([0-9][0-9A-Za-z.\-_]{0,22}) \(ESP32-S3\)", data)
    if not m:
        raise HTTPException(400, "Firmware version not found in the file.")
    cstr = lambda off, n: data[off:off + n].split(b"\0", 1)[0].decode("ascii", "replace")
    return {"version": m.group(1).decode(), "project": cstr(32 + 48, 32),
            "built": cstr(32 + 96, 16) + " " + cstr(32 + 80, 16)}


def releases() -> list[dict]:
    """Visos versijos; naujausia – pirma."""
    with db.engine.connect() as c:
        rows = c.execute(text("SELECT version, variant, size, sha256, notes, project, created_at FROM releases")).all()
    return sorted((dict(r._mapping) for r in rows), key=lambda r: _vkey(r["version"]), reverse=True)


@router.post("/v1/admin/firmware")
async def upload(req: Request, notes: str = ""):
    auth.require_admin(req)
    buf = bytearray()
    async for chunk in req.stream():             # skaitoma dalimis: atmintyje niekada nebūna daugiau nei MAX_SIZE
        buf += chunk
        if len(buf) > MAX_SIZE:
            raise HTTPException(413, "File too large")
    data = bytes(buf)
    info = parse_app(data)
    v = info["version"]
    if not VERSION_RE.fullmatch(v):
        raise HTTPException(400, f"Bad version in file: {v[:30]!r} (expected like 1.2.3)")
    os.makedirs(config.FIRMWARE_DIR, exist_ok=True)
    # Pirmiausia rašoma į laikiną failą, tada jis pervadinamas: ekranas niekada negaus pusiau įrašyto failo
    with open(_path(v) + ".tmp", "wb") as f:
        f.write(data)
    os.replace(_path(v) + ".tmp", _path(v))
    sha = hashlib.sha256(data).hexdigest()
    with db.engine.begin() as c:
        c.execute(text("DELETE FROM releases WHERE version = :v"), {"v": v})
        c.execute(text("INSERT INTO releases (version, variant, size, sha256, notes, project, created_at)"
                       " VALUES (:v, :var, :s, :h, :n, :p, :t)"),
                  {"v": v, "var": _variant(v), "s": len(data), "h": sha, "n": notes[:200], "p": info["project"],
                   "t": int(time.time())})
    log.info("ikelta versija %s (%.2f MB)%s", v, len(data) / 1048576, f": {notes[:80]}" if notes else "")
    return {"ok": True, "version": v, "variant": _variant(v), "size": len(data), "sha256": sha, **info}


@router.get("/v1/admin/releases")
def list_releases(req: Request):
    auth.require_admin(req)
    return {"releases": releases(), "devices": db.stats()}


@router.delete("/v1/admin/firmware/{version}")
def delete(req: Request, version: str):
    auth.require_admin(req)
    if not VERSION_RE.fullmatch(version):
        raise HTTPException(400, "bad version")
    with db.engine.begin() as c:
        n = c.execute(text("DELETE FROM releases WHERE version = :v"), {"v": version}).rowcount
    try:
        os.remove(_path(version))
    except OSError:
        pass
    log.info("pasalinta versija %s", version)
    return {"ok": bool(n)}


@router.get("/v1/update")
def check(req: Request, fw: str = ""):
    """Ekrano klausimas „ar yra naujesnė versija?“ (ekranas klausia kas 6 val.)."""
    auth.device(req)
    if len(fw) > 24:
        raise HTTPException(400, "bad version")
    for r in releases():
        if r["variant"] == _variant(fw) and _vkey(r["version"]) > _vkey(fw):
            return {"version": r["version"], "size": r["size"], "sha256": r["sha256"], "notes": r["notes"]}
    return {"version": ""}


@router.get("/v1/firmware/{version}")
def download(req: Request, version: str):
    dev = auth.device(req)
    if not VERSION_RE.fullmatch(version):
        raise HTTPException(400)
    if not os.path.exists(_path(version)):
        raise HTTPException(404)
    metrics.downloads[version] = metrics.downloads.get(version, 0) + 1
    log.info("skydelis %s parsisiuncia %s", dev, version)
    return FileResponse(_path(version), media_type="application/octet-stream")
