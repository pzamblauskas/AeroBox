"""Automatinė serverio patikra prieš kiekvieną diegimą. Internetas ir PostgreSQL nereikalingi: naudojama laikina
SQLite bazė ir netikras lėktuvų tiekėjas.

    uv run python -u tests/test_server.py

Kiekviena eilutė „OK …“ – patikrintas elgesys; pirma klaida sustabdo patikrą („FAIL …“, išėjimo kodas 1)."""
import os
import sys
import tempfile
import threading
import time

TMP = tempfile.mkdtemp(prefix="aerobox-test-")
os.environ.update(ADMIN_TOKEN="t" * 32, DATABASE_URL=f"sqlite:///{TMP}/test.db", FIRMWARE_DIR=f"{TMP}/fw",
                  GEOIP_DB=f"{TMP}/none.mmdb", ROLE="all", PROVIDER="free")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fastapi.testclient import TestClient  # noqa: E402
from sqlalchemy import text  # noqa: E402

from app import auth, config, db, main  # noqa: E402
from app import feed as feedmod  # noqa: E402

ADMIN = {"X-Admin-Token": "t" * 32}
passed = 0


def ok(cond, what):
    global passed
    if not cond:
        print("FAIL", what)
        sys.exit(1)
    passed += 1
    print("OK  ", what)


