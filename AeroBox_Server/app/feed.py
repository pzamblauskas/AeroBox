"""Lėktuvų duomenų rinkėjas – serverio šerdis.

Pagrindinė taisyklė: ekrano užklausa niekada nekeliauja tiesiai pas tiekėją. Foninė gija (Feed.run) savo tempu
klausia tiekėjo ir atmintyje laiko vieną bendrą visų lėktuvų „nuotrauką“. Ekranui atsakoma iš jos: tik lėktuvai
jo spinduliu, ne daugiau kaip MAX_PER_DEVICE artimiausių ir tik tie laukai, kuriuos skaito ekrano programa (SLIM).
Todėl užklausų tiekėjui skaičius nepriklauso nuo ekranų skaičiaus.

Du darbo režimai (pasirenkami .env faile):
  visas pasaulis  Licencijuotas tiekėjas su PROVIDER_WORLD_URL: kas POLL_S viena užklausa visiems ekranams.
                  Pigiausias ir paprasčiausias režimas; zonos ir MAX_ZONES tada nenaudojami.
  zonos           Tiekėjas grąžina tik sritį aplink tašką (nemokami adsb.fi / adsb.lol arba licencijuotas be
                  PROVIDER_WORLD_URL). Ekranai grupuojami į ZONE_GRID laipsnių zonas: viena užklausa vienai
                  zonai, ja dalijasi visi tos zonos ekranai. Tarp užklausų laukiama ne mažiau kaip provider.spacing.

Sekamas skrydis (watch) pirmiausia ieškomas nuotraukoje. Jei jo ten nėra, rinkėjas klausia jo atskirai, bet
sekimui skiria ne daugiau kaip kas WATCH_SHARE-ąją užklausą, kad zonos nevėluotų.

Procese yra vienas Feed objektas (`feed` modulio pabaigoje). Kelių procesų režime (cluster.py) tiekėjo klausia
tik core procesas, o api procesai nuotrauką gauna iš jo per interest / add_interest / export / apply."""
import logging
import math
import threading
import time
from collections import deque

import httpx

from . import config

log = logging.getLogger("aerobox.feed")

POLL_S = 8.0            # s – po tiek zonos (ar viso pasaulio) duomenys klausiami iš naujo
WATCH_POLL_S = 10.0     # s – po tiek sekamas skrydis klausiamas iš naujo (jei jo nėra nuotraukoje)
WATCH_SHARE = 3         # sekamiems skrydžiams skiriama ne daugiau kaip kas trečia užklausa tiekėjui
FREE_SPACING = 1.1      # s tarp užklausų nemokamiems šaltiniams (jie leidžia ~1 užklausą per sekundę)
ZONE_GRID = 0.5         # zonos dydis laipsniais (~55 km); vienos zonos ekranai dalijasi užklausa
ZONE_IDLE = 120         # s – zona, kurios niekas neklausia tiek laiko, pamirštama
WATCH_IDLE = 60         # s – tas pats sekamiems skrydžiams
AC_STALE = 90           # s – lėktuvas be naujos pozicijos tiek laiko pašalinamas iš nuotraukos
ZONE_DEAD = 120         # s – zona tiek laiko be duomenų → ekranui 503 (jis laikinai ima duomenis pats)
FIRST_WAIT = 6.0        # s – nauja zona: tiek laukiama pirmo atsakymo, kad ekranas negautų tuščio sąrašo
CELL = 1.0              # paieškos indekso langelio dydis laipsniais
MAX_PER_DEVICE = 32     # ekrano lentelės dydis (AC_MAX programoje); daugiau siųsti nėra prasmės

# Laukai, kuriuos skaito ekrano programa (adsb_client.c, parse_one); kiti nesiunčiami.
SLIM = ("hex", "flight", "t", "r", "lat", "lon", "alt_baro", "alt_geom", "gs", "track", "baro_rate", "geom_rate",
        "category", "dbFlags")
IDENT_KINDS = ("hex", "callsign", "registration")


