# AeroBox: tyrimo išvados, kas padaryta ir kas liko

Data: 2026-11-10. Programa 1.2.0, serveris 1.0.

## 1. Pavadinimas

- **„SkyPanel“ naudoti negalima.** Tai registruotas ARRI prekių ženklas LED šviestuvams (filmų apšvietimas).
  LED ekranui tas pats pavadinimas keltų tiesioginį konfliktą.
- **Pasirinkta „AeroBox“.** Paieškoje radau tik nesusijusius ženklus: JAV „AEROBOX“ reklamos paslaugoms
  (35 klasė) ir negaliojančius kitose klasėse. Elektronikos (9 klasė) ar ES registracijos neradau.
- **Tai nėra teisinis patikrinimas.** Prieš gaminant pakuotes patikrinkite EUIPO (`euipo.europa.eu/eSearch`)
  ir TMview bazes 9 klasėje, o geriausia užregistruokite ženklą (ES ~850 €).
- Pavadinimas kode yra vienoje vietoje kiekviename projekte, todėl prireikus pakeisti nesudėtinga.

Pakeista: ekrano užrašai, valdymo puslapis, tinklo vardas `AeroBox`, adresas `aerobox.local`, serveris.
2026-11-17 pakeisti ir aplankų vardai bei vidinis projekto vardas (`aerobox`). Ekraną su senesne nei 1.2.4
programa vieną kartą reikia atnaujinti laidu; nustatymai perkeliami automatiškai.

## 2. Konkurentai (ką radau)

| | FlightWall Mini | AeroBox |
|---|---|---|
| Ekranas | 128×64 LED | 128×64 LED |
| Kaina | 175 USD | – |
| Valdymas | iOS / Android programėlė | naršyklė (nieko diegti nereikia) |
| Atnaujinimas | kas 10 s | kas 10 s |
| Skrydžio sekimas | numeris, šaukinys, registracija | tas pats |
| Ryškumo grafikas | yra | yra (nakties režimas) |
| Žemėlapiai, radaras, padalintas vaizdas | – (nerasta) | yra |

Naudotojų pastabos apie FlightWall: trūksta išjungimo mygtuko programėlėje, pasitaiko strigimų pirmojo
nustatymo metu, norisi daugiau informacijos apie zoną. Jūsų pranašumai: nereikia programėlės, žemėlapiai,
nustatymas be perkrovimo.

## 3. Rastos ir ištaisytos klaidos

| Klaida | Pasekmė | Taisymas |
|---|---|---|
| Būsenos atsakymo buferis per mažas (6 KB) | Su 32 lėktuvais ir atnaujinimo laukais puslapis galėjo rodyti „No connection“ | 8 KB |
| Bendras nustatymų tinklo slaptažodis visiems ekranams | Kaimynas galėjo prisijungti nustatymo metu; neatitinka ES reikalavimų | Kiekvienam ekranui savas, rodomas ekrane |
| Serveris tikrino nežinomą raktą duomenų bazėje kiekvieną kartą | Lengva perkrauti serverį netikrais raktais | Atmintis + riba pagal IP |
| Serveris pasitikėjo `X-Forwarded-For` iš bet ko | Ribas pagal IP buvo galima apeiti | Tik iš Caddy tinklo |
| Bet kas galėjo registruoti neribotai ekranų | Duomenų bazės augimas, naudojimasis jūsų duomenimis | Vienas numeris = vienas įrašas, leidžiamų sąrašas |
| Registruotas ekranas galėjo klausti apie bet kurią pasaulio vietą | Tiekėjo užklausų (ir sąskaitos) išpūtimas | 4 vietos per valandą ekranui, bendra zonų riba |
| Programos įkėlimas skaitė visą failą prieš tikrindamas dydį | Atminties išnaudojimas | Srautinis skaitymas su riba |
| Vidiniai sąrašai serveryje niekada nevalomi | Atmintis auga mėnesiais | Valymas kas minutę |
| Nauja zona be duomenų: kiekviena užklausa laukė 6 s | Gijų išsekimas esant apkrovai | Laukiama tik pirmas sekundes |
| Maršrutų „palaukite“ (503) pildė žurnalą įspėjimais | Tikros klaidos paskęsta | 503 nebežymimas kaip klaida |

## 4. Kas padaryta šiame etape

**Ekrano programa 1.2.0** (`Uploadinimas\bin`):
- pavadinimas AeroBox, aviacinės spalvos valdymo puslapyje (juodas šiltas fonas, gintaro akcentai);
- savas nustatymų tinklo slaptažodis kiekvienam ekranui;
- ekranas registruodamasis siunčia savo numerį.

