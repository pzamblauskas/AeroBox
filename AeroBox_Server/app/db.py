"""Duomenų bazė (PostgreSQL; automatiniai testai naudoja SQLite). Lentelės:

  devices     ekranai: ID, rakto SHA-256 santrauka (pats raktas nesaugomas), įrenginio numeris (MAC),
              paskutinio kontakto laikas, programos versija, užklausų skaičius, ar užblokuotas
  releases    programos versijos atnaujinimams (žr. updates.py)
  http_cache  išorinių šaltinių atsakymai (maršrutai), kad to paties nereikėtų klausti kelis kartus

Ekrano veikla (paskutinis kontaktas, užklausų skaičius) kaupiama atmintyje ir kas 30 s įrašoma į bazę viena
transakcija (flush). Taip įprasta ekrano užklausa į bazę nerašo visai."""
import hashlib
import logging
import secrets
import threading
import time

from sqlalchemy import create_engine, event, text

from . import config

log = logging.getLogger("aerobox.db")

_sqlite = config.DATABASE_URL.startswith("sqlite")      # SQLite naudoja tik automatiniai testai
engine = create_engine(config.DATABASE_URL, pool_pre_ping=True, future=True,
                       **({} if _sqlite else {"pool_size": 10, "max_overflow": 20, "pool_recycle": 1800}))
if _sqlite:
    @event.listens_for(engine, "connect")
    def _sqlite_wait(conn, _):
        # Kai bazę vienu metu naudoja kelios gijos, užimtos bazės laukiama, užuot iškart grąžinus klaidą.
        conn.execute("PRAGMA busy_timeout=10000")

SCHEMA = (
    "CREATE TABLE IF NOT EXISTS devices ("
    " id TEXT PRIMARY KEY, key_hash TEXT NOT NULL UNIQUE, hw_id TEXT UNIQUE, created_at BIGINT NOT NULL,"
    " last_seen BIGINT NOT NULL, fw TEXT, requests BIGINT NOT NULL DEFAULT 0, blocked INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS devices_seen ON devices (last_seen)",
    "CREATE TABLE IF NOT EXISTS releases ("
    " version TEXT PRIMARY KEY, variant TEXT NOT NULL, size BIGINT NOT NULL, sha256 TEXT NOT NULL,"
    " notes TEXT NOT NULL, project TEXT NOT NULL, created_at BIGINT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS http_cache ("
    " k TEXT PRIMARY KEY, status INTEGER NOT NULL, body TEXT NOT NULL, fetched_at BIGINT NOT NULL)",
)


def init() -> None:
    """Sukuria trūkstamas lenteles. Kviečiama paleidžiant; esamų duomenų nekeičia."""
    with engine.begin() as c:
        for ddl in SCHEMA:
            c.execute(text(ddl))


# ---------- Ekranai ----------

_lock = threading.Lock()
_known: dict[str, tuple[str, float]] = {}     # rakto SHA-256 → (ekrano ID, kada tikrinta bazėje)
_unknown: dict[str, float] = {}               # nežinomi raktai → kada tikrinta. Tas pats neteisingas raktas
                                              # bazėje tikrinamas ne dažniau kaip kas UNKNOWN_TTL
KNOWN_TTL = 120.0                             # po tiek sekundžių raktas vėl tikrinamas bazėje, todėl
                                              # blokavimas kituose procesuose įsigalioja per ≤ 2 min
UNKNOWN_TTL = 60.0


class Blocked(Exception):
    """Ekraną administratorius užblokavo /admin puslapyje."""


def _hash(key: str) -> str:
    return hashlib.sha256(key.encode()).hexdigest()


def norm_hw(hw: str) -> str:
    """Įrenginio numeris (MAC) kaip 12 šešioliktainių simbolių be skirtukų; netinkamas – tuščia eilutė."""
    h = "".join(ch for ch in (hw or "").lower() if ch in "0123456789abcdef")
    return h if len(h) == 12 and len(hw) <= 17 else ""


