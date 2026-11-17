"""Visi serverio nustatymai vienoje vietoje.

Reikšmės imamos iš aplinkos kintamųjų. Kelias toks: `.env` failas → `docker-compose.yml` → konteinerio aplinka.
Kituose moduliuose `os.environ` neskaitomas – visi nustatymai pasiekiami tik per šį modulį.

Importuojant modulį iškart iškviečiama `check()`. Jei trūksta privalomos reikšmės arba ji netinkama, procesas
baigiasi su aiškiu pranešimu. Taip serveris niekada neveikia su tuščiais ar silpnais slaptažodžiais."""
import ipaddress
import os
import sys


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _int(name: str, default: int) -> int:
    """Sveikasis skaičius iš aplinkos; jei reikšmė netinkama – numatytoji."""
    try:
        return int(_env(name, str(default)))
    except ValueError:
        return default


BRAND = "AeroBox"
VERSION = "1.1"
USER_AGENT = f"{BRAND}Server/{VERSION}"   # siunčiamas visiems išoriniams šaltiniams

# ---- Proceso vaidmuo (plačiau – cluster.py).
# all – viskas viename procese; core – tik duomenų rinkėjas; api – tik atsakymai ekranams.
ROLE = _env("ROLE", "all").lower()
CORE_URL = _env("CORE_URL", "http://core:8081").rstrip("/")
INTERNAL_TOKEN = _env("INTERNAL_TOKEN")                 # bendras api ir core slaptažodis

# ---- Administratorius (/admin)
ADMIN_TOKEN = _env("ADMIN_TOKEN")
ADMIN_ALLOW_IPS = [s.strip() for s in _env("ADMIN_ALLOW_IPS").split(",") if s.strip()]   # tuščias – bet koks IP
ADMIN_BAD_TRIES = 10                                    # tiek klaidingų bandymų per 15 min užrakina IP adresą

# ---- Saugykla
DATABASE_URL = _env("DATABASE_URL")                     # postgresql+psycopg://…; sqlite:/// naudoja tik testai
FIRMWARE_DIR = _env("FIRMWARE_DIR", "/data/firmware")   # įkeltų programos versijų failai
GEOIP_DB = _env("GEOIP_DB", "/data/geoip/GeoLite2-City.mmdb")

# ---- Lėktuvų duomenų tiekėjas (feed.py)
PROVIDER = _env("PROVIDER", "free").lower()             # free arba licensed
UPSTREAMS = [s.strip() for s in _env("UPSTREAMS", "fi,lol").split(",") if s.strip()]   # free: adsb.fi, adsb.lol
PROVIDER_URL = _env("PROVIDER_URL").rstrip("/")
PROVIDER_KEY = _env("PROVIDER_KEY")
PROVIDER_KEY_HEADER = _env("PROVIDER_KEY_HEADER", "api-auth")
PROVIDER_HEX_PATH = _env("PROVIDER_HEX_PATH", "icao")
PROVIDER_WORLD_URL = _env("PROVIDER_WORLD_URL")         # nurodytas – visas pasaulis viena užklausa; tuščias – zonos
PROVIDER_SPACING = float(_env("PROVIDER_SPACING", "0.25"))
MAX_ZONES = _int("MAX_ZONES", 600)                      # zonų režime: kiek skirtingų vietų sekama vienu metu
MAX_WATCH = _int("MAX_WATCH", 200)                      # kiek skirtingų sekamų skrydžių vienu metu

# ---- Kiti išoriniai šaltiniai
ROUTES_URL = _env("ROUTES_URL", "https://api.adsbdb.com/v0").rstrip("/")
WEATHER_URL = _env("WEATHER_URL", "https://api.open-meteo.com/v1/forecast")
WEATHER_KEY = _env("WEATHER_KEY")                       # Open-Meteo komercinio plano raktas

# ---- Apsauga nuo sugedusio ekrano.
# Veikiantis ekranas siunčia maždaug vieną užklausą kas 10 s, todėl šių ribų niekada nepasiekia.
# Ribojamas tik tas ekranas, kuris jas viršija; kitiems ekranams tai neturi įtakos.
DEVICE_RATE = 5.0                                       # užklausų per sekundę, vidutiniškai
DEVICE_BURST = 100                                      # kiek užklausų galima išsiųsti iš karto


def ip_allowed(ip: str, nets: list[str]) -> bool:
    """Ar IP adresas patenka į bent vieną tinklą (CIDR). Tuščias sąrašas leidžia bet kokį adresą."""
    if not nets:
        return True
    try:
        a = ipaddress.ip_address(ip)
        return any(a in ipaddress.ip_network(n, strict=False) for n in nets)
    except ValueError:
        return False


def check() -> None:
    """Paleidimo patikra: geriau nepasileisti, nei veikti su trūkstamais ar silpnais nustatymais."""
    problems = []
    if ROLE not in ("all", "core", "api"):
        problems.append(f"ROLE turi buti all, core arba api (dabar: {ROLE!r})")
    if len(ADMIN_TOKEN) < 24:
        problems.append("ADMIN_TOKEN turi buti bent 24 simboliu (openssl rand -hex 24)")
    if ROLE in ("core", "api") and len(INTERNAL_TOKEN) < 24:
        problems.append("INTERNAL_TOKEN turi buti bent 24 simboliu")
    if not DATABASE_URL:
        problems.append("truksta DATABASE_URL")
    if PROVIDER not in ("free", "licensed"):
        problems.append(f"PROVIDER turi buti free arba licensed (dabar: {PROVIDER!r})")
    if PROVIDER == "licensed" and not PROVIDER_URL:
        problems.append("PROVIDER=licensed, bet truksta PROVIDER_URL")
    if problems:
        sys.exit("SERVERIS NEPALEISTAS:\n - " + "\n - ".join(problems))


check()     # šis modulis importuojamas pirmas, todėl patikra įvyksta prieš visa kita