**Serveris 1.0** (`AeroBox_Server`):
- trys režimai: vienas procesas bandymams; `core` + kiek reikia `api` kopijų gamybai;
- saugumo ribos (žr. `SAUGUMAS.md`), ekranų valdymas `/admin` puslapyje (paieška, blokavimas, leidžiamų sąrašas);
- licencijuoto tiekėjo jungtis, nustatoma per `.env`;
- gamybos režimas nepasileidžia su silpnais slaptažodžiais;
- diegimo failai ir instrukcija (`DIEGIMAS.md`).

**Patikrinta:** 44 automatiniai testai; apkrovos testas su 1 000 ekranų per du procesus (95 % atsakymų
per 1,8 ms, tiekėjo paklausta 146 kartus per 2,5 min.).
**Nepatikrinta:** Docker / Caddy failai tikrame serveryje; programa 1.2.0 tikrame ekrane.

## 5. Mastelis

| Ekranų | Ko reikia | Apytikslė kaina per mėn. |
|---|---|---|
| iki ~3 000 | 1 serveris (2 vCPU, 4 GB), 2 `api` kopijos | 6–12 € |
| ~3 000–20 000 | 1 serveris (4–8 vCPU), 4–8 `api` kopijos | 20–60 € |
| 20 000+ | keli serveriai skirtinguose regionuose, bendra talpykla (Redis) | nuo ~150 € |

Skaičiai paremti matavimu šiame kompiuteryje (viena `api` kopija atlaikė ~120 užklausų/s su atsarga) ir
yra vertinimas, ne garantija. Trečiai eilutei reikės papildomo darbo. Su nemokamais šaltiniais riba yra
tiekėjas (~1 užklausa/s, t. y. ~40 skirtingų miestų); su licencijuotu tiekėju šios ribos nėra.

## 6. Ko trūksta iki pardavimų (svarbumo tvarka)

1. **Licencijuotas duomenų tiekėjas.** Nemokami šaltiniai neleidžia komercinio naudojimo. Tai liečia ir
   atsarginį kelią ekrane (tiesioginiai šaltiniai), kurį parduodamoje versijoje reikės išjungti arba suderinti.
2. **Prisijungimas prie ekrano valdymo puslapio** (EN 18031).
3. **CE / RED sertifikavimas**, FCC JAV rinkai, WEEE registracija.
4. **ES Kibernetinio atsparumo aktas:** pranešimų apie pažeidžiamumus adresas ir tvarka (pranešimo pareiga
   galioja nuo 2026-09-11), nemokami saugumo atnaujinimai ne trumpiau kaip 5 metus (nuo 2027-12).
5. **Privatumo politika** (serveris mato IP ir apytikslę vietą) ir pardavimo sąlygos.
6. **Kainodara:** vienkartinis pardavimas, o duomenys ir serveris kainuoja kas mėnesį.

## 7. Idėjos funkcijoms (neįgyvendinta)

| Idėja | Kodėl verta |
|---|---|
| Išjungimo mygtukas / „ekranas išjungtas“ režimas puslapyje | To labiausiai prašo konkurento naudotojai |
| Oro uosto išvykimų / atvykimų lentelė | Naujas vaizdas be naujos įrangos |
| Dienos statistika (kiek lėktuvų, rečiausias tipas, toliausias maršrutas) | Priežastis grįžti į puslapį |
| Pranešimas, kai sekamas skrydis nusileidžia | Sekimo funkcijos užbaigimas |
| Kelių kalbų valdymo puslapis | Pardavimai ne angliškai kalbančiose šalyse |
| Prenumerata per serverį (aktyvavimo kodas) | Padengia mėnesines išlaidas |

## 8. Failai

| Kas | Kur |
|---|---|
| Programa ekranui | `Uploadinimas\bin\firmware.bin` |
| Serveris (kelti į hostingą) | `AeroBox_Server\` |
| Diegimo ir saugumo instrukcija | `AeroBox_Server\README.md` |
| Serverio testai | `AeroBox_Server\tests\test_server.py` |

Šaltiniai: ARRI SkyPanel ženklas (arri.com), AEROBOX ženklų įrašai (trademark.justia.com, markinton.com),
FlightWall (theflightwall.com), ES Kibernetinio atsparumo aktas (skadden.com, hoganlovells.com, ncsc.gov.ie).
