# AeroBox serveris

Serveris AeroBox ekranams (ESP32-S3, 128×64 LED). Jis atlieka keturis darbus:

- duoda ekranams lėktuvų duomenis;
- registruoja ekranus;
- tarpininkauja maršrutams ir orams;
- platina programos atnaujinimus.

Python 3.13, FastAPI, PostgreSQL, Docker Compose, Caddy (HTTPS).

> **Būsena:** programa patikrinta 46 automatiniais testais ir dviejų procesų (`core` + `api`) bandymu su
> tikrais duomenimis. Docker / Caddy failai dar nepaleisti tikrame serveryje, todėl pirmas diegimas gali
> pareikalauti smulkių pataisymų.

## 1. Kaip veikia

```
ekranai ──HTTPS──► caddy ──► api ×N ──► core ──► lėktuvų duomenų tiekėjas
                    │          │   └──► adsbdb (maršrutai), Open-Meteo (orai)
                    │          └──────► db (PostgreSQL)
                    └ /internal/* iš interneto atmetama
```

**Svarbiausia taisyklė:** ekrano užklausa niekada nekeliauja tiesiai pas tiekėją.

- `core` vienintelis klausia tiekėjo, savo tempu, ir laiko visų lėktuvų „nuotrauką“ atmintyje.
- `api` kopijos kas 2 s gauna iš `core` naujus lėktuvus ir atsako ekranams iš savo atminties.
- Todėl tiekėjo užklausų skaičius nepriklauso nei nuo ekranų, nei nuo `api` kopijų skaičiaus.

**Ekranas** ([firmware](../SkyPanel/components/net/adsb_client.c)):

- Pirmą kartą užsiregistruoja (`POST /v1/register`) ir gauna raktą. Su juo siunčia visas tolesnes užklausas
  (`X-Device-Key`).
- Lėktuvų klausia kas ~10 s.
- Atnaujinimų tikrina kas 6 val.
- Jei serveris neatsako 3 kartus iš eilės arba grąžina 5xx, ekranas 5 min ima duomenis tiesiai iš nemokamų
  šaltinių ir vėl bando serverį. Todėl trumpas serverio gedimas pirkėjams nematomas.

**Registracija be sąlygų.**

- Bet koks ekranas užsiregistruoja ir veikia iškart: be leidžiamų sąrašų ir be ribų pagal IP ar skaičių.
- Tas pats ekranas (pagal MAC) visada yra tas pats įrašas. Persiregistravęs jis gauna naują raktą, o senasis
  nustoja galioti.

## 2. Failai

| Failas | Kas jame |
|---|---|
| `app/main.py` | Įėjimo taškas: paleidimas, foninis valymas, saugumo antraštės, `/health` |
| `app/config.py` | **Visi nustatymai** ir paleidimo patikra (trūksta reikšmės – serveris nepasileidžia) |
| `app/feed.py` | **Širdis:** lėktuvų rinkėjas, tiekėjai (`FreeProvider`, `LicensedProvider`), zonos |
| `app/devices.py` | Adresai ekranams: registracija, lėktuvai, maršrutai, orai, vieta pagal IP |
| `app/auth.py` | Ekrano rakto ir administratoriaus rakto tikrinimas |
| `app/updates.py` | Programos atnaujinimai: įkėlimas, pasiūlymas ekranams, parsisiuntimas |
| `app/admin.py`, `app/admin_page.html` | Valdymo puslapis `/admin` ir jo duomenys |
| `app/cluster.py` | Ryšys `api` ↔ `core` |
| `app/db.py` | Duomenų bazė: lentelės ir užklausos |
| `app/metrics.py` | Skaitikliai ir žurnalas `/admin` puslapiui |
| `tests/test_server.py` | Automatinė patikra prieš kiekvieną diegimą |
| `docker-compose.yml` | Visos serverio dalys ir jų ryšiai |
| `Dockerfile`, `Caddyfile` | Programos konteineris; HTTPS ir paskirstymas tarp `api` kopijų |
| `.env.example` | Nustatymų šablonas |
| `deploy/paruosti_serveri.sh` | Naujo Ubuntu serverio paruošimas (ugniasienė, Docker, SSH apsauga) |
| `deploy/sukurti_env.sh` | `.env` su atsitiktiniais slaptažodžiais |

### Ekrano adresai

| Adresas | Ką grąžina |
|---|---|
| `POST /v1/register?fw=&id=<MAC>` | `{id, key}` |
| `GET /api/v2/lat/{lat}/lon/{lon}/dist/{nm}` | Artimiausi lėktuvai (≤ 32, adsb.fi formatas) |
| `GET /api/v2/{hex\|callsign\|registration}/{x}` | Sekamas skrydis |
| `GET /v0/{callsign\|airline}/{x}` | Maršrutas / aviakompanija (adsbdb formatas, laikoma duomenų bazėje) |
| `GET /v1/forecast` | Orai (Open-Meteo formatas, 15 min talpykla) |
| `GET /v1/geo` | Apytikslė vieta pagal IP (reikia GeoLite2 failo) |
| `GET /v1/update`, `GET /v1/firmware/{v}` | Atnaujinimai |

