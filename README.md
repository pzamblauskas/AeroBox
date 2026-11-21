# AeroBox

LED ekranas, rodantis lėktuvus virš jūsų: ESP32-S3-N16R8 plokštė su HUB75 panelėmis (128×64).
Ekrano programa parašyta C kalba (ESP-IDF 5.5) ir kompiliuojama per PlatformIO; serveris – Python (FastAPI).

| Aplankas | Kas tai |
|---|---|
| [AeroBox](AeroBox) | Ekrano programa |
| [AeroBox_Server](AeroBox_Server) | Serveris: lėktuvų duomenys, registracija, atnaujinimai ([instrukcija](AeroBox_Server/README.md)) |
| [Uploadinimas](Uploadinimas) | Programos įkėlimas į ekraną laidu ([instrukcija](Uploadinimas/README.md)) |
| [PLANAS.md](PLANAS.md) | Tyrimas, rastos klaidos ir kas liko iki pardavimų |

## Kaip sukurtas šis projektas

Prasidėjo viskas vibecodinimu, nes negalvojau, kad kažkas pavyks, o dabar jau priėjom prie to, kad per mažai žinių turiu, jog ištobulinti iki galutinio, parduodamo varianto ^^

## Pakeitimai

**1.2.5 (2026-11-21)** – patikimesnis Wi-Fi nustatymas bet kokiam tinklui ir slaptažodžiui (lietuviškos raidės,
simboliai, 12–13 kanalai, „hotspot“ – mygtukas **Save without testing**); namų vieta nebenustatoma pagal IP –
kol ji nepasirinkta, ekrane rodoma **CHOOSE YOUR LOCATION** ir puslapio adresas. Išsamiau ir kaip įkelti –
[Uploadinimas/README.md](Uploadinimas/README.md). Paruošti failai – [Uploadinimas/bin](Uploadinimas/bin).

## Kompiliavimas

Ekrano programai reikia [uv](https://docs.astral.sh/uv/). Kelyje neturi būti tarpų (ESP-IDF jų nepriima),
todėl aplankas prijungiamas kaip diskas:

```bash
subst S: "C:\kelias\iki\sio\aplanko"
cd /d S:\AeroBox
uvx --from platformio pio run -e esp32-s3-n16r8
```

Pirmą kartą PlatformIO atsisiunčia ESP-IDF ir bibliotekas (`managed_components`) pats. Valdymo puslapio
suspaustą kopiją (`index.html.gz`) prieš kiekvieną kompiliavimą sukuria `gz_page.py`.

Įkėlimui į ekraną skirtas `Uploadinimas\4_kompiliuoti_is_kodo_ir_ikelti.bat`: jis sukompiliuoja programą ir
įrašo `.bin` failus į `bin\`.
