# Pukk

Firmware voor Pukk, een handheld quizapparaat, op de
**Waveshare ESP32-C6-Touch-AMOLED-2.16**.

**Fase 0** is de hardware-basis:

- AXP2101 (PMU) initialiseren; accuspanning en laadstatus loggen (bij boot en
  elke 30 s).
- 2,16" AMOLED (480×480, CO5300 via QSPI) en touch werkend met LVGL 9, met
  FPS/CPU- en geheugenoverlay.
- BLE (NimBLE): adverteert als `Pukk-XXXX` met een eigen GATT-service.
- Na elke init-stap wordt de vrije heap gelogd.

**Fase 1** voegt quizpacks toe:

- Packs schrijf je als JSON; `tools/pukk.py` zet ze om naar het compacte
  `.pukk`-formaat en uploadt ze via Bluetooth.
- Het apparaat bewaart packs in de flash (`/packs`), toont ze in een lijst en
  speelt ze: vraag voor vraag met timer, 2–4 antwoorden, score aan het eind.
- KEY (IO10) gaat vanuit een spel terug naar de lijst.

Formaat en protocol: [docs/PACKS.md](docs/PACKS.md).

**Fase 2** geeft Pukk een huisstijl ("speels"):

- Font Fredoka met Latin-1 (é, ë, ü, …), zwart AMOLED-scherm, geel als
  merkkleur en vier antwoordkleuren met elk een vorm (driehoek, ruit, cirkel,
  vierkant).
- Opstartscherm met het woordmerk, statusbalk met Bluetooth en batterij,
  timer als ring, badges en een "pop" bij het antwoord, sterren bij de score,
  schermen die even inschuiven.
- De FPS/CPU- en geheugenoverlays staan uit; BOOT zet ze aan en uit.
- Een preview-tool die de echte schermcode op de Mac naar PNG's rendert.

**Fase 3** is de web-app ([app/](app/)): packs maken in de browser, online
bewaren op je eigen server (Docker) en via Bluetooth naar Pukks sturen.

**Fase 4** is de battle-modus: twee Pukks spelen hetzelfde pack tegen elkaar,
zonder telefoon. Tik op een pack › **Battle** op de ene Pukk en op
**Meedoen** op de andere. Heeft de tweede het pack niet, dan stuurt de eerste
het mee. Wie als eerste goed antwoordt, krijgt 500–1000 punten (sneller =
meer); voor de ander is de vraag dan voorbij. Regels en protocol:
[docs/BATTLE.md](docs/BATTLE.md).

Geluid (een eerder geplande fase 5) is geschrapt: Pukk blijft bewust stil.

Beslissingen en open vragen staan in [docs/NOTES.md](docs/NOTES.md).

## Documentatie

- Board: <https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16/ESP-IDF>
- Schema: [docs/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf](docs/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf)
- Waveshare-voorbeelden: <https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16>

## ESP-IDF 5.5.2 installeren

Gebruik precies **v5.5.2**: daarmee is het project gebouwd, en Waveshare
ontwikkelt de voorbeeldcode voor dit bord er ook op.

macOS (Homebrew):

```bash
brew install cmake ninja dfu-util ccache python3
```

Linux (Debian/Ubuntu):

```bash
sudo apt install git wget flex bison gperf python3 python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

ESP-IDF zelf (toolchain alleen voor de C6):

```bash
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.2
```

```bash
~/esp/esp-idf-v5.5.2/install.sh esp32c6
```

In elke nieuwe terminal de omgeving laden:

```bash
. ~/esp/esp-idf-v5.5.2/export.sh
```

Alleen bouwen kan ook zonder installatie, met Docker. Gebruik een eigen
builddir, zodat die niet botst met een native build (flashen gaat vanaf de Mac
zelf):

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.5.2 idf.py -B build-docker -D SDKCONFIG=build-docker/sdkconfig build
```

## Bouwen, flashen, monitoren

De target (`esp32c6`) staat al in `sdkconfig.defaults`; `idf.py set-target` is
niet nodig.

```bash
idf.py build
```

Het bord zit via USB-C aan de ingebouwde USB Serial/JTAG van de C6. Op macOS
heet de poort `/dev/cu.usbmodem…`:

```bash
ls /dev/cu.usbmodem*
```

