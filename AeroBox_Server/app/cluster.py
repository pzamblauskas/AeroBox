"""Keli procesai – kai ekranų tiek daug, kad vieno proceso nebeužtenka.

  ROLE=all   viskas viename procese.
  ROLE=core  VIENAS procesas: renka lėktuvų duomenis iš tiekėjo (feed.py). Iš interneto nepasiekiamas –
             tik /internal/* adresai vidiniame Docker tinkle, su INTERNAL_TOKEN.
  ROLE=api   tiek kopijų, kiek reikia: atsako ekranams. Kas SYNC_S sekundes kiekviena kopija praneša core,
             kokių zonų ir sekamų skrydžių reikia jos ekranams, ir gauna naujus lėktuvus.
             api kopijos tiekėjo niekada neklausia, todėl užklausų tiekėjui skaičius nepriklauso nuo kopijų skaičiaus.

Jei core sustoja, api kopijos dar ~2 min atsako iš turimos nuotraukos, paskui grąžina 503, ir ekranai laikinai
ima duomenis patys. Docker core paleidžia iš naujo automatiškai (restart: unless-stopped)."""
import hmac
import logging
import threading
import time

import httpx
from fastapi import APIRouter, HTTPException, Request

from . import config, metrics
from .feed import feed

log = logging.getLogger("aerobox.cluster")
SYNC_S = 2.0            # s tarp api → core sinchronizavimų
PROC_FRESH = 15.0       # s – tiek nesusisiekusi api kopija nebeįtraukiama į statistiką

router = APIRouter()    # registruojamas tik core procese (main.py)
_procs: dict[str, dict] = {}                     # core: api kopijų skaitikliai pagal proceso ID
_plock = threading.Lock()


def _internal(req: Request) -> None:
    """Vidinių adresų apsauga: be teisingo INTERNAL_TOKEN atsakoma 404."""
    token = req.headers.get("x-internal-token", "")
    if not hmac.compare_digest(token.encode(), config.INTERNAL_TOKEN.encode()):
        raise HTTPException(404)


@router.post("/internal/sync")
async def sync(req: Request):
    """api kopija atsiunčia savo poreikius, skaitiklius ir naujas žurnalo eilutes; atgal gauna naujus lėktuvus."""
    _internal(req)
    body = await req.json()
    interest = body.get("interest") or {}
    feed.add_interest(interest.get("zones") or [], interest.get("watch") or [])
    st = body.get("stats")
    now = time.time()
    if isinstance(st, dict) and st.get("id"):
        st["t"] = now
        with _plock:
            _procs[str(st["id"])[:60]] = st
            for k in [k for k, v in _procs.items() if now - v["t"] > 120]:
                del _procs[k]
    # api kopijų žurnalas sujungiamas į core žurnalą; šaltinis pažymimas „modulis@proceso ID pabaiga“
    for r in (body.get("logs") or [])[:500]:
        metrics.RING.add(float(r.get("t", now)), str(r.get("lvl", "INFO")),
                         f"{r.get('src', '')}@{str(body.get('id', ''))[-12:]}", str(r.get("msg", "")))
    return feed.export(float(body.get("since") or 0))


@router.get("/internal/stats")
def stats(req: Request):
    _internal(req)
    now = time.time()
    with _plock:
        procs = [v for v in _procs.values() if now - v["t"] < PROC_FRESH]
    return {"feed": feed.stats(), "procs": [metrics.local()] + procs, "log_last_hour": metrics.RING.last_hour()}


@router.get("/internal/logs")
def logs(req: Request, after: int = 0, level: str = "INFO", limit: int = 300):
    _internal(req)
    return metrics.RING.since(after, level, limit)


# ---------- api kopijos pusė ----------

_http = httpx.Client(timeout=6.0, headers={"X-Internal-Token": config.INTERNAL_TOKEN})


def core_get(path: str, **params) -> dict:
    r = _http.get(config.CORE_URL + path, params=params)
    r.raise_for_status()
    return r.json()


def _sync_loop(cache_size) -> None:
    """Kas SYNC_S: išsiunčia poreikius, statistiką ir naujas žurnalo eilutes; pritaiko core atsakymą."""
    since, shipped, fails = 0.0, 0, 0     # since – nuo kada prašyti lėktuvų; shipped – paskutinė išsiųsta eilutė
    while True:
        t0 = time.time()
        try:
            new_logs = metrics.RING.since(shipped, "INFO", 500)
            r = _http.post(config.CORE_URL + "/internal/sync",
                           json={"id": metrics.PROC_ID, "since": since, "interest": feed.interest(),
                                 "stats": metrics.local(cache_size()), "logs": new_logs["logs"]})
            r.raise_for_status()
            snap = r.json()
            feed.apply(snap)
            since = float(snap.get("t", t0)) - 1.0      # 1 s atsarga; pasikartojantys lėktuvai nekenkia
            shipped = new_logs["last"]
            if fails >= 3:
                log.info("rysys su core atsistate")
            fails = 0
        except Exception as e:
            fails += 1
            if fails == 3:                               # registruojama vieną kartą, ne kas 2 s
                log.error("core nepasiekiamas: %s", type(e).__name__)
        time.sleep(max(0.2, SYNC_S - (time.time() - t0)))


def start_sync(cache_size) -> None:
    """api kopijoje paleidžia sinchronizavimo su core giją."""
    threading.Thread(target=_sync_loop, args=(cache_size,), name="core-sync", daemon=True).start()