def register_device(fw: str, hw_id: str = "") -> tuple[str, str]:
    """Užregistruoja ekraną be jokių papildomų sąlygų.

    Tas pats įrenginys (pagal MAC) visada lieka tas pats įrašas: jis gauna naują raktą, o senasis nustoja galioti.
    Grąžina (ID, raktas). Raktas grąžinamas tik čia – bazėje saugoma tik jo santrauka."""
    hw = norm_hw(hw_id)
    key = secrets.token_urlsafe(24)
    now = int(time.time())
    with engine.begin() as c:
        row = c.execute(text("SELECT id, key_hash, blocked FROM devices WHERE hw_id = :h"),
                        {"h": hw}).first() if hw else None
        if row and row[2]:
            raise Blocked()
        if row:
            dev_id, old = row[0], row[1]
            c.execute(text("UPDATE devices SET key_hash = :k, fw = :fw, last_seen = :t WHERE id = :i"),
                      {"k": _hash(key), "fw": fw, "t": now, "i": dev_id})
        else:
            dev_id, old = "ab_" + secrets.token_hex(6), None
            c.execute(text("INSERT INTO devices (id, key_hash, hw_id, created_at, last_seen, fw)"
                           " VALUES (:i, :k, :h, :t, :t, :fw)"),
                      {"i": dev_id, "k": _hash(key), "h": hw or None, "t": now, "fw": fw})
    with _lock:
        _known.pop(old or "", None)           # senasis raktas šiame procese nebegalioja iškart
        _known[_hash(key)] = (dev_id, time.time())
        _unknown.pop(_hash(key), None)
    return dev_id, key


def device_for_key(key: str) -> str | None:
    """Ekrano ID pagal raktą; None – raktas nežinomas arba ekranas užblokuotas.

    Dažniausiai atsakoma iš atminties (_known / _unknown); į bazę kreipiamasi tik pasibaigus jų galiojimui."""
    if not key or len(key) > 64:
        return None
    h, now = _hash(key), time.time()
    with _lock:
        hit = _known.get(h)
        if hit and now - hit[1] < KNOWN_TTL:
            return hit[0]
        if now - _unknown.get(h, 0) < UNKNOWN_TTL:
            return None
    with engine.connect() as c:
        row = c.execute(text("SELECT id, blocked FROM devices WHERE key_hash = :h"), {"h": h}).first()
    with _lock:
        if not row or row[1]:
            _known.pop(h, None)
            if len(_unknown) > 50000:         # apsauga nuo atminties augimo, jei kas nors siunčia atsitiktinius raktus
                _unknown.clear()
            _unknown[h] = now
            return None
        _known[h] = (row[0], now)
    return row[0]


_activity: dict[str, list] = {}               # ekrano ID → [paskutinis kontaktas, užklausų skaičius, versija]


def touch(dev_id: str, fw: str | None) -> None:
    """Pažymi ekrano užklausą (tik atmintyje; į bazę įrašo flush())."""
    with _lock:
        a = _activity.get(dev_id)
        if a is None:
            _activity[dev_id] = [int(time.time()), 1, fw]
        else:
            a[0], a[1], a[2] = int(time.time()), a[1] + 1, fw or a[2]


def flush() -> None:
    """Sukauptą ekranų veiklą įrašo į bazę viena transakcija."""
    with _lock:
        rows = [{"i": k, "t": v[0], "n": v[1], "fw": v[2]} for k, v in _activity.items()]
        _activity.clear()
    if rows:
        with engine.begin() as c:
            c.execute(text("UPDATE devices SET last_seen = :t, requests = requests + :n, fw = COALESCE(:fw, fw)"
                           " WHERE id = :i"), rows)