def nm_between(lat1, lon1, lat2, lon2) -> float:
    """Atstumas tarp dviejų taškų jūrmylėmis (haversino formulė)."""
    p1, p2 = math.radians(lat1), math.radians(lat2)
    h = math.sin((p2 - p1) / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(math.radians(lon2 - lon1) / 2) ** 2
    return 2 * 3440.065 * math.asin(min(1.0, math.sqrt(h)))


# ================================================================ Tiekėjai

class Provider:
    """Bendra tiekėjų dalis. Kiekvienas metodas grąžina lėktuvų sąrašą arba None, jei tiekėjas neatsakė."""
    name = ""
    global_feed = False          # True – world() grąžina visą pasaulį
    spacing = FREE_SPACING       # s tarp užklausų zonų režime

    def __init__(self, http: httpx.Client):
        self.http = http
        self.last_err = ""       # paskutinė klaida, rodoma /admin puslapyje

    def _get(self, url: str, headers: dict | None = None, timeout: float = 8.0) -> list[dict] | None:
        """GET užklausa. Atsakymo laukas `ac` arba `aircraft` → sąrašas; klaida → None."""
        host = url.split("/")[2]
        try:
            r = self.http.get(url, headers=headers, timeout=timeout)
            if r.status_code == 200:
                d = r.json()
                ac = (d.get("ac") or d.get("aircraft") or []) if isinstance(d, dict) else []
                return ac if isinstance(ac, list) else []
            self.last_err = f"{host}: HTTP {r.status_code}"
        except (httpx.HTTPError, ValueError) as e:
            self.last_err = f"{host}: {type(e).__name__}"
        log.warning("tiekejas neatsake: %s", self.last_err)
        return None

    def area(self, lat: float, lon: float, nm: int) -> list[dict] | None:
        raise NotImplementedError

    def ident(self, kind: str, ident: str) -> list[dict] | None:
        raise NotImplementedError

    def world(self) -> list[dict] | None:
        return None


class FreeProvider(Provider):
    """Nemokami adsb.fi ir adsb.lol (UPSTREAMS). Jų sąlygos neleidžia komercinio naudojimo, todėl parduodamam
    produktui reikia licencijuoto tiekėjo (PROVIDER=licensed). Jei pirmas šaltinis neatsako, klausiamas kitas."""
    URLS = {   # šaltinis → (sritis aplink tašką, vienas lėktuvas)
        "fi": ("https://opendata.adsb.fi/api/v2/lat/{lat:.4f}/lon/{lon:.4f}/dist/{nm}",
               "https://opendata.adsb.fi/api/v2/{kind}/{ident}"),
        "lol": ("https://api.adsb.lol/v2/point/{lat:.4f}/{lon:.4f}/{nm}",
                "https://api.adsb.lol/v2/{kind}/{ident}"),
    }

    def __init__(self, http: httpx.Client, sources: list[str]):
        super().__init__(http)
        self.sources = [s for s in sources if s in self.URLS]
        self.name = "free: " + ", ".join(self.sources)

    def _first(self, urls: list[str]) -> list[dict] | None:
        """Pirmas sėkmingas atsakymas iš eilės bandant šaltinius."""
        for u in urls:
            ac = self._get(u)
            if ac is not None:
                return ac
        return None

    def area(self, lat, lon, nm):
        return self._first([self.URLS[s][0].format(lat=lat, lon=lon, nm=nm) for s in self.sources])

    def ident(self, kind, ident):
        return self._first([self.URLS[s][1].format(kind=kind, ident=ident) for s in self.sources])


class LicensedProvider(Provider):
    """Licencijuotas tiekėjas su readsb / ADS-B Exchange v2 tipo API (tie patys laukai: hex, flight, lat, lon,
    alt_baro, gs, track, seen_pos …). Nustatymai .env faile:
      PROVIDER_URL         bazinis adresas, pvz. https://adsbexchange.com/api/aircraft/v2
      PROVIDER_KEY         raktas; siunčiamas antraštėje PROVIDER_KEY_HEADER (numatyta „api-auth“)
      PROVIDER_HEX_PATH    „icao“ (ADS-B Exchange) arba „hex“ (readsb) – vieno lėktuvo paieška pagal ICAO adresą
      PROVIDER_WORLD_URL   visas pasaulis vienu adresu (rekomenduojama); tuščias – zonų režimas
      PROVIDER_SPACING     sekundės tarp užklausų zonų režime (pagal sutartyje nustatytą ribą)
    Sudarius sutartį, tikslius adresus ir antraštes reikės pritaikyti tik šioje klasėje."""

    def __init__(self, http: httpx.Client):
        super().__init__(http)
        self.base = config.PROVIDER_URL
        self.headers = {config.PROVIDER_KEY_HEADER: config.PROVIDER_KEY} if config.PROVIDER_KEY else {}
        self.global_feed = bool(config.PROVIDER_WORLD_URL)
        self.spacing = config.PROVIDER_SPACING
        self.name = "licensed: " + (self.base.split("/")[2] if "//" in self.base else self.base)

    def area(self, lat, lon, nm):
        return self._get(f"{self.base}/lat/{lat:.4f}/lon/{lon:.4f}/dist/{nm}/", self.headers, 20.0)

    def ident(self, kind, ident):
        path = config.PROVIDER_HEX_PATH if kind == "hex" else kind
        return self._get(f"{self.base}/{path}/{ident}/", self.headers, 20.0)

    def world(self):
        return self._get(config.PROVIDER_WORLD_URL, self.headers, 20.0)


# ================================================================ Rinkėjas

class Feed:
    def __init__(self, provider: Provider):
        self.p = provider
        self.lock = threading.Lock()
        self.cv = threading.Condition(self.lock)       # pažadina laukiančias užklausas, kai ateina nauji duomenys
        # Nuotrauka
        self.ac: dict[str, tuple[float, float, dict]] = {}   # hex → (pozicijos laikas, gavimo laikas, SLIM laukai)
        self.cells: dict[tuple[int, int], set[str]] = {}     # paieškos indeksas: langelis → lėktuvų hex
        self.by_cs: dict[str, str] = {}                      # šaukinys → hex
        self.by_reg: dict[str, str] = {}                     # registracijos numeris → hex
        # Ko reikia ekranams
        self.zones: dict[tuple[float, float], dict] = {}     # (lat, lon) → {nm, used, ok, next, fails, born}
        self.watch: dict[tuple[str, str], dict] = {}         # (kind, ident) → {used, ok, next, fails, hex, born}
        self.world_ok = 0.0                                  # kada paskutinį kartą gautas visas pasaulis
        # Statistika
        self.calls: deque[float] = deque()                   # užklausų tiekėjui laikai per paskutinę minutę
        self.calls_total = self.fails_total = 0
        self.last_ok = 0.0
        self.last_fail: tuple[float, str] | None = None
        self._turn = 0                                       # užklausų skaitiklis WATCH_SHARE eilei
        self._purged = 0.0

    # ---------- Nuotrauka (visi šie metodai kviečiami laikant self.lock) ----------

    @staticmethod
    def _cell(lat: float, lon: float) -> tuple[int, int]:
        return int(math.floor(lat / CELL)), int(math.floor(((lon + 180.0) % 360.0) / CELL))

    def _merge(self, items: list[dict], fetched: float) -> None:
        """Įrašo tiekėjo atsakymą į nuotrauką; įrašai be tinkamo hex ar koordinačių praleidžiami."""
        for a in items:
            if not isinstance(a, dict):
                continue
            hx, lat, lon = a.get("hex"), a.get("lat"), a.get("lon")
            if not isinstance(hx, str) or len(hx) > 8 or not isinstance(lat, (int, float)) \
                    or not isinstance(lon, (int, float)) or not (-90 <= lat <= 90 and -180 <= lon <= 180):
                continue
            # seen_pos – kiek sekundžių pozicija buvo sena tiekėjo atsakymo metu
            seen = a.get("seen_pos", a.get("seen", 0))
            pos_t = fetched - (seen if isinstance(seen, (int, float)) and 0 <= seen < 3600 else 0)
            self._put(hx.lower().lstrip("~"), pos_t, fetched, {k: a[k] for k in SLIM if k in a})

    def _put(self, hx: str, pos_t: float, fetched: float, slim: dict) -> None:
        """Įrašo vieną lėktuvą, jei jo pozicija naujesnė nei turima, ir atnaujina indeksus."""
        old = self.ac.get(hx)
        if old and old[0] >= pos_t:                       # jau turime naujesnę poziciją
            return
        if old:
            self.cells.get(self._cell(old[2]["lat"], old[2]["lon"]), set()).discard(hx)
        self.cells.setdefault(self._cell(slim["lat"], slim["lon"]), set()).add(hx)
        self.ac[hx] = (pos_t, fetched, slim)
        cs = slim.get("flight")
        if isinstance(cs, str) and cs.strip():
            self.by_cs[cs.strip().upper()] = hx
        if isinstance(slim.get("r"), str) and slim["r"]:
            self.by_reg[slim["r"].upper()] = hx

    def _purge(self, now: float) -> None:
        """Pašalina pasenusius lėktuvus (tikrinama ne dažniau kaip kas 5 s)."""
        if now - self._purged < 5:
            return
        self._purged = now
        for hx in [h for h, v in self.ac.items() if now - v[0] > AC_STALE]:
            s = self.ac.pop(hx)[2]
            self.cells.get(self._cell(s["lat"], s["lon"]), set()).discard(hx)
        for index in (self.by_cs, self.by_reg):
            for k in [k for k, h in index.items() if h not in self.ac]:
                del index[k]

    def _hex_of(self, kind: str, ident: str) -> str | None:
        """Lėktuvo hex pagal paieškos tipą (hex, šaukinys ar registracija)."""
        if kind == "hex":
            return ident.lower()
        return (self.by_cs if kind == "callsign" else self.by_reg).get(ident)

    @staticmethod
    def _out(entry: tuple[float, float, dict], now: float) -> dict:
        """Lėktuvas atsakymui ekranui: seen_pos perskaičiuojamas pagal dabartinį laiką."""
        o = dict(entry[2])
        o["seen_pos"] = o["seen"] = round(max(0.0, now - entry[0]), 1)
        return o

    def _nearest(self, lat: float, lon: float, nm: int, now: float) -> list[dict]:
        """Artimiausi lėktuvai spinduliu nm: peržiūrimi tik indekso langeliai, kuriuos kerta spindulys."""
        dlat = nm / 60.0
        dlon = nm / (60.0 * max(0.05, math.cos(math.radians(lat))))
        ncols = int(360 / CELL)
        r0, c0 = self._cell(lat - dlat, lon - dlon)
        rows = int(math.floor((lat + dlat) / CELL)) - r0
        cols = min(ncols, int(math.ceil(2 * dlon / CELL)) + 1)
        found = []
        for r in range(rows + 1):
            for c in range(cols + 1):
                for hx in self.cells.get((r0 + r, (c0 + c) % ncols), ()):
                    e = self.ac.get(hx)
                    if e and now - e[0] <= AC_STALE:
                        d = nm_between(lat, lon, e[2]["lat"], e[2]["lon"])
                        if d <= nm:
                            found.append((d, hx))
        found.sort()
        return [self._out(self.ac[hx], now) for _, hx in found[:MAX_PER_DEVICE]]

    # ---------- Ko reikia ekranams (kviečiama laikant self.lock) ----------

    def _want_zone(self, key: tuple[float, float], nm: int, now: float) -> dict | None:
        """Pažymi, kad zonos reikia (naują sukuria). None – pasiektas MAX_ZONES."""
        z = self.zones.get(key)
        if z is None:
            if len(self.zones) >= config.MAX_ZONES:
                return None
            z = self.zones[key] = {"nm": nm, "used": now, "ok": 0.0, "next": 0.0, "fails": 0, "born": now}
            log.info("nauja zona %.1f, %.1f (%d NM), zonu: %d", key[0], key[1], nm, len(self.zones))
            self.cv.notify_all()
        z["used"] = now
        if nm > z["nm"]:
            z["nm"], z["next"] = nm, 0.0                  # reikia didesnio spindulio – klausti iškart
        return z

    def _want_watch(self, key: tuple[str, str], now: float) -> dict | None:
        """Pažymi, kad sekamo skrydžio reikia (naują sukuria). None – pasiektas MAX_WATCH."""
        w = self.watch.get(key)
        if w is None:
            if len(self.watch) >= config.MAX_WATCH:
                return None
            w = self.watch[key] = {"used": now, "ok": 0.0, "next": 0.0, "fails": 0, "hex": "", "born": now}
            self.cv.notify_all()
        w["used"] = now
        return w

    def _forget_idle(self, now: float) -> None:
        """Pamiršta zonas ir sekamus skrydžius, kurių niekas seniai neklausė."""
        for k in [k for k, z in self.zones.items() if now - z["used"] > ZONE_IDLE]:
            del self.zones[k]
            log.info("zona %.1f, %.1f nebenaudojama, zonu: %d", k[0], k[1], len(self.zones))
        for k in [k for k, w in self.watch.items() if now - w["used"] > WATCH_IDLE]:
            del self.watch[k]

    def _wait_first(self, st: dict, now: float) -> bool:
        """Naujai zonai ar sekamam skrydžiui palaukia pirmo atsakymo (tik pirmąsias FIRST_WAIT s nuo sukūrimo).
        Grąžina True, jei duomenys jau yra."""
        left = FIRST_WAIT - (now - st["born"])
        if not st["ok"] and left > 0:
            self.cv.wait_for(lambda: st["ok"] > 0 or st["fails"] >= 2, left)
        return st["ok"] > 0

    @staticmethod
    def _still_starting(st: dict) -> bool:
        """True – pirmo atsakymo dar laukiama (ekranui tuščias sąrašas); False – tai jau klaida (None)."""
        return st["fails"] < 2 and time.time() - st["born"] < ZONE_DEAD

    # ---------- Ekranų užklausos ----------

    def area(self, lat: float, lon: float, nm: int) -> list[dict] | None:
        """Artimiausi lėktuvai. None – duomenų nėra, ir ekranas laikinai ims duomenis pats."""
        now = time.time()
        with self.cv:
            if self.p.global_feed:
                if not self.world_ok:
                    self.cv.wait_for(lambda: self.world_ok > 0, FIRST_WAIT)
                ok = self.world_ok
            else:
                key = (round(lat / ZONE_GRID) * ZONE_GRID, round(lon / ZONE_GRID) * ZONE_GRID)
                # Zonos spindulys turi aprėpti ekrano spindulį nuo zonos centro (+2 NM atsarga), apvalinant iki 10 NM
                need = min(250, int(math.ceil((nm + nm_between(lat, lon, *key) + 2) / 10.0)) * 10)
                z = self._want_zone(key, need, now)
                if z is None:
                    return None
                if not self._wait_first(z, now):
                    return [] if self._still_starting(z) else None
                ok = z["ok"]
            if not ok or time.time() - ok > ZONE_DEAD:
                return None
            return self._nearest(lat, lon, nm, time.time())

    def one(self, kind: str, ident: str) -> list[dict] | None:
        """Sekamas skrydis: [lėktuvas], [] (nerastas) arba None (duomenų nėra)."""
        ident = ident.upper()
        now = time.time()
        with self.cv:
            hx = self._hex_of(kind, ident)
            e = self.ac.get(hx) if hx else None
            if e and now - e[0] < 15:                     # nuotraukoje yra šviežia pozicija – tiekėjo neklausiame
                return [self._out(e, now)]
            w = self._want_watch((kind, ident), now)
            if w is None:
                return None
            if not self._wait_first(w, now):
                return [] if self._still_starting(w) else None
            e = self.ac.get(w["hex"]) if w["hex"] else None
            return [self._out(e, time.time())] if e else []

    # ---------- Foninis darbas ----------

    def _next_job(self, now: float):
        """Ko klausti tiekėjo dabar: ("zone", key, z), ("watch", key, w) arba None. Kviesti laikant self.lock.
        Pasirenkama labiausiai vėluojanti zona; sekamas skrydis – tik kas WATCH_SHARE-ąjį kartą."""
        self._forget_idle(now)
        zone, zone_late = None, -1.0
        for k, z in self.zones.items():
            late = now - z["next"]
            if late >= 0 and late > zone_late:
                zone, zone_late = ("zone", k, z), late
        self._turn += 1
        if zone and self._turn % WATCH_SHARE:             # zonos svarbesnės: jas mato visi tos vietos ekranai
            return zone
        watch, watch_late = None, -1.0
        for k, w in self.watch.items():
            hx = self._hex_of(*k)
            e = self.ac.get(hx) if hx else None
            if e and now - e[1] < WATCH_POLL_S:           # lėktuvą jau atnaujina zonos – atskirai klausti nereikia
                continue
            late = now - w["next"]
            if late >= 0 and late > watch_late:
                watch, watch_late = ("watch", k, w), late
        return watch or zone

    def _record_call(self, ok: bool) -> None:
        """Statistika: užklausa tiekėjui ir jos rezultatas."""
        now = time.time()
        self.calls.append(now)
        self.calls_total += 1
        self.fails_total += 0 if ok else 1
        if ok:
            self.last_ok = now
        else:
            self.last_fail = (now, self.p.last_err or "no answer")
        while self.calls and now - self.calls[0] > 60:
            self.calls.popleft()

    def _run_world(self) -> None:
        """Vienas pasaulio režimo ciklas: visas pasaulis viena užklausa, tada laukiama iki POLL_S."""
        t0 = time.time()
        items = self.p.world()
        with self.cv:
            self._record_call(items is not None)
            if items is not None:
                self._merge(items, time.time())
                self.world_ok = time.time()
            self._purge(time.time())
            self.cv.notify_all()
        time.sleep(max(0.0, POLL_S - (time.time() - t0)))

    def _run_zones(self) -> None:
        """Vienas zonų režimo ciklas: viena užklausa tiekėjui (zonai ar sekamam skrydžiui), tada pauzė spacing."""
        with self.cv:
            self._purge(time.time())
            job = self._next_job(time.time())
            if job is None:
                self.cv.wait(0.5)
                return
        kind, key, st = job
        t0 = time.time()
        items = self.p.area(key[0], key[1], st["nm"]) if kind == "zone" else self.p.ident(*key)
        with self.cv:
            self._record_call(items is not None)
            now = time.time()
            if items is None:
                st["fails"] += 1
                st["next"] = now + min(30.0, 2.0 * st["fails"])      # nepavyko – kartoti vis rečiau, iki 30 s
                if st["fails"] == 3:
                    log.error("tiekejas 3 kartus is eiles neatsake: %s %s", kind, key)
            else:
                st["fails"], st["ok"] = 0, now
                st["next"] = now + (POLL_S if kind == "zone" else WATCH_POLL_S)
                self._merge(items, now)
                if kind == "watch":
                    first = next((a for a in items if isinstance(a, dict) and isinstance(a.get("hex"), str)
                                  and isinstance(a.get("lat"), (int, float))), None)
                    st["hex"] = first["hex"].lower().lstrip("~") if first else ""
            self.cv.notify_all()
        time.sleep(max(0.0, self.p.spacing - (time.time() - t0)))

    def run(self) -> None:
        log.info("rinkejas: %s (%s)", self.p.name, "visas pasaulis" if self.p.global_feed else "zonomis")
        while True:
            try:
                self._run_world() if self.p.global_feed else self._run_zones()
            except Exception:                             # klaida užregistruojama, bet rinkėjas nesustoja
                log.exception("rinkejo klaida")
                time.sleep(2)

    def start(self) -> None:
        threading.Thread(target=self.run, name="feed", daemon=True).start()

    # ---------- Keli procesai (cluster.py) ----------

    def interest(self) -> dict:
        """api → core: kokių zonų ir sekamų skrydžių reikia šio proceso ekranams."""
        with self.lock:
            self._forget_idle(time.time())
            return {"zones": [[k[0], k[1], z["nm"]] for k, z in self.zones.items()],
                    "watch": [list(k) for k in self.watch]}

    def add_interest(self, zones: list, watch: list) -> None:
        """core: įrašo api procesų poreikius (tą patį viename procese daro area() ir one())."""
        now = time.time()
        with self.cv:
            for lat, lon, nm in zones[:5000]:
                key = (float(lat), float(lon))
                if -90 <= key[0] <= 90 and -180 <= key[1] <= 180:
                    self._want_zone(key, max(10, min(250, int(nm))), now)
            for kind, ident in watch[:2000]:
                if kind in IDENT_KINDS:
                    self._want_watch((kind, str(ident).upper()[:12]), now)

    def export(self, since: float) -> dict:
        """core → api: lėktuvai, gauti po `since`, ir zonų bei sekamų skrydžių būsena."""
        with self.lock:
            return {"t": time.time(), "world_ok": self.world_ok,
                    "zones": [[k[0], k[1], z["ok"], z["fails"]] for k, z in self.zones.items()],
                    "watch": [[k[0], k[1], w["ok"], w["fails"], w["hex"]] for k, w in self.watch.items()],
                    "ac": [[hx, e[0], e[1], e[2]] for hx, e in self.ac.items() if e[1] > since]}

    def apply(self, snap: dict) -> None:
        """api: įrašo iš core gautą nuotrauką į šio proceso atmintį."""
        with self.cv:
            for hx, pos_t, fetched, slim in snap.get("ac", []):
                self._put(hx, pos_t, fetched, slim)
            self.world_ok = snap.get("world_ok", 0.0)
            for lat, lon, ok, fails in snap.get("zones", []):
                z = self.zones.get((lat, lon))
                if z is not None:
                    z["ok"], z["fails"] = ok, fails
            for kind, ident, ok, fails, hx in snap.get("watch", []):
                w = self.watch.get((kind, ident))
                if w is not None:
                    w["ok"], w["fails"], w["hex"] = ok, fails, hx
            self._purge(time.time())
            self.cv.notify_all()

    def stats(self) -> dict:
        """Rinkėjo būsena /admin puslapiui."""
        now = time.time()
        with self.lock:
            oldest = max((now - z["ok"] for z in self.zones.values() if z["ok"]), default=0.0)
            return {
                "provider": self.p.name,
                "mode": "world" if self.p.global_feed else "zones",
                "zones_active": len(self.zones),
                "tracked_watch": len(self.watch),
                "aircraft_in_memory": len(self.ac),
                "upstream_calls_last_min": len(self.calls),
                "upstream_calls_total": self.calls_total,
                "upstream_failures_total": self.fails_total,
                "last_ok_age_s": round(now - self.last_ok, 1) if self.last_ok else None,
                "last_failure": {"age_s": round(now - self.last_fail[0]), "what": self.last_fail[1]}
                if self.last_fail else None,
                "oldest_zone_age_s": round(oldest, 1),
                "zone_cycle_s": round(max(POLL_S, len(self.zones) * self.p.spacing), 1),   # per kiek apeinamos visos zonos
            }


def _provider() -> Provider:
    """Tiekėjas pagal PROVIDER nustatymą."""
    http = httpx.Client(timeout=8.0, headers={"User-Agent": config.USER_AGENT}, follow_redirects=False)
    return LicensedProvider(http) if config.PROVIDER == "licensed" else FreeProvider(http, config.UPSTREAMS)


feed = Feed(_provider())   # vienintelis rinkėjas procese; jį naudoja devices.py, admin.py ir cluster.py
