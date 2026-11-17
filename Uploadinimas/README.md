# AeroBox – programos įkėlimas (Uploadinimas)

Programos versija: **0.12.3** · Plokštė: **ESP32-S3-N16R8** · Ekranas: 1× arba 4× P2.5 64×32 HUB75 (arba 1× 128×64)

## Kas yra šiame aplanke

| Failas | Kam skirtas |
|---|---|
| `2_istrinti_viska_ir_ikelti.bat` | **Pirmam kartui ir kai „niekas neveikia“.** Ištrina visą atmintį ir įkelia programą. Wi-Fi ir nustatymai bus ištrinti. |
| `1_ikelti.bat` | Įprastas atnaujinimas per USB. Wi-Fi ir nustatymai **išlieka**. |
| `3_ziureti_zurnala.bat` | Rodo plokštės žurnalą (log). Naudokite, jei kažkas neveikia. |
| `4_kompiliuoti_is_kodo_ir_ikelti.bat` | Sukompiliuoja iš `..\AeroBox` kodo, įkelia ir atnaujina `bin\` failus. |
| `bin\firmware_merged.bin` | Viskas viename faile, rašoma adresu **0x0** (naršyklės būdui ir gamybai). |
| `bin\firmware.bin` | Tik programa. Skirta atnaujinti **per valdymo puslapį** (System → Firmware update). |
| `bin\bootloader.bin`, `partitions.bin`, `ota_data_initial.bin` | Atskiros dalys (naudoja `1_ikelti.bat`). |

Skriptams reikia **uv** (jau įdiegtas šiame kompiuteryje) arba Python su `pip install esptool`.
Kitame kompiuteryje uv įdiegiamas taip: `powershell -c "irm https://astral.sh/uv/install.ps1 | iex"`.

## A būdas – skriptu (rekomenduojama)

1. Prijunkite ESP32-S3 USB kabeliu prie jungties **USB** (jei plokštė turi dvi – ne „UART/COM“).
2. Dukart spustelėkite `2_istrinti_viska_ir_ikelti.bat` (pirmą kartą) arba `1_ikelti.bat` (vėliau).
3. Įveskite COM prievadą (skriptas parodo rastus, pvz. `COM8`).
4. Palaukite „[OK] Ikelta“. Jei ekranas per 5 s neužsidega – paspauskite plokštės **RST**.

Jei rašo „Failed to connect“: laikykite **BOOT**, trumpai paspauskite **RST**, atleiskite **BOOT** ir bandykite dar kartą.

## B būdas – naršyklėje, nieko nediegiant

1. Chrome arba Edge naršyklėje atidarykite https://espressif.github.io/esptool-js/
2. Baudrate: `460800` → **Connect** → pasirinkite ESP32 prievadą.
3. (Pirmą kartą) **Erase Flash**.
4. Flash Address `0x0`, failas `bin\firmware_merged.bin` → **Program**.
5. Paspauskite plokštės **RST**.

Šis būdas ištrina Wi-Fi ir nustatymus (merged failas užrašo ir nustatymų sritį).

## C būdas – per valdymo puslapį (be laido)

Kai AeroBox jau veikia: atidarykite **http://aerobox.local** (arba IP, rodomą ekrane po prisijungimo) →
**System** → **Firmware update** → pasirinkite `bin\firmware.bin` → **Upload firmware**.
Nustatymai išlieka. Jei nauja programa nepasileidžia, po perkrovimo grąžinama senoji (rollback).
`firmware_merged.bin` čia netinka – puslapis jį atmes.

## D būdas – iš kodo (programuotojui)

```
cd ..\AeroBox
uvx --from platformio pio run -e esp32-s3-n16r8 -t upload --upload-port COM8
uvx --from platformio pio device monitor -p COM8 -b 115200
```

## Pirmas paleidimas (vartotojo valdymas, kaip TickerBox)

1. Ekrane: **AEROBOX**, tada **WI-FI SETUP · WI-FI NAME AeroBox, PASSWORD**.
2. Telefonu prisijunkite prie Wi-Fi tinklo **AeroBox** (slaptažodis rodomas ekrane).
   Valdymo puslapis atsidaro pats. Jei neatsidaro, naršyklėje įveskite **http://192.168.4.1**.
3. Skiltyje **Wi-Fi** pasirinkite namų tinklą (tik 2,4 GHz), įveskite slaptažodį → **Connect**.
   AeroBox pirma išbando ryšį; pavykus išsaugo ir persikrauna. Nepavykus parodo priežastį (blogas slaptažodis, tinklas nerastas…).
4. Grįžkite į namų Wi-Fi ir atidarykite **http://aerobox.local** (Android, jei neveikia – IP iš ekrano).

Tas pats valdymo puslapis veikia abiem atvejais:

- **Status** – gyva ekrano peržiūra (taip, kaip dabar atrodo panelės), Wi-Fi, duomenys, lėktuvai virš namų.
- **Display** – režimas (ir savas ekranas iš dviejų pusių), ryškumas (keičiasi iš karto), šriftas, radaro animacija ir žymekliai,
  vienetai kiekvienam matui atskirai, ką rodo skrydžio kortelė, spalvų tema ir atskiros spalvos, naktinis režimas, apvertimas 180°.
- **Track** – skrydžio sekimas.
- **Location** – koordinatės, spindulys, žemėlapis (kai telefonas turi internetą).
- **Wi-Fi** – tinklų paieška, prisijungimas prie kito tinklo, „Forget this Wi-Fi network“.
- **System** – panelių išdėstymas, spalvų tvarka, panelių numerių ir spalvų testai, maitinimo blokas (srovės ribojimas),
  laiko juosta, programos atnaujinimas, perkrovimas, gamykliniai nustatymai, diagnostika.

Mygtukas (GPIO 21): trumpai – režimas, 0,8 s – ryškumas, 5 s – pamiršti Wi-Fi.
Laikant mygtuką įjungimo metu – atsidaro AeroBox tinklas.

## 4 panelės (128×64): nustatymas

1. **System → Panel layout**: „4 panelės … S grandinė“ (numatytasis) arba „eilutėmis“.
2. **Show panel numbers**: kiekviena panelė rodo savo numerį (1 – arčiausiai ESP32) ir rodyklę **į viršų**.
   Jei rodyklė rodo žemyn arba numeriai ne savo vietose – rinkitės kitą išdėstymą arba „savas“
   (`stulpelis,eilutė,apversta;` kiekvienai panelei nuo ESP32, pvz. `1,0,1;1,1,0;0,1,0;0,0,1`).
3. Išsaugojus išdėstymą AeroBox persikrauna.
4. **Colour test**: užrašas RED turi šviesti raudonai, GREEN – žaliai, BLUE – mėlynai.
   Jei žalia rodoma mėlynai (ir atvirkščiai) – **System → Colour order → R-B-G**. Pritaikoma iš karto.
   (Bandomajame stende 2026-10-23 taip ir buvo – nustatyta R-B-G.)

## Maitinimas – svarbu su 4 panelėmis

- 4× P2.5 64×32: naudokite **5 V ne mažiau kaip 8 A** bloką (geriau 10 A). 5 V 4 A pakanka tik vienai–dviem panelėms.
- Valdymo puslapyje nurodykite savo bloką (**System → Panel power supply**): ryškumas automatiškai ribojamas, kad blokas neperkaistų ir įtampa nekristų.
- ESP32 maitinkite per USB arba atskirai. **Nejunkite** panelių 5 V prie ESP32 5V kontakto, kai ESP32 prijungtas prie USB.
- **GND** tarp ESP32 ir panelių – būtinas. Maitinimą kiekvienai panelei veskite atskirais, storesniais laidais (ne grandine per vieną kabelį).

## Kodėl praeitą kartą „niekas neveikė“ (rasta ir pataisyta)

Plokštės žurnalas parodė: `E hub75: truksta atminties` → `HUB75 inicializacija nepavyko`.
Plokštė iš tikrųjų veikė (Wi-Fi, lėktuvų duomenys), bet **ekranas nepasileido**: 4 panelėms reikia 2× 48 KB
ištisinės DMA atminties, o ji buvo išskiriama jau paleidus Wi-Fi ir tinklalapį, kai atmintis suskaidyta.

Pataisyta (v0.5):

- Ekranas inicializuojamas **prieš** Wi-Fi; grandinės buferis, Wi-Fi ir TLS buferiai perkelti į PSRAM.
- Jei ekranas vis tiek nepasileistų, valdymo puslapis tai parodo („Ekranas nepasileido“), o žurnale matyti, kiek atminties trūksta.
- Srovės ribojimas pagal maitinimo bloką; paleidimo ekranas taupesnis.
- **Saugus režimas**: jei AeroBox 3 kartus iš eilės lūžta ar dingsta maitinimas, ryškumas apribojamas, įjungiamas AeroBox tinklas, o puslapyje parodoma priežastis.
- Paleidimo priežastis (pvz. „maitinimo kritimas“) rodoma Sistema → Apie įrenginį.
- adsb.lol maršrutų API dabar grąžina 201 – kodas tai priima. (Šiuo metu tas API grąžina tuščią atsakymą,
  todėl maršrutų stulpelis tuščias – tai tiekėjo pusėje.)

Patikrinta su plokšte (2026-10-23): HUB75 256×32 inicializuojasi, Wi-Fi ir duomenys veikia,
valdymo puslapis ir jo API veikia, atnaujinimas per puslapį (C būdas) veikia – v0.6 įkelta būtent taip, per Wi-Fi.
4 panelės sujungtos teisingai (S grandinė).

## Kas nauja v0.12.3

- **Galima pasirinkti, kiek laiko rodoma viena skrydžio kortelė**: valdymo puslapyje Display → Flight cards →
  „Each card stays on screen for“ (3–30 s). Numatyta dabar 6 s (buvo 4 s). Tas pats laikas galioja „Flight cards“
  vaizde, AUTO cikle ir savo ekrano kortelės lange; sekamo skrydžio kortelė rodoma ne trumpiau nei 8 s.
- **Kortelė nebepersijungia anksčiau laiko**: anksčiau, lėktuvams susikeitus vietomis pagal atstumą (vienas
  priartėja, kitas nutolsta), kortelė galėjo pasikeisti jau po sekundės. Dabar kiekvienas lėktuvas rodomas visą
  nustatytą laiką, tada – kitas sąraše.
- **Žemėlapiuose vietoj „NO ROUTE“ rašoma „NO DATA“** (maršrutų žemėlapis, sekamo skrydžio žemėlapis, savo ekrano
  žemėlapio langas): lėktuvas skrenda kažkur, tiesiog jo maršruto duomenų nėra.

Peržiūra: `perziura\v0123_no_data.png`.

## Kas nauja v0.12.2

- **Maršrutų žemėlapyje rodomi ir lėktuvai be maršruto** (AUTO cikle, „Route map“ vaizde ir savo ekrano lange): tie
  patys 4 kaip radaro sąraše ir tomis pačiomis spalvomis (sekamas – baltas). Žinomas maršrutas – linija per lėktuvą,
  nežinomas – tik lėktuvas. Kai paryškinamas lėktuvas be maršruto, viršuje rašoma „THY1HB NO ROUTE … A21N 4/4“.
- AUTO cikle žemėlapis rodomas, kai bent vienas iš jų turi maršrutą (kitaip užtenka radaro); po 4 s kiekvienam.

Peržiūra: `perziura\v0122_lektuvai_be_marsruto.png`.

## Kas nauja v0.12.1

- **Maršruto linija eina per lėktuvą**: išvykimas → lėktuvas → paskirtis (ryškesnė nuskrista dalis). Anksčiau tai buvo
  tiesi (trumpiausia) linija tarp oro uostų, o tikri keliai prie Lietuvos dažnai lenkia uždarytą oro erdvę
  (Baltarusija, Rusija, Ukraina), todėl lėktuvas atrodydavo „nuklydęs“.
- **Netinkami maršrutai nerodomi**: maršrutus AeroBox gauna iš duomenų bazės pagal skrydžio numerį, ir kartais ji
  priskiria kito reiso maršrutą (pvz. THY1HB „IST > STR“, nors lėktuvas skrenda virš Lietuvos į šiaurę). Jei kelias per
  lėktuvą daug ilgesnis už tiesų (×1,3 + 200 km) arba toli nuo oro uostų lėktuvas skrenda tolyn nuo paskirties,
  maršrutas laikomas nežinomu: kortelėje „NO ROUTE DATA“, žemėlapyje nepiešiamas.

- Žemėlapyje „NO ROUTE“ nebeužima vietos apačioje: rašoma viršuje, šalia skrydžio numerio (pvz. „BAF637 NO ROUTE
  118KM AWAY“). Savo ekrano žemėlapio langas sekamam skrydžiui irgi turi viršutinę eilutę (skrydis ir maršrutas
  arba „NO ROUTE“); šalia kortelės – be teksto, nes kortelė pati rodo skrydį.

Peržiūros: `perziura\v0121_marsrutai_per_lektuva.png`, `perziura\v0121_no_route_virsuje.png`.

## Kas nauja v0.12

- **Naujas vaizdas „Route map“** (Display → *What to show* → *Route map (routes of nearby flights)*; mygtuku –
  „ROUTE MAP“): artimiausių skrydžių (iki 4) maršrutai viename Europos žemėlapyje, kiekvienas sava spalva
  (gintarinė, žydra, rožinė, violetinė; sekamas – baltas). Kas 4 s paryškinamas kitas: ryški linija, oro uostų kodai,
  lėktuvas su pulsuojančiu žiedu, o viršuje – skrydis, tipas, maršrutas ir „2/3“. Jei maršrutų dar nežinoma,
  rodomas priartintas žemėlapis aplink namus su lėktuvais.
- Jis gauna savo eilę ir **AUTO** cikle (po radaro), kai bent vieno artimo skrydžio maršrutas žinomas.
- **Savo ekrano „Route map“ langas** šalia kortelės rodo jos lėktuvą, kitaip – sekamą skrydį, o jei nieko nesekate –
  artimų skrydžių maršrutus paeiliui, kaip naujas vaizdas.
- **Savo ekrano pusėse nebegalima pasirinkti to paties lango**: kitos pusės pasirinkimas sąraše pilkas. Jei taip buvo
  išsaugota anksčiau, AeroBox pats pakeičia vieną pusę.
- *Test → Route map* rodo naują žemėlapį (su tikrais skrydžiais arba trimis pavyzdiniais per Europą).
- **Ryškumo skalė**: puslapio 0 % dabar = buvę 50 % (žemiau jų LED'uose dingsta blankios spalvos ir glotnių
  raidžių kraštai), 100 % – kaip anksčiau. Ta pati skalė – nakties ryškumui ir mygtuko lygiams (0 / 25 / 50 / 75 /
  100 %). Anksčiau nustatyti mažesni ryškumai (ir naktinis) pakeliami iki naujo 0 %. Saugus režimas (po maitinimo
  kritimų) ryškumą tebeapriboja, bet jūsų nustatymo nebekeičia.

Peržiūra: `perziura\v012_marsrutu_zemelapis.png`.

## Kas nauja v0.11.2

- Sekimas: kai iki nusileidimo liko mažiau nei minutė, rodoma „LANDING NOW“ (anksčiau „LANDS IN 0 MIN“),
  puslapyje – „Landing now“.
- Ilgas šaukinys (pvz. ARGON316) sekimo kortelėje lieka didelis: registracija nusileidžia į maršruto eilutę, kaip
  skrydžių kortelėje (glotniu DejaVu šriftu 7–8 simbolių vis tiek mažėja – per platus).
- Žemėlapyje oro uosto kodas nebeužlenda po lėktuvu ar namų kryželiu: perkeliamas į kitą pusę, toliau, virš ar po
  tašku (pvz. nusileidus – šalia lėktuvo, ne po juo).
- Savo ekrano žemėlapio lange sekamas lėktuvas irgi turi pulsuojantį žiedą, kaip radare.
- Kol rodomas „LOOK UP!“, sekimo ir AUTO ratas stovi: po jo kortelė ar žemėlapis rodomi likusį laiką
  (anksčiau tas laikas dingdavo).
- Skirtukas **Test** → **Tracked flight**: 16 s paeiliui rodoma sekamo skrydžio kortelė ir žemėlapis; jei nieko
  nesekama – pavyzdys BAW882 London → Vilnius.

Peržiūra: `perziura\v0112_sekimas.png`.

## Kas nauja v0.11.1

- **Sekamas skrydis (TRACKING) keičiasi ratu**: skrydžio kortelė (8 s) → maršruto žemėlapis per visą ekraną (8 s) →
  radaras (10 s, tik kai lėktuvas radaro spindulyje ir įjungta *Track → Add the radar to the rotation*) → vėl kortelė.
  Anksčiau, lėktuvui atskridus į spindulį, rodytas vien radaras, todėl TRACKING atrodė lygiai kaip RADAR.
- **Žemėlapis per visą ekraną**: viršuje skrydis ir būsena („LANDS IN 45 MIN 16:02“, „118KM AWAY“, „LANDED AT RIX“),
  po jais Europos kontūras, maršrutas (nuskrista dalis ryškesnė), oro uostai, namai ir lėktuvas su pulsuojančiu žiedu,
  kaip radare. Kai ilgas maršrutas visas netelpa, rodoma likusi dalis (lėktuvas, paskirtis, namai); be maršruto
  duomenų – lėktuvas ir namai. Lėktuvui priskridus prie krašto, vaizdas parenkamas iš naujo.
- **AUTO**: sekamo skrydžio kortelė ir žemėlapis gauna savo eilę cikle. Radaras nebeužima viso ekrano, kol sekamas
  lėktuvas spindulyje – jis vis tiek matosi radare (baltas, su žiedu) ir kortelėse.
- Sekimo kortelė: be maršruto atstumas nebekartojamas dviejose eilutėse – jo vietoje kilimo/leidimosi greitis;
  ypatingiems lėktuvams (pvz. kariniam A400) – rožinė juosta „MILITARY“, kaip skrydžių kortelėse.
- Pataisyta: „Stop“ po perkrovimo grąžindavo AUTO, o ne buvusį vaizdą; savo ekrano žemėlapis rodydavo kitą lėktuvą,
  kai sekamas toliau nei spindulys; žemėlapis nepersistatydavo, kai lėktuvas be maršruto išskrisdavo už krašto, ir
  rodydavo oro uosto kodą krašte, kai pats oro uostas už vaizdo; *Test → Route map* kartais rodydavo „NO ROUTE“.

Peržiūra: `perziura\v0111_sekimas.png`.

## Kas nauja v0.11

Naujas valdymo puslapio skirtukas **Test**: mygtukai kelias sekundes parodo kiekvieną naują funkciją ekrane
(su tikrais lėktuvais, o jei jų nėra – su pavyzdiniais), o viršuje matosi gyva ekrano peržiūra. Įjungti ir išjungti –
**Display → Extras**.

- **„Lėktuvas virš galvos“ (LOOK UP!)**: kai lėktuvas praskrenda arčiau nei pasirinkta (1 / 2 / 3 / 5 km, numatyta
  2 km), 8 s rodoma pulsuojanti antraštė, didelis lėktuvas, perskrendantis per namus, ir kuris tai lėktuvas, aukštis,
  atstumas ir pusė, į kurią žiūrėti (pvz. „1.2KM NE“). Kiekvienas lėktuvas – vieną kartą, ne dažniau kaip kas minutę.
  Prie oro uosto rinkitės mažesnį atstumą arba *Off*. Peržiūra: `perziura\v011_look_up.png`.
- **Ypatingi lėktuvai**: A380, 747, An-124 ir kiti reti (RARE AIRCRAFT), kariniai (MILITARY), sraigtasparniai
  (HELICOPTER), verslo lėktuvai (BUSINESS JET). Atpažįstami pagal duomenų šaltinio žymes, ADS-B kategoriją ir tipų
  sąrašus. Kortelėje – rožinė juosta su užrašu, radare – rožinis žiedas, puslapio lentelėje – rožinis užrašas.
  Reti ir kariniai rodomi kortelėse, net jei nėra tarp 5 artimiausių. Peržiūra: `perziura\v011_reti_lektuvai.png`.
- **Maršruto žemėlapis** – naujas savo ekrano langas (*Route map*): supaprastintas Europos kontūras, skrydžio kelias
  nuo išvykimo iki atvykimo oro uosto (nuskrista dalis ryškesnė), lėktuvas ir jūsų namai. Šalia kortelės rodo jos
  lėktuvą. Kontūras sudarytas rankomis (~650 taškų), tikslumas 10–30 km; už Europos (pvz. į JAV) maršrutas išeina
  už krašto. Peržiūra: `perziura\v011_marsruto_zemelapis.png`.
- **Išvykimo lentos efektas**: pasikeitus skrydžio kortelei, raidės ~1 s „sukasi“ ir sustoja iš kairės į dešinę,
  kaip oro uosto lentoje. Peržiūra: `perziura\v011_isvykimo_lenta.png`.

## Kas nauja v0.10.1

- **Laikrodžio dvitaškis** nebe dideli kvadratai: abu taškai tokio pat storio kaip skaitmenų linijos (Classic 5×7 –
  vienas šrifto „taškas“, 3×3 dideliame laikrodyje, 2×2 mažame ir savo ekrano lange; glotniais šriftais – apvalinti)
  ir tolygiai prigęsta bei vėl sušvinta kas 2 s, o ne mirksi įjungta/išjungta.
- **Laikrodžio išdėstymas** tolygesnis: laikas 4 taškai nuo viršaus, data, orai ir lėktuvų skaičius po juo vienodais
  tarpais, kraštuose ne mažiau kaip 4 taškai (Classic šriftu dabar „WED 28 OCT 2026“ ir „5 FLIGHTS IN 20KM“, kad
  netrintų kraštų). Tas pats savo ekrano laikrodžio lange. Peržiūra: `perziura\v0101_laikrodis.png`.

## Kas nauja v0.10

Viskas – valdymo puslapio skiltyje **Display**. Įkėlus nustatymai išlieka; ankstesnis „Units: Metric/Imperial“
virsta atitinkamais atstumo ir temperatūros vienetais.

- **Savas ekranas** (*What to show → Custom screen*): ekranas padalijamas į dvi kvadratines 64×64 puses, kiekvienai
  pasirenkate langą – radaras, skrydžio kortelė, artimiausių lėktuvų sąrašas, laikrodis su orais arba sekamas skrydis.
  Pvz., kairėje radaras, dešinėje kortelė: radaras pažymi kortelėje rodomą lėktuvą (jo spalva + žiedas, kiti pilki).
  Radaras šalia sąrašo – spalvos sutampa su sąrašu. Mygtukas dabar perjungia ir šį vaizdą (CUSTOM).
  Vienoje 64×32 panelėje rodomas kairės pusės langas per visą ekraną. Peržiūra: `perziura\v010_savas_ekranas.png`.
- **Vienetai kiekvienam matui atskirai**: atstumas (km / mylios / jūrmylės), aukštis (skrydžio lygis FL / pėdos / metrai),
  kilimo greitis (ft/min / m/s), greitis (kt / km/h / mph), temperatūra (°C / °F), laikrodis (24 / 12 val.).
  Skrydžio kortelėje kilimo greitis rodomas tik pasirinktais vienetais (vienas, ne du). Ilgas aukštis (pvz. „23725FT“)
  atstumą pastumia į dešinę.
- **Ką rodo skrydžio kortelė**: varnelės – tipas, registracija, maršrutas, aukštis, atstumas, greitis, kursas,
  kilimas/leidimasis, progreso juosta, kortelės numeris (1/5). Skrydžio numeris – visada. 64×64 lange išjungtos eilutės
  vietos neužima.
- **Spalvų temos**: Default, Amber (seni aviacijos ekranai), Ice blue, Green radar, White (didžiausias kontrastas),
  Night red (tamsoje neakina). Skiltyje *Change individual colours* – 11 spalvų atskirai (skrydžio nr., tipas, aukštis,
  kursas, tekstas, kyla, leidžiasi, progreso juosta, laikrodis, data, radaras); pakeitus temą tampa *Custom*.
  Peržiūra: `perziura\v010_spalvu_temos.png`.
- Sekamo skrydžio laukimo užrašai siauruose ekranuose sutrumpinti („NOT TRACKING“), kad nebūtų nukirpti.
- Peržiūra: `perziura\v010_vienetai_ir_laukai.png` (pėdos + jūrmylės + m/s, metrai + km/h, kortelė be dalies laukų).
- Atsarginė kopija prieš šiuos pakeitimus: `..\AeroBox_v0.9.1_atsargine` (senos v0.7 ir v0.8 kopijos – šiukšlinėje).

## Kas nauja v0.9.1

Versijos numeris pakeistas, kad būtų galima atskirti nuo jau įkeltos v0.9 (paleidimo ekrane ir System skiltyje – „0.9.1“).

- **Ilgi šaukiniai skrydžio kortelėje lieka dideli.** Pvz., „ARGON316“ (8 simboliai) Classic 5×7 šriftu dvigubo
  dydžio užima 94 taškus ir šalia tipo bei registracijos netilpdavo, todėl buvo piešiamas mažu šriftu. Dabar tokiai
  kortelei registracija nusileidžia į maršruto eilutės dešinę (vietoj „1/5“), o „1/5“ pakyla po tipu. Kitos
  kortelės nepasikeitė. Classic ir Smooth mono šriftais telpa visi iki 8 simbolių; Smooth (DejaVu) raidės daug
  platesnės – 7–8 simbolių šaukiniai juo vis dar mažinami. Peržiūra: `perziura\v09_ilgas_saukinys.png`.
- Taip pat yra visi žemiau išvardyti v0.9 pakeitimai (ir m/s šalia FPM, jei įkeltoje v0.9 jo dar nebuvo).

## Kas nauja v0.9

- **Lėktuvai juda tik pagal gautus duomenis.** Anksčiau tarp atnaujinimų lėktuvas buvo stumiamas pagal greitį ir kursą
  (iki 3 min), todėl posūkyje ar dingus signalui nuskrisdavo per toli ir, atėjus naujiems duomenims, šokdavo atgal.
  Dabar niekas nespėjama: gavus naują tikrą poziciją, lėktuvas tolygiai nuslenka iki jos (per tiek laiko, kiek praėjo
  tarp dviejų paskutinių pozicijų, 1–10 s) ir laukia kitos. Todėl ekrane jis rodomas keliomis sekundėmis vėliau,
  bet niekada ne toliau, nei rodo duomenys. Sekamo skrydžio atstumas ir laikas iki nusileidimo – irgi iš paskutinės tikros pozicijos.
- **Progreso juostoje** vietoj mirksinčio lėktuvėlio – baltas glotnus trikampis kaip radare (kortelėse ir sekamo
  skrydžio ekrane; pilkas, kai su sekamu lėktuvu dingo ryšys).
- **Radaro trikampiai** ~20 % mažesni.
- **Radare nebėra pėdsakų** už lėktuvų – lieka tik žymeklis. Display → *Radar animation* pirmas pasirinkimas
  dabar vadinasi *None (calm)* (be animacijos). Su žymekliu *Dots* skrydžio kryptis nebesimato – kryptį rodo
  rodyklės ir trikampiai.
- **Radaro sąraše – 4 artimiausi lėktuvai** (buvo 3), šriftas tas pats 3×5. Spindulys („20KM“) perkeltas į kairį
  viršutinį radaro kampą, „+N“ (kiek lėktuvų netilpo į sąrašą) – į dešinį. Ketvirtojo lėktuvo spalva – violetinė.
  Peržiūra: `perziura\v09_radaras_4_lektuvai.png`.
- **Kai maršruto nėra**, vietoj „---“ rašoma **NO ROUTE DATA** (64×32 ekrane – „NO ROUTE“), puslapio lentelėje – „no data“.
- **M ir W raidės** glotniame šrifte nupieštos iš naujo: tiesūs kamienai tiksliai ant taškų, siauresnės, be „išplaukusių“
  įstrižainių (visuose trijuose dydžiuose). Palyginimas: `perziura\v09_M_W_raides.png`.
- **Įvedus sekamą skrydį, ekranas iškart persijungia į TRACKING** ir ten lieka (ir nusileidus) – kitą vaizdą
  pasirenkate patys (Display arba mygtuku). *Stop* grąžina ankstesnį vaizdą. Jei įjungta *Show it on the radar…*,
  sekamam lėktuvui esant spindulyje rodomas radaras ir TRACKING vaizde (dar 10 s jam išskridus, kad nemirgėtų).
- **Greičio vienetai**: Display → *Speed* – mazgai (kt), km/h arba mph (ekrane ir puslapyje).
- **Kilimo / leidimosi greitis skrydžio kortelėje** – šalia FPM (pėdos per minutę) dar ir **m/s**, kai pasirinkti
  metriniai vienetai: „↓ 1200FPM 6M/S“. Ženklų +/− nebėra – kryptį rodo rodyklė ir spalva (žalia – kyla,
  raudona – leidžiasi). m/s rodomi sveikais skaičiais, nes ADS-B duomenų žingsnis – 64 ft/min (0,33 m/s).
  Kad 5×7 šriftu tilptų, kursas toje eilutėje rodomas be „HDG“: „245°“. Su imperiniais vienetais – kaip anksčiau,
  tik FPM ir „HDG 245“. Peržiūra: `perziura\v09_kilimo_greitis_ms.png`.
- **Naujas šriftas palyginimui**: Display → Font → *Smooth mono (JetBrains Mono)* – visos raidės vienodo pločio
  (skrydžio nr., laikrodis, tekstas). Smulkus tekstas: 7 taškų aukščio JetBrains Mono gautų tik 5 taškus raidei
  (M, N, W susilietų), todėl jis pieštas tikromis proporcijomis – 6 taškai raidei, kraštai ant taškų, nulis be taško viduje.
  Licencija SIL OFL 1.1 – galima naudoti parduodamame produkte (tekstas `..\AeroBox\components\gfx\ttf\OFL.txt`,
  autoriai nurodyti puslapyje System → Fonts). Palyginimas: `perziura\v09_sriftai_smooth_mono.png`.
- Peržiūra: `perziura\v09_ekranai.png`.

## Kas nauja v0.8

- **Viskas angliškai** – ekrano užrašai ir valdymo puslapis (produktas kitoms šalims).
  Puslapio skiltys: **Status, Display, Track, Location, Wi-Fi, System**. Kodo komentarai ir žurnalas (log) liko lietuviški.
- **Glotnus šriftas** (Display → Font → *Smooth*, numatytasis): proporcingas, švelniais kraštais (DejaVu Sans –
  licencija leidžia naudoti parduodamame produkte). Dideli užrašai (laikrodis, skrydžio nr., maršrutas,
  pranešimai) glotnūs visuose šriftuose, išskyrus *Classic pixel 5×7*. Taškiniai 3×5 ir 4×5 liko pasirinkimui.
  64×32 ekrane smulkus tekstas rodomas 4×5.
- **Laikrodis su orais**: ten, kur buvo lėktuvų skaičius – orai (piktograma, temperatūra, aprašymas;
  Open-Meteo, kas 15 min). Žemiau – „3 FLIGHTS WITHIN 20KM“, o kai tuščia – „NO FLIGHTS IN 20KM“.
  64×32 ekrane apatinė eilutė kas 4 s keičiasi: orai / lėktuvai.
- **Radaras**: numeriai nuimti, liko tik spalvos (tokios pat ir sąraše). „N“ visada vidutinio 4×5 šrifto,
  sąrašas dešinėje – mažu 3×5, telpa 3 lėktuvai. Naujas pasirinkimas **Display → Aircraft on the radar**:
  rodyklės (kaip buvo), taškai arba glotnūs trikampiai (rodo tikslią kryptį).
- **Skrydžio sekimas** (skiltis **Track** arba mygtukas **Track** prie lėktuvo sąraše Status skiltyje).
  Įveskite šaukinį (BTI658), skrydžio nr. (BT658), registraciją (YL-ABW) arba ICAO adresą (502D96).
  Ekranas *Tracked flight* rodo skrydį, tipą, registraciją, maršrutą, aviakompaniją, aukštį, greitį,
  atstumą nuo namų, **kiek liko iki nusileidimo ir kada nusileis**, progreso juostą; nusileidus – „LANDED AT RIX“.
  Automatiniame režime šis ekranas įsiterpia tarp kitų; mygtuku pasiekiamas kaip „TRACKING“.
  - Priedas: kai sekamas lėktuvas įskrenda į radaro spindulį, rodomas radaras – lėktuvas baltas, su
    pulsuojančiu žiedu, sąraše pirmas (išjungiama: Track → *Show it on the radar…*).
  - Laikas iki nusileidimo = likęs atstumas iki paskirties oro uosto / greitis, todėl apytikris (be tūpimo manevrų).
  - Sekamas skrydis išlieka perkrovus; jei šiandien jau nusileido, rytoj tas pats reisas bus rastas vėl.
  - Kai kurios aviakompanijos skrenda kitu šaukiniu nei skrydžio nr. (Ryanair FR1234 → RYR5PK) –
    tada įveskite šaukinį (jį rodo Flightradar24).
- **Maršrutai vėl veikia**: vietoj tuščio adsb.lol routeset – adsbdb.com (iš kur, į kur, oro uostų koordinatės,
  miestas, aviakompanija). Kortelėse vėl matosi maršrutas ir progreso juosta.
- **Vienetai ir laikas**: Display → *Units* (km/°C arba mi/°F) ir *Clock* (24 val. arba 12 val. AM/PM).
  Daugiau laiko juostų (JAV, Kanada, Azija, Australija…).
- Pataisyta sena klaida: puslapyje du laukai turėjo tą patį id, todėl koordinačių laukas galėjo rodyti „1“,
  o spalvų tvarka neišsisaugodavo mygtuku „Save“.
- Didieji buferiai perkelti į PSRAM: statinė vidinė RAM 55 KB (v0.7 – apie 63 KB), daugiau vietos HUB75 DMA.
- Peržiūros: `perziura\v08_laikrodis_orai.png`, `v08_radaras_zymekliai.png`, `v08_sekamas_skrydis.png`,
  `v08_sriftai_korteles.png`, `v08_sistema.png`.

**Dėl pardavimo:** adsb.fi, adsbdb maršrutai ir Open-Meteo orai nemokami tik asmeniniam / nekomerciniam
naudojimui. Parduodamam produktui reikės savo serverio ir komercinių šaltinių (lėktuvai, maršrutai, orai).

## Kas nauja v0.7

- **Duomenų šaltinis** (Sistema → Duomenys): numatytas **adsb.fi** – nemokamas, tik asmeniniam naudojimui ir bandymams, atnaujina kas 3 s (jų riba ~1 užklausa per sekundę). Galima pasirinkti ir adsb.lol.
- **adsb.lol taisyklės**: adsb.lol leidžia 500 užklausų per parą vienam namų
  tinklui (visiems įrenginiams kartu). AeroBox turi paros „biudžetą“ – 450 užklausų (10 % atsargos) ir
  leidžia jį ten, kur reikia: kai šalia lėktuvai – atnaujina **kas 15 s**, kol yra rezervo (iki 20 iš eilės),
  kai dangus tuščias – kas 5 min, naktinio režimo valandomis – kas 10 min. Rezervas prisipildo po 1 kas ~3 min
  ir išlieka perkrovus. Tarp atnaujinimų lėktuvai **juda tolygiai** pagal greitį ir kursą (iki 3 min).
  - Sistema → Duomenys: „Pagal adsb.lol taisykles“ arba, tik bandymams, kas 5 / 10 / 15 s (viršija paros ribą).
  - Jei adsb.lol vis tiek riboja (429) – AeroBox laukia, kiek prašo serveris; ekrane „RIBOJA ADSB.LOL“,
    puslapyje „Riboja adsb.lol, dar X s“. Sistema → Apie įrenginį: rezervas ir kada kita užklausa.
  - Maršrutų paieška (adsb.lol ją šiuo metu grąžina tuščią) šiame režime išjungta – ji irgi skaičiuotųsi į ribą.
- **Radaro animacijos** (Ekranas → Radaro animacija): *Pėdsakai* (ramus, be spindulio), *Sonaras*
  (banga iš namų kas 3 s, lėktuvas sublyksi, kai banga jį pasiekia) ir *Klasikinis spindulys*.
  Visuose – blėstantis lėktuvo pėdsakas (tikras kelias per ~30 s). Peržiūra: `perziura\radar_stiliai.gif`.
- **Šriftai** (Ekranas → Šriftas): *Mažas 3×5* (kaip buvo), *Platus 4×5* (aiškesnės raidės N, M, W, 0/O)
  ir *Didelis 5×7* (geriausiai įskaitomas iš arti; 128×64 ekrane – su atskiru kortelės ir radaro sąrašo
  išdėstymu, 64×32 ekrane vietoj jo naudojamas 4×5). Peržiūra: `perziura\font0/1/2_128x64.png`.

## Kas nauja v0.6

- **Radaras aiškesnis**: lėktuvai – rodyklės skrydžio kryptimi; trys artimiausi pažymėti numeriu 1, 2, 3
  ir savo spalva, tokia pati kaip sąraše dešinėje. Lėktuvai nebeblėsta, spindulys subtilus, namai – baltas kryželis.
- Kilimas / leidimasis – aiškios ↑ ↓ rodyklės (radare ir skrydžio kortelėje).
- **Spalvų tvarka** (Sistema): sukeistos spalvos taisomos be litavimo.
- **Gyva ekrano peržiūra** valdymo puslapyje (Būsena).
- Pranešimai (ryškumas, IP) – per visą plotį, fonas patamsintas; nauja pradžios užsklanda.
- Blankus tekstas nebedingsta naktį; kortelėje be maršruto nebėra tuščios juostos.
- Keičiant Wi-Fi iš namų tinklo ekrane rodoma „JUNGIAMASI PRIE …“.
- Panelių numerių testas paleidžiant rodomas tik kol Wi-Fi nenustatytas (ir bet kada – iš puslapio).
- Srovės ribojimas veikia jau nuo pradžios ekrano.

## Jei vis tiek neveikia

1. Paleiskite `3_ziureti_zurnala.bat`, paspauskite **RST** ir atsiųskite pirmas ~40 eilučių.
2. Ieškokite eilučių: `paleidimas: …` (priežastis), `hub75: …`, `ekranas …`, klaidų `E (…)`.
3. Vaizdas pasislinkęs ar mirga – žr. `..\AeroBox\README.md` skyrių „Jei vaizdas netinkamas“.

## Atminties žemėlapis (16 MB)

| Adresas | Dalis |
|---|---|
| 0x0 | bootloader.bin |
| 0x8000 | partitions.bin |
| 0x9000 | nvs (nustatymai, Wi-Fi) |
| 0xF000 | ota_data_initial.bin |
| 0x20000 | firmware.bin (ota_0) |
| 0x420000 | ota_1 (atnaujinimui per puslapį) |