def start_flusher() -> None:
    """Foninė gija, kuri kas 30 s kviečia flush()."""
    def run():
        while True:
            time.sleep(30)
            try:
                flush()
            except Exception:
                log.exception("skydeliu veiklos irasymas")
    threading.Thread(target=run, name="db-flush", daemon=True).start()


def set_blocked(ident: str, blocked: bool) -> int:
    """Užblokuoja arba atblokuoja ekraną. ident – ekrano ID (ab_…) arba MAC.

    Šiame procese pakeitimas įsigalioja iškart, kituose – per KNOWN_TTL. Grąžina pakeistų įrašų skaičių."""
    with engine.begin() as c:
        n = c.execute(text("UPDATE devices SET blocked = :b WHERE id = :i OR (:h <> '' AND hw_id = :h)"),
                      {"b": int(blocked), "i": ident.strip(), "h": norm_hw(ident)}).rowcount
    with _lock:
        _known.clear()
    return n


def devices_list(q: str = "", limit: int = 100) -> list[dict]:
    """Ekranų paieška /admin puslapiui pagal ID arba MAC dalį; naujausiai matyti – pirmi."""
    with engine.connect() as c:
        rows = c.execute(text("SELECT id, hw_id, fw, created_at, last_seen, requests, blocked FROM devices"
                              " WHERE :q = '' OR LOWER(id) LIKE :l OR LOWER(COALESCE(hw_id, '')) LIKE :l"
                              " ORDER BY last_seen DESC LIMIT :n"),
                         {"q": q.strip(), "l": f"%{q.strip().lower()}%", "n": max(1, min(500, limit))}).all()
    return [dict(r._mapping) for r in rows]


def stats() -> dict:
    """Ekranų ir bazės suvestinė /admin puslapiui."""
    now = int(time.time())
    with engine.connect() as c:
        def count(sql: str, **p) -> int:
            return c.execute(text(sql), p).scalar() or 0
        return {
            "devices": count("SELECT COUNT(*) FROM devices"),
            "active_5min": count("SELECT COUNT(*) FROM devices WHERE last_seen > :t", t=now - 330),
            "active_1h": count("SELECT COUNT(*) FROM devices WHERE last_seen > :t", t=now - 3600),
            "active_24h": count("SELECT COUNT(*) FROM devices WHERE last_seen > :t", t=now - 86400),
            "blocked": count("SELECT COUNT(*) FROM devices WHERE blocked <> 0"),
            "cached_responses": count("SELECT COUNT(*) FROM http_cache"),
            "firmware": {str(f or "?"): n for f, n in c.execute(text("SELECT fw, COUNT(*) FROM devices GROUP BY fw"))},
            "db_kind": engine.dialect.name,
            "db_bytes": None if _sqlite else count("SELECT pg_database_size(current_database())"),
        }


# ---------- Išorinių atsakymų talpykla ----------

def cache_get(k: str, max_age: int) -> tuple[int, str] | None:
    """(HTTP kodas, atsakymas), jei įrašas ne senesnis nei max_age sekundžių; kitaip None."""
    with engine.connect() as c:
        row = c.execute(text("SELECT status, body, fetched_at FROM http_cache WHERE k = :k"), {"k": k}).first()
    return (row[0], row[1]) if row and time.time() - row[2] <= max_age else None


def cache_put(k: str, status: int, body: str) -> None:
    with engine.begin() as c:
        c.execute(text("INSERT INTO http_cache (k, status, body, fetched_at) VALUES (:k, :s, :b, :t)"
                       " ON CONFLICT (k) DO UPDATE SET status = :s, body = :b, fetched_at = :t"),
                  {"k": k, "s": status, "b": body[:20000], "t": int(time.time())})


def cache_purge(older_than_days: int = 30) -> int:
    """Ištrina senesnius nei older_than_days įrašus; grąžina ištrintų skaičių."""
    with engine.begin() as c:
        return c.execute(text("DELETE FROM http_cache WHERE fetched_at < :t"),
                         {"t": int(time.time()) - older_than_days * 86400}).rowcount