class FakeProvider(feedmod.Provider):
    """Grąžina 200 lėktuvų aplink bet kurį tašką ir skaičiuoja užklausas."""
    name = "fake"
    spacing = 0.0

    def __init__(self):
        super().__init__(None)
        self.calls = 0

    def area(self, lat, lon, nm):
        self.calls += 1
        return [{"hex": f"{0x400000 + i:06x}", "flight": f"TST{i:03d} ", "lat": lat + (i % 20 - 10) * 0.02,
                 "lon": lon + (i // 20 - 5) * 0.03, "alt_baro": 30000 + i, "gs": 400, "track": 90, "seen_pos": 1.0,
                 "t": "B738", "r": f"LY-T{i:02d}", "secret_extra_field": "x" * 50} for i in range(200)]

    def ident(self, kind, ident):
        self.calls += 1
        return [{"hex": "4ca123", "flight": "FAR1    ", "lat": 10.0, "lon": 10.0, "seen_pos": 0.5}]


fake = FakeProvider()
feedmod.feed.p = fake

with TestClient(main.app) as c:
    # ---- Registracija: bet kuris ekranas, be papildomų sąlygų
    ok(c.get("/api/v2/lat/54.7/lon/25.3/dist/30").status_code == 401, "be rakto - 401")
    ok(c.get("/api/v2/lat/54.7/lon/25.3/dist/30", headers={"X-Device-Key": "wrong"}).status_code == 401,
       "neteisingas raktas - 401 (skydelis persiregistruoja)")
    ok(c.post("/v1/register?fw=1.0.0").status_code == 200, "registracija be MAC (sena programa) - veikia")
    r = c.post("/v1/register?fw=1.2.0&id=aabbccddeeff").json()
    key1, dev = r["key"], r["id"]
    H = {"X-Device-Key": key1, "User-Agent": "AeroBox/1.2.0 (ESP32-S3)"}
    r2 = c.post("/v1/register?fw=1.2.0&id=AA:BB:CC:DD:EE:FF").json()
    ok(r2["id"] == dev and r2["key"] != key1, "tas pats irenginys - tas pats irasas, naujas raktas")
    db._known.clear()
    ok(c.get("/v1/update?fw=1.2.0", headers=H).status_code == 401, "senas raktas po perregistravimo nebegalioja")
    ok(all(c.post("/v1/register?fw=1.2.0&id=aabbccddeeff").status_code == 200 for _ in range(30)),
       "30 perregistravimu is eiles - visi priimti")
    H["X-Device-Key"] = c.post("/v1/register?fw=1.2.0&id=aabbccddeeff").json()["key"]
    with db.engine.connect() as conn:
        ok(conn.execute(text("SELECT COUNT(*) FROM devices WHERE hw_id = 'aabbccddeeff'")).scalar() == 1,
           "baze neauga: vienas irenginys - vienas irasas")
    ok(all(c.post(f"/v1/register?fw=1.2.0&id=0000000000{i:02x}").status_code == 200 for i in range(60)),
       "60 skirtingu skydeliu is vieno IP (parduotuve, biuras) - visi priimti")

    # ---- Lėktuvai
    r = c.get("/api/v2/lat/54.7/lon/25.3/dist/30", headers=H)
    d = r.json()
    ok(r.status_code == 200 and 0 < d["total"] <= 32, f"lektuvai: {d['total']} artimiausi (ne daugiau 32)")
    ok("secret_extra_field" not in d["aircraft"][0] and "seen_pos" in d["aircraft"][0], "siunciami tik reikalingi laukai")
    calls = fake.calls
    for _ in range(20):
        c.get("/api/v2/lat/54.7/lon/25.3/dist/30", headers=H)
    ok(fake.calls - calls <= 1, "20 skydelio uzklausu - tiekejas papildomai neklaustas")
    ok(c.get("/api/v2/lat/954.7/lon/25.3/dist/30", headers=H).status_code == 400, "blogos koordinates - 400")
    codes = [c.get(f"/api/v2/lat/{10 + i * 3}.0/lon/25.3/dist/30", headers=H).status_code for i in range(10)]
    ok(all(x == 200 for x in codes), "skydelis keicia vieta 10 kartu is eiles - visada gauna duomenis")
    ok(c.get("/api/v2/callsign/TST005", headers=H).json()["total"] == 1, "sekamas skrydis randamas nuotraukoje")
    ok(c.get("/api/v2/callsign/FAR1", headers=H).json()["total"] == 1, "sekamas skrydis uz zonu ribu - paklausta atskirai")
    ok(c.get("/api/v2/callsign/..%2F..", headers=H).status_code in (400, 404), "netinkamas saukinys atmestas")
    ok(c.get("/v1/geo").status_code == 404, "be GeoLite2 failo vieta nerandama (skydelis klausia savininko)")

    # ---- Sugedęs ekranas
    auth._buckets.clear()
    rate, config.DEVICE_RATE = config.DEVICE_RATE, 0.0          # kad rezultatas nepriklausytų nuo testo greičio
    codes = [c.get("/v1/update?fw=1.2.0", headers=H).status_code for _ in range(config.DEVICE_BURST + 5)]
    config.DEVICE_RATE = rate
    ok(codes[0] == 200 and codes[-1] == 429, f"sugedes skydelis (>{config.DEVICE_BURST} uzklausu is karto) - 429")
    other = c.post("/v1/register?fw=1.2.0&id=bbbbbbbbbbbb").json()["key"]
    ok(c.get("/v1/update?fw=1.2.0", headers={"X-Device-Key": other}).status_code == 200, "kiti skydeliai veikia toliau")
    auth._buckets.clear()

    # ---- Administratorius
    ok(c.get("/v1/stats").status_code == 404, "statistika be rakto - 404")
    for _ in range(config.ADMIN_BAD_TRIES):
        c.get("/v1/stats", headers={"X-Admin-Token": "spejimas"})
    ok(c.get("/v1/stats", headers=ADMIN).status_code == 429, "po daug neteisingu bandymu administratorius laikinai uzrakintas")
    auth._bad_admin.clear()
    s = c.get("/v1/stats", headers=ADMIN).json()
    ok(s["devices"] >= 2 and s["server"]["processes"] == 1 and "feed" in s, "statistika su teisingu raktu")
    ok("content-security-policy" in c.get("/admin").headers, "valdymo puslapis su saugumo antrastemis")
    ok(c.get("/internal/stats").status_code == 404, "vidiniai adresai is isores nepasiekiami")
    config.ADMIN_ALLOW_IPS[:] = ["10.0.0.0/8"]
    ok(c.get("/v1/stats", headers=ADMIN).status_code == 404, "administratorius ne is leidziamo IP - 404")
    config.ADMIN_ALLOW_IPS[:] = []

    # ---- Atnaujinimai
    fw = bytearray(4096)
    fw[0] = 0xE9
    fw[32:36] = (0xABCD5432).to_bytes(4, "little")
    fw[32 + 48:32 + 55] = b"skypanel"[:7]
    fw[600:600 + 24] = b"AeroBox/9.9.9 (ESP32-S3)"
    fw = bytes(fw)
    ok(c.post("/v1/admin/firmware", content=fw).status_code == 404, "programos ikelimas be rakto - 404")
    ok(c.post("/v1/admin/firmware", headers=ADMIN, content=b"\xe9" + b"0" * (4 * 1024 * 1024 + 10)).status_code == 413,
       "per didelis failas - 413")
    ok(c.post("/v1/admin/firmware", headers=ADMIN, content=b"ne programa" * 100).status_code == 400, "ne programos failas - 400")
    r = c.post("/v1/admin/firmware?notes=test", headers=ADMIN, content=fw)
    ok(r.status_code == 200 and r.json()["version"] == "9.9.9", "programa ikelta, versija perskaityta is failo")
    u = c.get("/v1/update?fw=1.2.0", headers=H).json()
    ok(u["version"] == "9.9.9" and u["size"] == len(fw) and len(u["sha256"]) == 64, "skydeliui pasiuloma naujesne versija")
    ok(c.get("/v1/update?fw=9.9.9", headers=H).json()["version"] == "", "naujausia versija turinciam nieko nesiuloma")
    ok(c.get("/v1/update?fw=1.2.0-fw", headers=H).json()["version"] == "", "kitam variantui nesiuloma")
    ok(c.get("/v1/firmware/9.9.9", headers=H).content == fw, "parsisiustas failas sutampa")
    ok(c.get("/v1/firmware/9.9.9").status_code == 401, "programos failas be skydelio rakto - 401")
    ok(c.get("/v1/firmware/..", headers=H).status_code in (400, 404), "kelio apejimas atmestas")
    ok(c.delete("/v1/admin/firmware/9.9.9", headers=ADMIN).json()["ok"], "versija pasalinta")
    ok(c.get("/v1/update?fw=1.2.0", headers=H).json()["version"] == "", "pasalinta versija nebesiuloma")

    # ---- Blokavimas (tik rankiniu būdu /admin puslapyje)
    ok(c.post(f"/v1/admin/devices/block?id={dev}", headers=ADMIN).json()["ok"], "skydelis uzblokuotas")
    ok(c.get("/v1/update?fw=1.2.0", headers=H).status_code == 401, "uzblokuotas skydelis nebeaptarnaujamas")
    ok(c.post("/v1/register?fw=1.2.0&id=aabbccddeeff").status_code == 403, "uzblokuotas negali persiregistruoti")
    ok(c.get("/v1/admin/devices?q=aabb", headers=ADMIN).json()["devices"][0]["blocked"] == 1, "matomas skydeliu sarase")
    c.post(f"/v1/admin/devices/block?id={dev}&blocked=0", headers=ADMIN)
    ok(c.post("/v1/register?fw=1.2.0&id=aabbccddeeff").status_code == 200, "atblokuotas vel registruojasi")
    ok(len(c.get("/v1/admin/logs", headers=ADMIN).json()["logs"]) > 3, "zurnalas pasiekiamas")

# ---- Keli procesai: core renka duomenis, api gauna nuotrauką
core, api = feedmod.Feed(FakeProvider()), feedmod.Feed(FakeProvider())
core.start()
res = {}
t = threading.Thread(target=lambda: res.update(a=api.area(48.85, 2.35, 40)))
t.start()
since = 0.0
for _ in range(40):
    it = api.interest()
    core.add_interest(it["zones"], it["watch"])
    snap = core.export(since)
    api.apply(snap)
    since = snap["t"] - 1
    if not t.is_alive():
        break
    time.sleep(0.1)
t.join(5)
ok(res.get("a") and len(res["a"]) > 0, f"api procesas gavo {len(res.get('a') or [])} lektuvu is core")
ok(api.p.calls == 0 and core.p.calls >= 1, "tiekejo klausia tik core, api procesai - niekada")
ok(len(api.area(48.85, 2.35, 40)) > 0, "kitos uzklausos - is api proceso atminties")

print(f"\nVISKAS GERAI: {passed} patikrinimai")
os._exit(0)