Atsakymas **503** reiškia „duomenų dabar nėra“: ekranas laikinai pereina prie tiesioginių šaltinių. Tai ne klaida.

## 3. Pirmas diegimas (~15 min)

**Reikia:**

- Linux serverio su Ubuntu 22.04 / 24.04: 2 vCPU, 4 GB RAM, 40 GB disko;
- domeno, pvz. `api.jusu-domenas.com`, kurio A įrašas rodo į serverio IP;
- SSH rakto.

**Žingsniai:**

1. Nukelkite šį aplanką į serverį (`/opt/aerobox`). `.venv` aplanko kelti nereikia.

2. Paruoškite serverį:

   ```bash
   cd /opt/aerobox
   sudo bash deploy/paruosti_serveri.sh
   ```

   Skriptas atnaujina sistemą, įjungia ugniasienę (22, 80, 443) ir fail2ban, įdiegia Docker ir sukuria swap.
   Jei rado SSH raktą, išjungia prisijungimą slaptažodžiu.

3. Sukurkite `.env`:

   ```bash
   bash deploy/sukurti_env.sh api.jusu-domenas.com jusu@pastas.com
   ```

   Skriptas parodys `ADMIN_TOKEN`. Išsaugokite jį slaptažodžių tvarkyklėje.

4. Paleiskite:

   ```bash
   docker compose up -d --build
   ```

5. Patikrinkite, ar viskas veikia:
   - `docker compose ps`: visi konteineriai turi būti `running` / `healthy`;
   - `curl https://api.jusu-domenas.com/health` turi atsakyti `{"ok":true,"role":"api"}`;
   - `https://api.jusu-domenas.com/admin` atsidaro ir priima raktą.

6. Atsisiųskite nemokamą MaxMind failą `GeoLite2-City.mmdb` ir padėkite į `geoip/`. Tada paleiskite
   `docker compose restart api`. Be šio failo ekrano vieta įvedama ranka, o visa kita veikia.

**Ekranų nukreipimas į serverį:**

- Parduodamiems ekranams: [settings.h](../SkyPanel/components/sys/settings.h) faile `SKY_SERVER_URL` įrašykite
  `"https://api.jusu-domenas.com"` ir sukompiliuokite programą.
- Jau veikiančiam ekranui: jo puslapyje System → Flight data → AeroBox server.

## 4. Kasdienis darbas

| Ką daryti | Komanda / vieta |
|---|---|
| Bendra būsena, klaidos, ekranai | `https://DOMENAS/admin` |
| Konteinerių būsena | `docker compose ps` |
| Žurnalas | `docker compose logs -f --tail 200 api core` |
| Pakeisti nustatymus | redaguoti `.env`, tada `docker compose up -d` |
| Daugiau galios | `.env`: `API_REPLICAS=4`, tada `docker compose up -d` |
| Įdiegti pataisytą kodą | `tests` praeina → nukelti failus → `docker compose up -d --build` |
| Perkrauti | `docker compose restart` |
| Ekrano programos atnaujinimas | `/admin` → Upload firmware → įkelti `firmware.bin` (ne `firmware_merged.bin`) |
| Užblokuoti ekraną | `/admin` → Manage displays → Block. Ekranas nustoja gauti duomenis per ≤ 2 min |

**Stebėsena.** Nemokamoje paslaugoje (UptimeRobot, Better Stack) nustatykite tikrinti
`https://DOMENAS/health` kas minutę ir siųsti pranešimą, jei neatsako.

## 5. Atsarginės kopijos

- Konteineris `backup` kasdien įrašo duomenų bazę į `backups/` ir laiko 14 dienų.
- Kopijas reguliariai parsisiųskite ir kitur: `scp root@SERVERIS:/opt/aerobox/backups/*.gz .`
- Saugokite ir `.env` failą: be `DB_PASSWORD` kopijos neatkursite.
- Atkūrimas:

  ```bash
  gunzip -c backups/aerobox-DATA.sql.gz | docker compose exec -T db psql -U aerobox aerobox
  ```

- Net praradus bazę ekranai neužstringa: kiekvienas gaus 401, persiregistruos pats ir veiks toliau. Prarandama
  tik istorija ir įkeltos programos versijos.

## 6. Gedimų paieška

