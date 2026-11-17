"""Skaitikliai ir žurnalas atmintyje /admin puslapiui.

RING – paskutinės 2000 žurnalo eilučių (tos pačios eilutės patenka ir į `docker compose logs`). Kelių procesų
režime (cluster.py) kiekviena api kopija savo skaitiklius ir naujas eilutes siunčia į core, o /admin rodo visų
procesų sumą. Po kiekvieno perkrovimo skaitikliai pradedami nuo nulio; ilgalaikė istorija saugoma bazėje (devices)."""
import logging
import os
import socket
import threading
import time
from collections import deque

from . import config

PROC_ID = f"{config.ROLE}-{socket.gethostname()[:12]}-{os.getpid()}"   # unikalus proceso vardas statistikai
STARTED = time.time()


class Ring(logging.Handler):
    """Žurnalo įrašai atmintyje (žiedinis buferis), kad /admin puslapis galėtų juos rodyti."""

    def __init__(self, keep: int):
        super().__init__(logging.INFO)
        self.buf: deque[dict] = deque(maxlen=keep)
        self.n = 0                               # visų įrašų numeris; puslapis prašo tik naujesnių nei turi
        self.lk = threading.Lock()

    def add(self, t: float, lvl: str, src: str, msg: str) -> None:
        with self.lk:
            self.n += 1
            self.buf.append({"id": self.n, "t": round(t, 1), "lvl": lvl[:8], "src": src[:40], "msg": msg[:600]})

    def emit(self, r: logging.LogRecord) -> None:
        try:
            msg = r.getMessage()
        except Exception:
            msg = str(r.msg)
        self.add(r.created, r.levelname, r.name.replace("aerobox.", ""), msg)

    def since(self, after: int, level: str = "INFO", limit: int = 300) -> dict:
        """Įrašai, kurių numeris didesnis nei after, ne žemesnio nei level lygio."""
        order = {"INFO": 0, "WARNING": 1, "ERROR": 2, "CRITICAL": 2}
        lv = order.get(level.upper(), 0)
        with self.lk:
            rows = [r for r in self.buf if r["id"] > after and order.get(r["lvl"], 0) >= lv]
            return {"logs": rows[-max(1, min(1500, limit)):], "last": self.n}

    def last_hour(self) -> dict:
        now = time.time()
        with self.lk:
            return {"warnings": sum(1 for r in self.buf if r["lvl"] == "WARNING" and now - r["t"] < 3600),
                    "errors": sum(1 for r in self.buf if r["lvl"] in ("ERROR", "CRITICAL") and now - r["t"] < 3600)}


RING = Ring(2000)

_lock = threading.Lock()
_req_times: deque[float] = deque()                       # ekranų užklausų laikai per paskutinę minutę
_lat: deque[tuple[float, float, int]] = deque()          # (laikas, trukmė, HTTP kodas) per paskutinę minutę
codes_total: dict[str, int] = {}                         # atsakymų skaičius pagal klasę (2xx, 4xx …) nuo paleidimo
upstream = {"adsbdb": 0, "weather": 0}                   # užklausos kitiems šaltiniams (ne lėktuvų tiekėjui)
downloads: dict[str, int] = {}                           # programos parsisiuntimai pagal versiją
rejected = {"bad_key": 0, "rate_limited": 0, "bad_admin": 0}


def count_device_request() -> None:
    now = time.time()
    with _lock:
        _req_times.append(now)
        while _req_times and now - _req_times[0] > 60:
            _req_times.popleft()


def count_response(took: float, status: int) -> None:
    now = time.time()
    with _lock:
        _lat.append((now, took, status))
        while _lat and now - _lat[0][0] > 60:
            _lat.popleft()
        cls = f"{status // 100}xx"
        codes_total[cls] = codes_total.get(cls, 0) + 1


def local(cache_size: int = 0) -> dict:
    """Šio proceso skaitikliai, atsakymų laikų procentiliai ir (jei įdiegtas psutil) atminties ir CPU naudojimas."""
    now = time.time()
    with _lock:
        while _req_times and now - _req_times[0] > 60:
            _req_times.popleft()
        rpm = len(_req_times)
        rows = [r for r in _lat if now - r[0] <= 60]
        ct = dict(codes_total)
    lat = sorted(d for _, d, _ in rows)
    ms = (lambda p: round(lat[min(len(lat) - 1, int(len(lat) * p))] * 1000, 1) if lat else 0)
    codes: dict[str, int] = {}
    for _, _, c in rows:
        codes[f"{c // 100}xx"] = codes.get(f"{c // 100}xx", 0) + 1
    out = {"id": PROC_ID, "t": now, "started": int(STARTED), "rpm": rpm, "codes_min": codes, "codes_total": ct,
           "ms": {"p50": ms(0.5), "p95": ms(0.95), "p99": ms(0.99)}, "upstream": dict(upstream),
           "downloads": dict(downloads), "rejected": dict(rejected), "cache": cache_size}
    try:
        import psutil
        p = psutil.Process()
        with p.oneshot():
            out.update(rss_mb=round(p.memory_info().rss / 1048576, 1), cpu_pct=p.cpu_percent(interval=None),
                       threads=p.num_threads())
        out.update(host_mem_used_pct=psutil.virtual_memory().percent, host_cpu_pct=psutil.cpu_percent(interval=None),
                   disk_free_gb=round(psutil.disk_usage(os.path.abspath(".")).free / 1e9, 1))
    except Exception:
        pass
    return out


def _sum(dicts: list[dict]) -> dict:
    """Žodynų suma pagal raktus."""
    out: dict = {}
    for d in dicts:
        for k, v in (d or {}).items():
            out[k] = out.get(k, 0) + v
    return out


def aggregate(procs: list[dict]) -> dict:
    """Visų procesų skaitiklių suma tokiu pavidalu, kokio laukia /admin puslapis."""
    procs = [p for p in procs if p]
    first = min((p["started"] for p in procs), default=int(STARTED))
    g = lambda k: [p[k] for p in procs if p.get(k) is not None]
    return {
        "server": {"version": config.VERSION, "role": config.ROLE, "processes": len(procs),
                   "uptime_s": int(time.time() - first), "started": first,
                   "rss_mb": round(sum(g("rss_mb")), 1) if g("rss_mb") else None,
                   "cpu_pct": round(sum(g("cpu_pct")), 1) if g("cpu_pct") else None,
                   "threads": sum(g("threads")) or None,
                   "host_mem_used_pct": max(g("host_mem_used_pct"), default=None),
                   "host_cpu_pct": max(g("host_cpu_pct"), default=None),
                   "disk_free_gb": min(g("disk_free_gb"), default=None)},
        "device_requests_last_min": sum(p["rpm"] for p in procs),
        "responses_last_min": _sum([p["codes_min"] for p in procs]),
        "responses_total": _sum([p["codes_total"] for p in procs]),
        # Kai procesų keli, rodomas lėčiausio proceso laikas (tai ne tikslus bendras procentilis)
        "server_ms_last_min": {k: max((p["ms"][k] for p in procs), default=0) for k in ("p50", "p95", "p99")},
        "other_upstream_calls": _sum([p["upstream"] for p in procs]) or dict(upstream),
        "rejected": _sum([p["rejected"] for p in procs]),
        "memory_cache": sum(p.get("cache", 0) for p in procs),
        "updates_downloads": _sum([p["downloads"] for p in procs]),
    }
