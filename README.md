# AeroBox

LED ekranas, rodantis lėktuvus virš jūsų: ESP32-S3-N16R8 plokštė su HUB75 panelėmis (128×64).
Ekrano programa parašyta C kalba (ESP-IDF 5.5) ir kompiliuojama per PlatformIO; serveris – Python (FastAPI).

| Aplankas | Kas tai |
|---|---|
| [SkyPanel](SkyPanel) | **AeroBox** ekrano programa (vidinis projekto vardas – `skypanel`) |
| [SkyPanel_Server](SkyPanel_Server) | AeroBox serveris: lėktuvų duomenys, registracija, atnaujinimai ([instrukcija](SkyPanel_Server/README.md)) |
| [Uploadinimas](Uploadinimas) | Programos įkėlimas į ekraną laidu ([instrukcija](Uploadinimas/README.md)) |
| [PLANAS.md](PLANAS.md) | Tyrimas, rastos klaidos ir kas liko iki pardavimų |

## Kaip sukurtas šis projektas

Prasidėjo viskas vibecodinimu, nes negalvojau, kad kažkas pavyks, o dabar jau priėjom prie to, kad per mažai žinių turiu, jog ištobulinti iki galutinio, parduodamo varianto ^^

## Kompiliavimas

Ekrano programai reikia [uv](https://docs.astral.sh/uv/). Kelyje neturi būti tarpų (ESP-IDF jų nepriima),
todėl aplankas prijungiamas kaip diskas:

```bash
subst S: "C:\kelias\iki\sio\aplanko"
cd /d S:\SkyPanel
uvx --from platformio pio run -e esp32-s3-n16r8
```

Pirmą kartą PlatformIO atsisiunčia ESP-IDF ir bibliotekas (`managed_components`) pats. Valdymo puslapio
suspaustą kopiją (`index.html.gz`) prieš kiekvieną kompiliavimą sukuria `gz_page.py`.

Įkėlimui į ekraną skirtas `Uploadinimas\4_kompiliuoti_is_kodo_ir_ikelti.bat`: jis sukompiliuoja programą ir
įrašo `.bin` failus į `bin\`.
