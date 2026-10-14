# Pukk

Firmware voor Pukk, een handheld quizapparaat, op de
**Waveshare ESP32-C6-Touch-AMOLED-2.16**.

Fase 0 is de hardware-basis:

- AXP2101 (PMU) initialiseren; accuspanning en laadstatus loggen (bij boot en
  elke 30 s).
- 2,16" AMOLED (480×480, CO5300 via QSPI) en touch werkend met LVGL 9, met
  FPS/CPU- en geheugenoverlay.
- Testscherm: één vraag, vier antwoordknoppen, timerbalk van 15 s, goed/fout.
- KEY-knop (IO10) start de vraag opnieuw.
- BLE (NimBLE): adverteert als `Pukk-XXXX` met een eigen GATT-service
  (CTRL/DATA/INFO). Nog geen packoverdracht.
- Na elke init-stap wordt de vrije heap gelogd.

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

## Knoppen

| Silkscreen | GPIO | Functie in fase 0 |
|---|---|---|
| IO10 | 10 | KEY: vraag opnieuw starten |
| BOOT | 9 | downloadmodus (bij aanzetten), verder alleen gelogd |
| PWR | PMU (gespiegeld op GPIO18) | aan/uit (lang drukken = uit), verder alleen gelogd |

## BLE

| | UUID | Eigenschappen |
|---|---|---|
| Service | `8e410001-0580-4871-83bd-51273ef66407` | |
| CTRL | `8e410002-0580-4871-83bd-51273ef66407` | write, notify (skelet: stuurt elke write terug) |
| DATA | `8e410003-0580-4871-83bd-51273ef66407` | write without response (skelet: telt bytes) |
| INFO | `8e410004-0580-4871-83bd-51273ef66407` | read: `{"fw":"0.1.0","flash_total":…,"flash_free":…}` |

Testen kan met nRF Connect (iOS/Android): scan naar `Pukk-XXXX`, verbinden,
INFO lezen, notificaties op CTRL aanzetten en iets naar CTRL schrijven.

## Projectstructuur

```
main/                 app_main: opstartvolgorde + heap-logging
components/board/     dunne hardwarelaag: I2C, PMU, scherm, touch, knoppen
                      (pinnen per bordvariant in board_config.h)
components/ui/        LVGL-port (taak + mutex) en schermen
components/ble/       NimBLE GATT-service
components/storage/   LittleFS-partitie "packs" op /packs
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
