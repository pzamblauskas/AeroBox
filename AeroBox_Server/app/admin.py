"""Valdymas (administratorius). Be teisingo X-Admin-Token visi šie adresai atsako 404.

  GET  /admin                          valdymo puslapis (admin_page.html; raktas įvedamas pačiame puslapyje)
  GET  /v1/stats                       visa būsena: ekranai, užklausos, tiekėjas, procesai, klaidos
  GET  /v1/admin/logs?after=&level=    žurnalas (paskutinės 2000 eilučių iš visų procesų)
  GET  /v1/admin/devices?q=            ekranų paieška pagal ID arba MAC
  POST /v1/admin/devices/block?id=&blocked=1|0

Programos atnaujinimų valdymas yra updates.py."""
import logging
import os

from fastapi import APIRouter, Query, Request
from fastapi.responses import HTMLResponse

from . import auth, cluster, config, db, devices, metrics, updates
from .feed import feed

router = APIRouter()
log = logging.getLogger("aerobox.admin")
PAGE = open(os.path.join(os.path.dirname(__file__), "admin_page.html"), encoding="utf-8").read()


@router.get("/admin", response_class=HTMLResponse)
def page():
    # Pats puslapis viešas (jame nėra duomenų); duomenis jis gauna tik su raktu iš /v1/stats ir /v1/admin/*
    return PAGE


def _cluster() -> dict:
    """Rinkėjo būsena ir visų procesų skaitikliai.
    api procese duomenys imami iš core; jei core neatsako – grąžinami tik šio proceso duomenys."""
    if config.ROLE == "api":
        try:
            return cluster.core_get("/internal/stats")
        except Exception:
            return {"feed": {**feed.stats(), "core_down": True}, "procs": [metrics.local(devices.cache_size())],
                    "log_last_hour": metrics.RING.last_hour()}
    return {"feed": feed.stats(), "procs": [metrics.local(devices.cache_size())],
            "log_last_hour": metrics.RING.last_hour()}


@router.get("/v1/stats")
def stats(req: Request):
    auth.require_admin(req)
    cs = _cluster()
    agg = metrics.aggregate(cs["procs"])
    return {**db.stats(), **agg, "feed": cs["feed"], "log_last_hour": cs["log_last_hour"],
            "updates": {"releases": len(updates.releases()), "downloads_since_start": agg["updates_downloads"]},
            "security": {"admin_ip_limit": bool(config.ADMIN_ALLOW_IPS), "licensed_data": config.PROVIDER == "licensed"}}


@router.get("/v1/admin/logs")
def logs(req: Request, after: int = 0, level: str = Query("INFO", max_length=10), limit: int = Query(300, le=1500)):
    auth.require_admin(req)
    if config.ROLE == "api":
        try:
            return cluster.core_get("/internal/logs", after=after, level=level, limit=limit)
        except Exception:
            pass                                 # core neatsako – rodome bent šio proceso žurnalą
    return metrics.RING.since(after, level, limit)


@router.get("/v1/admin/devices")
def device_list(req: Request, q: str = Query("", max_length=40), limit: int = Query(100, le=500)):
    auth.require_admin(req)
    return {"devices": db.devices_list(q, limit)}


@router.post("/v1/admin/devices/block")
def device_block(req: Request, id: str = Query(..., max_length=40), blocked: int = 1):
    auth.require_admin(req)
    n = db.set_blocked(id, bool(blocked))
    log.warning("skydelis %s %s", id, "uzblokuotas" if blocked else "atblokuotas")
    return {"ok": bool(n)}