| Ką matote | Tikėtina priežastis | Ką daryti |
|---|---|---|
| Konteineris neįsijungia, žurnale `SERVERIS NEPALEISTAS` | Trūksta `.env` reikšmės arba ji per trumpa | Pataisyti `.env` pagal pranešimą |
| `/health` neatsako | Caddy negavo sertifikato (DNS neteisingas) arba `api` nukrito | `docker compose logs caddy api` |
| `/admin` rodo „CORE NOT ANSWERING“ | `core` sustojo | `docker compose logs core`. Docker jį paleidžia pats; ekranai tuo metu naudoja atsarginį kelią |
| Daug 503, žurnale `tiekejas neatsake` | Tiekėjas neatsako arba blokuoja | `/admin` → Flight data (paskutinė klaida); patikrinti `PROVIDER_*` ir tiekėjo sutartį |
| Lėktuvai rodomi pavėluotai | Zonų režime per daug zonų tiekėjo greičiui | `/admin` → Flight data (zonų ratas, s). Sprendimas – `PROVIDER_WORLD_URL` (visas pasaulis viena užklausa) |
| Naujos vietos negauna duomenų | Pasiekta `MAX_ZONES` | Didinti `MAX_ZONES` arba pereiti į pasaulio režimą |
| Ekranas gauna 401 | Raktas negalioja arba ekranas užblokuotas | Ekranas persiregistruoja pats; jei užblokuotas – `/admin` → Unblock |
| Ekranas gauna 429 | Tas ekranas siunčia >100 užklausų iš karto (sugedusi programa) | Ribojamas tik jis; žiūrėti to ekrano programos versiją |
| `/admin` atsako 429 | 10 neteisingų raktų per 15 min iš to IP | Palaukti 15 min |
| `Upload failed` įkeliant programą | Įkeltas ne `firmware.bin` arba nėra versijos eilutės | Žiūrėti klaidos tekstą |

Kodo klaidas lengviausia rasti `/admin` → Log, pasirinkus `ERROR`. Kiekviena nesugauta klaida įrašoma su visu
Python pėdsaku (traceback).

## 7. Nustatymai (`.env`)

Visi aprašyti [.env.example](.env.example). Svarbiausi:

| Kintamasis | Reikšmė |
|---|---|
| `DOMAIN`, `ACME_EMAIL` | Serverio adresas ir el. paštas HTTPS sertifikatui |
| `DB_PASSWORD`, `ADMIN_TOKEN`, `INTERNAL_TOKEN` | Slaptažodžiai, ≥ 24 simboliai |
| `ADMIN_ALLOW_IPS` | `/admin` tik iš šių IP (rekomenduojama) |
| `API_REPLICAS` | `api` kopijų skaičius |
| `PROVIDER` | `free` (bandymams) arba `licensed` |
| `PROVIDER_URL`, `PROVIDER_KEY`, `PROVIDER_WORLD_URL` | Licencijuoto tiekėjo duomenys |
| `WEATHER_URL`, `WEATHER_KEY` | Open-Meteo komercinis planas |

Pastovios reikšmės (laikai, talpyklos, ribos) yra modulių viršuje, pavadintos didžiosiomis raidėmis, su
komentaru. Pavyzdžiui, `POLL_S` ir `ZONE_GRID` yra `feed.py`, `DEVICE_RATE` – `config.py`.

## 8. Licencijuoto tiekėjo prijungimas

1. `.env` faile nustatykite:
   - `PROVIDER=licensed`;
   - `PROVIDER_URL` ir `PROVIDER_KEY`;
   - jei tiekėjas duoda visą pasaulį vienu adresu, ir `PROVIDER_WORLD_URL`.
2. Jei tiekėjo adresai ar antraštės kitokie, pritaikykite tik `LicensedProvider` klasę `app/feed.py` faile.
   Ji grąžina lėktuvų sąrašą readsb / ADS-B Exchange v2 laukais.
3. `docker compose up -d` ir `/admin` → Flight data: tiekėjo pavadinimas, užklausos ir „last OK“.

## 9. Apsaugos

| Kas | Kaip |
|---|---|
| Svetimi naudojasi duomenimis | Kiekvienas ekranas turi savo raktą; bazėje laikomas tik rakto SHA-256 |
| Sugedęs ekranas siunčia užklausas be perstojo | 5 užklausos/s, iki 100 iš karto, tik tam ekranui. Tikras ekranas siunčia ~0,1/s, todėl šios ribos niekada nepasiekia |
| `/admin` perėmimas | Raktas ≥ 24 simbolių, lyginamas pastovaus laiko funkcija; 10 klaidų per 15 min užrakina IP; `ADMIN_ALLOW_IPS`; be rakto atsakoma 404 |
| IP klastojimas | `X-Forwarded-For` priimamas tik iš Caddy tinklo |
| Kenkėjiškas programos failas | ≤ 4 MB, tikrinama ESP32 antraštė ir versija; ekranas tikrina SHA-256; diegia tik savininkui paspaudus |
| Vidiniai adresai | `/internal/*` Caddy atmeta, reikia `INTERNAL_TOKEN`, `core` neturi atviro prievado |
| Duomenų bazė | Prievadas neatidarytas, tik vidinis Docker tinklas |
| Konteineriai | Ne root, tik skaitoma failų sistema, be Linux privilegijų, atminties ribos |
| Serveris | Tik SSH raktai, fail2ban, ugniasienė, automatiniai saugumo atnaujinimai |

**Niekada:**

- nekelkite `.env` į git;
- neatidarykite 5432 prievado;
- neišjunkite HTTPS.

## 10. Testai

Testus paleiskite prieš kiekvieną kodo diegimą. Interneto ir PostgreSQL nereikia:

```bash
uv run python -u tests/test_server.py
```

Paskutinė eilutė turi būti `VISKAS GERAI: 46 patikrinimai`. Taisant klaidą, pirmiausia parašykite testą, kuris
ją parodo.
