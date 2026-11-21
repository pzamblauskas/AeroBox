# AeroBox – programos įkėlimas

Programos versija: **1.2.5** · Plokštė: **ESP32-S3-N16R8** · Ekranas: 4 × P2.5 64×32 HUB75 (128×64)

## Kas yra šiame aplanke

| Failas | Kam skirtas |
|---|---|
| `1_ikelti.bat` | Įprastas įkėlimas per USB. Wi-Fi ir nustatymai **išlieka**. |
| `2_istrinti_viska_ir_ikelti.bat` | **Pirmam kartui ir kai „niekas neveikia“.** Ištrina visą atmintį ir įkelia programą. Wi-Fi ir nustatymai bus ištrinti. |
| `3_ziureti_zurnala.bat` | Rodo plokštės žurnalą (log) – kai reikia išsiaiškinti, kas negerai. |
| `4_kompiliuoti_is_kodo_ir_ikelti.bat` | Sukompiliuoja programą iš `..\AeroBox`, įkelia ją ir atnaujina `bin\` failus. |
| `bin\firmware.bin` | Tik programa – atnaujinimui per valdymo puslapį arba per AeroBox serverį. |
| `bin\firmware_merged.bin` | Viskas viename faile, rašoma adresu **0x0** (naršyklės būdui ir gamybai). Ištrina nustatymus. |
| `bin\bootloader.bin`, `partitions.bin`, `ota_data_initial.bin` | Atskiros dalys, kurias naudoja `1_ikelti.bat`. |

`bin\` failai yra saugykloje (naujausia versija, paruošti įkelti); juos atnaujina `4_kompiliuoti_is_kodo_ir_ikelti.bat`.

Skriptams reikia **uv** arba Python su `pip install esptool`. uv įdiegiamas taip:
`powershell -c "irm https://astral.sh/uv/install.ps1 | iex"`.

> **Ekranams su 1.2.3 ar senesne programa:** nuo 1.2.4 pasikeitė vidinis programos vardas (`skypanel` → `aerobox`),
> todėl senesnė programa naujos per puslapį ar serverį nepriims. Vieną kartą įkelkite ją laidu
> (`1_ikelti.bat`). Nustatymai išliks – nauja programa juos perkels pati. Vėliau atnaujinimai vėl veikia be laido.

## Kas pakeista – 1.2.5 (2026-11-21)

Wi-Fi nustatymas – kad pirmą kartą įjungtą dėžutę galėtų prijungti bet kas, su bet kokiu tinklu:

- Tinklo vardai ir slaptažodžiai su lietuviškomis raidėmis, tarpais ir simboliais (`@ # ! & %`) priimami teisingai
  (anksčiau ilgesni buvo laikomi tuščiais: „Choose a Wi-Fi network“ arba „Wrong password“).
- Randami tinklai 12 ir 13 kanaluose (Europoje dažni; anksčiau – „Network not found“).
- 32 simbolių tinklo vardas ir 64 simbolių slaptažodis nebenukerpami.
- Iš sąrašo pasirinktas tinklas siunčiamas tiksliai (ir su tarpais pradžioje ar gale).
- Nustatymų tinkle puslapis, atidarytas kitu adresu nei `192.168.4.1`, nukreipiamas į jį (anksčiau „Connect“ tyliai neveikė).
- Puslapis nebestringa ties „Connecting…“: po 12 s be atsakymo parodomas pranešimas.
- Naujas mygtukas **Save without testing**: tinklas išsaugomas nebandžius (pvz., to paties telefono „hotspot“);
  dėžutė prisijungia, kai tik tinklas atsiranda.
- Daugiau tinklo ryšių (16 vietoj 10) – puslapis atsidaro patikimiau.

Namų vieta:

- Vieta pagal IP adresą nebenustatoma. Kol vieta neišsaugota puslapio skiltyje **Location**, ekrane nuolat rodoma
  juosta **CHOOSE YOUR LOCATION** su valdymo puslapio adresu.

## Programos įkėlimas

### A būdas – skriptu (rekomenduojama)

1. Prijunkite ESP32-S3 USB kabeliu prie jungties, pažymėtos **USB** (jei plokštė turi dvi – ne „UART/COM“).
2. Dukart spustelėkite `2_istrinti_viska_ir_ikelti.bat` (pirmą kartą) arba `1_ikelti.bat` (vėliau).
3. Įveskite COM prievadą (skriptas parodo rastus, pvz., `COM8`).
4. Palaukite „[OK] Ikelta“. Jei ekranas per 5 s neužsidega, paspauskite plokštės **RST**.

Jei rašo „Failed to connect“: laikykite **BOOT**, trumpai paspauskite **RST**, atleiskite **BOOT** ir bandykite dar kartą.

### B būdas – naršyklėje, nieko nediegiant

1. Chrome arba Edge naršyklėje atidarykite https://espressif.github.io/esptool-js/
2. Baudrate `460800` → **Connect** → pasirinkite ESP32 prievadą.
3. Pirmą kartą – **Erase Flash**.
4. Flash Address `0x0`, failas `bin\firmware_merged.bin` → **Program**.
5. Paspauskite plokštės **RST**.