```bash
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

Alleen flashen, zonder ESP-IDF (alleen esptool nodig: `brew install
esptool`): het script zoekt firmware in `build/` en `build-flash/*/`, toont
de versie, zoekt aangesloten Pukks (met hun Bluetooth-naam) en flasht er een
of allemaal. Settings en packs blijven staan.

```bash
bin/flash-pukk
```

Stoppen met monitoren: `Ctrl+]`. Een crashdump lezen na een reboot:

```bash
idf.py -p /dev/cu.usbmodem1101 coredump-info
```

De eerste build downloadt de componenten (LVGL, drivers, LittleFS) naar
`managed_components/`; de versies liggen vast in `dependencies.lock`.

**Let op:** `sdkconfig` wordt bij de eerste build gemaakt uit
`sdkconfig.defaults` en daarna niet meer overschreven. Na een wijziging in
`sdkconfig.defaults` eerst `sdkconfig` weggooien (wordt niet gecommit):

```bash
rm sdkconfig && idf.py build
```

## Downloadmodus

Normaal zet `idf.py flash` de chip zelf in downloadmodus. Lukt dat niet (bijv.
omdat de firmware crasht of de USB-poort niet verschijnt):

1. Houd **BOOT** ingedrukt.
2. Zet het bord opnieuw aan: USB-kabel eruit en er weer in. Met accu: houd
   **PWR** een paar seconden ingedrukt tot het bord uit is en druk dan kort op
   **PWR**.
3. Laat BOOT los en flash opnieuw.

Na het flashen in downloadmodus het bord nog een keer uit/aan zetten als de
firmware niet vanzelf start.

## Web-app

In de app maak en bewerk je packs, en stuur je ze naar een of meer Pukks. De
packs staan in een database op je server; iedereen met het wachtwoord ziet
dezelfde packs.

Bluetooth in de browser (Web Bluetooth) werkt in **Chrome en Edge** op een
computer of Android-telefoon, en alleen via **https** (of `localhost`). Safari
op iPhone/iPad en Firefox ondersteunen het niet.

### Op de Synology (Container Manager)

Bij elke push naar `main` bouwt GitHub Actions een image voor Intel en ARM:
`ghcr.io/tijmenbruggeman/pukk-app:latest`.

1. Container Manager › Project › Maken, en plak
   [app/compose.yaml](app/compose.yaml). Zet `PUKK_PASSWORD` op een eigen
   wachtwoord.
2. DSM › Configuratiescherm › Aanmeldingsportaal › Geavanceerd ›
   Reverse proxy: een https-regel naar `localhost:8080`, met je certificaat.
3. Bijwerken: in het project op "Image ophalen" en opnieuw starten (of
   `docker compose pull && docker compose up -d`).

De data (SQLite-database en het sleutelbestand voor sessies) staat in het
volume `pukk-data`. Wachtwoord wijzigen logt iedereen uit.

### Lokaal draaien

Node 24 is nodig.

```bash
cd app && npm install
```

Ontwikkelen (Vite met hot reload op http://localhost:5173, API op poort 8787,
wachtwoord `dev`):

```bash
npm run dev:server
```

```bash
npm run dev
```

De productieversie lokaal (http://localhost:8080):

```bash
npm run build && PUKK_PASSWORD=test npm start
```

Tests en typecheck:

```bash
npm test && npm run typecheck
```

De pack-tests vergelijken de bytes van de app met `tools/pukk.py`, dus
`python3` moet beschikbaar zijn.

## Packs maken en uploaden (zonder app)

Een pack is een JSON-bestand, zie [packs/voorbeeld.json](packs/voorbeeld.json)
en [docs/PACKS.md](docs/PACKS.md). Tekst mag Latin-1 bevatten (é, ë, ü, ç,
€, …); andere letters met een accent zet de tool om naar de basisletter.

Twee optionele instellingen bepalen hoe een spel loopt:
`"questions_per_game": 20` kiest per spel willekeurig 20 vragen uit het pack
(weglaten = alle), en `"order": "random"` stelt ze door elkaar in plaats van op
volgorde. In een battle krijgen beide spelers dezelfde vragen. In de web-app
staan ze onder de titel.

Eenmalig de Python-omgeving voor de tool (bleak voor Bluetooth):

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
```

Uploaden. Zonder `--device` neemt de tool het eerste Pukk-apparaat dat hij
vindt; de naam staat bovenaan de packlijst op het scherm:

```bash
.venv/bin/python tools/pukk.py upload packs/voorbeeld.json --device Pukk-EA66
```

Overige commando's: `scan`, `list`, `info`, `delete <naam>`, `build` (JSON →
`.pukk`) en `inspect` (`.pukk` controleren).

**macOS:** draai de BLE-commando's in **Terminal** of iTerm2. macOS koppelt
Bluetooth-toegang aan de terminal-app; Warp en het terminalpaneel van de
Claude-app declareren geen Bluetooth-gebruik, en dan wordt Python direct
gestopt (crash met `Namespace TCC`).

Tests zonder hardware (packformaat, vraagselectie, battle-regels; met
AddressSanitizer):

```bash
tools/test_host.sh
```

## Knoppen

Van links naar rechts met het scherm rechtop: BOOT, PWR, IO10.

| Silkscreen | GPIO | Functie |
|---|---|---|
| IO10 | 10 | rechts, KEY: vanuit een spel terug naar de packlijst; in een battle stoppen (voor allebei) |
| BOOT | 9 | links: **1,5 s vasthouden = uitzetten**; kort = FPS/geheugen-overlays aan/uit; bij aanzetten: downloadmodus |
| PWR | PMU (gespiegeld op GPIO18) | midden: kort = aanzetten; enkele seconden vasthouden = hard uit (door de PMU zelf) |

## BLE

| | UUID | Eigenschappen |
|---|---|---|
| Service | `8e410001-0580-4871-83bd-51273ef66407` | |
| CTRL | `8e410002-0580-4871-83bd-51273ef66407` | write, notify: verzoeken en antwoorden |
| DATA | `8e410003-0580-4871-83bd-51273ef66407` | write without response: uploaddata |
| INFO | `8e410004-0580-4871-83bd-51273ef66407` | read: `{"fw":"0.4.0","flash_total":…,"flash_free":…}` |
| PACKS | `8e410005-0580-4871-83bd-51273ef66407` | read: JSON-lijst van de packs |
| BATTLE | `8e410006-0580-4871-83bd-51273ef66407` | write, notify: berichten tussen twee Pukks in een battle |

Het protocol staat in [docs/PACKS.md](docs/PACKS.md), de battle in
[docs/BATTLE.md](docs/BATTLE.md). INFO en PACKS kun je ook met nRF Connect
(iOS/Android) lezen. Een Pukk kan twee verbindingen tegelijk hebben: een app
of tool, en een andere Pukk.

## UI bekijken zonder bord

De preview compileert LVGL en `components/ui` voor de Mac, speelt alle
schermen af met nep-packs en gesimuleerde tijd, en schrijft een PNG per
scherm naar `build-preview/`. Nodig: een C-compiler, ImageMagick (`brew
install imagemagick`) en `managed_components/` (na één `idf.py build`).

```bash
make -C tools/preview -j8 run
```

Fonts en vormen staan gegenereerd in `components/ui/assets/`. Opnieuw maken
(bijv. voor een extra lettergrootte; nodig: node en python3):

```bash
tools/assets/gen_fonts.sh && python3 tools/assets/gen_shapes.py
```

## Projectstructuur

```
main/                 app_main: opstartvolgorde + heap-logging
components/board/     dunne hardwarelaag: I2C, PMU, scherm, touch, knoppen
                      (pinnen per bordvariant in board_config.h)
components/ui/        LVGL-port (taak + mutex), huisstijl (ui_theme) en schermen
components/ui/assets/ gegenereerde fonts en vormen (tools/assets/)
components/ble/       NimBLE GATT-service, uploadprotocol en central-rol (battle)
components/battle/    battle-modus: spelregels, berichten en de engine-taak
components/storage/   LittleFS-partitie "packs" op /packs
components/packs/     .pukk-formaat (reader) en packopslag
app/                  web-app: editor, server (Hono + SQLite), Web Bluetooth
app/compose.yaml      deployment voor Docker Compose / Synology
bin/flash-pukk        firmware flashen, met keuze uit gevonden Pukks
tools/pukk.py         packs bouwen en via BLE beheren
tools/preview/        UI renderen op de Mac (PNG per scherm)
tools/assets/         fonts en vormen opnieuw genereren
packs/                voorbeeldpacks (JSON)
partitions.csv        2× OTA 3 MB, coredump, packs 9,8 MB
sdkconfig.defaults    alle afwijkingen van de IDF-defaults
```

## Geheugenkeuzes

De ESP32-C6 heeft 512 KB SRAM en **geen PSRAM**. Alles (heap, stacks, BLE,
LVGL, code die in IRAM moet) deelt die 512 KB.

- **Geen framebuffer.** Een volledig frame is 480×480×2 = 450 KB; dat past
  niet. Het scherm heeft eigen GRAM, dus LVGL rendert in stroken
  (render mode PARTIAL) en stuurt alleen gewijzigde gebieden.
- **Twee draw buffers van 480×48 px** (1/10 scherm, 2 × 45 KB), in intern
  RAM en DMA-capable. Terwijl de ene buffer via QSPI-DMA naar het scherm gaat,
  rendert LVGL in de andere. LVGL wacht op de DMA met een semafoor, zodat de
  CPU intussen vrij is voor BLE.
- **LVGL-heap 64 KB** (vaste array) voor objecten en stijlen. De overlay
  linksonder toont het gebruik; later bijstellen.
- **NimBLE** in plaats van Bluedroid: veel kleiner. Alleen peripheral, één
  verbinding, geen pairing.
- LVGL-functies staan **niet** in IRAM (dat kost op de C6 heap-RAM).
- De boot-log toont na elke stap de vrije interne en DMA-capable heap en het
  verschil met de vorige stap (`heap after …`). Het verschil tussen die twee is
  15 KB LP-RAM die wel intern maar niet DMA-capable is. `idf.py size` toont het
  statische deel (`.bss`, `.data`, IRAM).
- Gemeten op board 1: LVGL kost ~165 KB, BLE ~70 KB, en na het opstarten is er
  ~200 KB heap vrij. Details in [docs/NOTES.md](docs/NOTES.md).