Šis būdas ištrina Wi-Fi ir nustatymus.

### C būdas – per valdymo puslapį (be laido)

Atidarykite **http://aerobox.local** (arba ekrane rodomą IP adresą) → **System** → **Firmware update** →
pasirinkite `bin\firmware.bin` → **Upload firmware**. Nustatymai išlieka. Jei nauja programa nepasileidžia,
po perkrovimo grąžinama senoji. `firmware_merged.bin` čia netinka – puslapis jį atmes.

### D būdas – per AeroBox serverį

Kai ekranas naudoja AeroBox serverį, nauja versija įkeliama serverio valdymo puslapyje (`/admin`), o ekranų
savininkai savo puslapyje mato pranešimą ir paspaudžia **Update**. Plačiau – `..\AeroBox_Server\README.md`.

### E būdas – iš kodo (programuotojui)

```bash
cd ..\AeroBox
uvx --from platformio pio run -e esp32-s3-n16r8 -t upload --upload-port COM8
uvx --from platformio pio device monitor -p COM8 -b 115200
```

Kelyje neturi būti tarpų – žr. pagrindinį `README.md` (aplankas prijungiamas kaip diskas `subst`).

## Pirmas paleidimas

1. Ekrane: **AEROBOX**, tada **WI-FI SETUP** su tinklo vardu ir slaptažodžiu.
2. Telefonu prisijunkite prie Wi-Fi tinklo **AeroBox** (slaptažodis rodomas ekrane).
   Valdymo puslapis atsidaro pats; jei ne – naršyklėje įveskite **http://192.168.4.1**.
3. Skiltyje **Wi-Fi** pasirinkite namų tinklą (tik 2,4 GHz), įveskite slaptažodį → **Connect**.
   AeroBox pirmiausia išbando ryšį ir išsaugo tinklą tik pavykus; nepavykus parodo priežastį.
   Jei tinklo dabar nėra (pvz., to paties telefono „hotspot“) – **Save without testing**: AeroBox prisijungs,
   kai tik tinklas atsiras.
4. Ekrane – įprastas vaizdas su juosta **CHOOSE YOUR LOCATION** ir IP adresu. Grįžkite į namų Wi-Fi ir atidarykite
   tą adresą (pvz., **http://192.168.0.105**; dažnai veikia ir **http://aerobox.local**).
5. Skiltyje **Location** įveskite namų koordinates → **Save location**. Juosta dingsta.

Valdymo puslapio skiltys:

- **Status** – gyva ekrano peržiūra, Wi-Fi, duomenys, lėktuvai virš namų.
- **Display** – vaizdas, ryškumas, šriftas, radaro animacija, vienetai, skrydžio kortelė, spalvos, nakties režimas.
- **Track** – skrydžio sekimas.
- **Location** – namų vieta ir spindulys.
- **Wi-Fi** – kito tinklo pasirinkimas, „Forget this Wi-Fi network“.
- **System** – lėktuvų duomenų šaltinis, laiko juosta, programos atnaujinimas, perkrovimas, gamykliniai nustatymai, diagnostika.

**Mygtukas** (GPIO 21): trumpai – kitas vaizdas, 0,8 s – ryškumas, 5 s – pamiršti Wi-Fi.
Laikant mygtuką įjungimo metu atsidaro nustatymų tinklas.

## Maitinimas

- 4 × P2.5 64×32 panelėms naudokite **5 V ne mažiau kaip 8 A** bloką (geriau 10 A).
- ESP32 maitinkite per USB arba atskirai. **Nejunkite** panelių 5 V prie ESP32 5V kontakto, kai ESP32 prijungtas prie USB.
- **GND** tarp ESP32 ir panelių būtinas. Kiekvienai panelei maitinimą veskite atskirais, storesniais laidais.
- Programa riboja srovę pagal maitinimo bloką (numatyta 4 A). Panelių nustatymai puslapyje nerodomi; jei reikia,
  ribą galima pakeisti komanda (čia – 10 A bloko):

  ```bash
  curl -X POST -H "X-AeroBox: 1" -d "psu=10" http://aerobox.local/api/settings
  ```

## Jei neveikia

1. Paleiskite `3_ziureti_zurnala.bat`, paspauskite **RST** ir išsaugokite pirmas ~40 eilučių.
2. Ieškokite eilučių `paleidimas: …` (perkrovimo priežastis), `hub75: …`, `ekranas …` ir klaidų `E (…)`.
3. Jei AeroBox 3 kartus iš eilės nepasileidžia arba dingsta maitinimas, įsijungia **saugus režimas**: ryškumas
   apribojamas, atsidaro nustatymų tinklas, o puslapyje (System) parodoma priežastis.
4. Jei niekas nepadeda – `2_istrinti_viska_ir_ikelti.bat`.

## Atminties žemėlapis (16 MB)

| Adresas | Dalis |
|---|---|
| 0x0 | bootloader.bin |
| 0x8000 | partitions.bin |
| 0x9000 | nvs (nustatymai, Wi-Fi) |
| 0xF000 | ota_data_initial.bin |
| 0x20000 | firmware.bin (ota_0) |
| 0x420000 | ota_1 (atnaujinimui be laido) |
