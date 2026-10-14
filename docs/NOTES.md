# Pukk – notities

Beslissingen, afwijkingen en open vragen uit fase 0. Bijwerken zodra iets op
de hardware geverifieerd is.

## Bronnen

- Waveshare-repo `waveshareteam/ESP32-C6-Touch-AMOLED-2.16` (commit `294543798f`,
  juli 2026), map `02_Example/ESP-IDF-v5.5.3/`: `01_AXP2101_Test`,
  `09_LVGL_V9_Test`. Ter controle ook de Arduino-voorbeelden en de XiaoZhi-port
  (`02_Example/XiaoZhi-v2.2.5/main/boards/waveshare/esp32-c6-touch-amoled-2.16`).
- Schema: `docs/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf` (pagina 1 = schema,
  pagina 2–3 = componentplaatsing).
- Waveshare-docs: <https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16/ESP-IDF>

## Pinnen

| Functie | GPIO | Bron | Schema |
|---|---|---|---|
| I2C SCL / SDA | 7 / 8 | alle demo's | ✔ ESP32_SCL = MTDO = GPIO7, ESP32_SDA = GPIO8, pull-ups 2,2k |
| AXP2101 | I2C 0x34 | demo's | ✔ |
| LCD QSPI CLK, D0–D3 | 0, 1, 2, 3, 4 | demo's | ✔ connector J4 |
| LCD CS | 15 | Arduino, XiaoZhi, defaults in `display_bsp.h` | ✔ op J4 (pin 4); welk paneelsignaal is niet te zien |
| LCD reset | – (via AXP2101 ALDO3) | demo's | ✔ LCD_RESET → R16 10k → ALDO3 |
| LCD TE | – | – | alleen testpunt TP20 |
| Touch INT / RST | 5 / 11 | Arduino, XiaoZhi, defaults in `display_bsp.h` | ✔ op J4 (pin 17/18) |
| Knop BOOT | 9 | – | ✔ Key1, 10k pull-up |
| Knop IO10 ("KEY") | 10 | – | ✔ Key3, 10k pull-up, actief laag |
| Knop PWR | AXP2101 PWRON, gespiegeld op GPIO18 | – | ✔ via T1 (BSS138), **geïnverteerd**: GPIO18 hoog = ingedrukt |
| SD-kaart (later) | CLK 0, MOSI 1, MISO 2, CS 6 | demo's | ✔ deelt de SPI-bus met het scherm |
| QMI8658 INT1/INT2 (later) | 16 / 17 | – | ✔ |
| I2S audio (later) | MCLK 19, SCLK 20, DSIN 21, LRCK 22, DOUT 23 | demo's | ✔ |

### Tegenstrijdigheden gevonden

1. **LCD CS en touch-INT.** In `09_LVGL_V9_Test/main/user_config.h` staat
   `CS = GPIO5`, `TOUCH_INT = GPIO15`. Die defines worden voor het scherm niet
   gebruikt: `DisplayPort` wordt aangemaakt met zijn defaults `cs = 15`,
   `tp_int = 5`. De Arduino-demo en de XiaoZhi-port gebruiken ook 15/5. We
   gebruiken **CS = 15, INT = 5**. `user_config.h` lijkt gekopieerd van het
   2.06"-bord (dat heeft CS = 5).
2. **GPIO10.** De GPIO-tabel rechtsboven in het schema noemt GPIO10
   "LCD_RESET", maar in het schema zelf gaat LCD_RESET naar ALDO3 en zit
   GPIO10 op Key3 (silkscreen "IO10"). De code van Waveshare klopt met het
   schema, niet met de tabel.
3. **ALDO-spanningen.** De railtabel in het schema zegt ALDO3 = 3,0 V en
   ALDO4 = 1,8 V; de demo's zetten ALDO1–4 alle op 3,3 V. ALDO4 is niet
   aangesloten en ALDO3 is alleen de pull-up van LCD_RESET, dus we volgen de
   demo's.
4. **Init-sequentie.** De XiaoZhi-port gebruikt een andere sequentie
   (kolommen 0x16–0x1AF, MADCTL 0xA0) die bij een ander paneel lijkt te horen.
   We gebruiken die van `09_LVGL_V9_Test` (480×480, MADCTL 0x30).
5. **ESP-IDF-versie.** De Waveshare-docs zeggen v5.5.2; de map in de repo
   heet `ESP-IDF-v5.5.3`, de `sdkconfig.defaults` daarin zeggen 5.5.1/5.5.2.
   We gebruiken v5.5.2.
6. **Touch-chip.** Jij noemt een CST9220; Waveshare gebruikt de driver
   `waveshare/esp_lcd_touch_cst9217` (zelfde protocol). De driver logt bij
   boot het chiptype ("Chip Type: 0x...."). Even nakijken.
7. **"KEY"-knop.** Op de silkscreen heten de knoppen BOOT, PWR en IO10. We
   nemen aan dat KEY = IO10 (GPIO10).

## Beslissingen

### PMU (AXP2101)

Eigen minimale driver (`components/board/pmu_axp2101.c`) in plaats van
XPowersLib mee te kopiëren (C++, heel groot). Registers en coderingen komen
uit XPowersLib, de waarden uit `Custom_PmicRegisterInit()`:

- VBUS-ingangsstroom 2000 mA, DCDC1 en ALDO1–4 op 3,3 V.
- Laden: precharge 50 mA, constante stroom 500 mA, eindstroom 50 mA.
- TS-pinmeting uit, zoals XPowersLib `begin()` in de demo's doet. Let op: op
  het bord zit wel een 10k-NTC (RP2) op de TS-pin (zie open vraag 1).

Afwijkingen:

- Precharge-veld als 4 bits geschreven (XPowersLib maskeert 2 bits, maar de
  enum loopt tot 200 mA = 8). Resultaat is in beide gevallen 50 mA, tenzij de
  bovenste bits al gezet waren.
- Extra aangezet voor de logging: accudetectie, ADC-kanalen VBAT/VBUS/VSYS en
  de fuel gauge (percentage).
- ALDO1/ALDO2 (audio) laten we zoals ze na power-on staan; de log toont het
  aan/uit-register.

### Scherm

- `espressif/esp_lcd_sh8601` 1.0.0 met de CO5300-init-sequentie van Waveshare,
  QSPI 40 MHz. Er bestaat inmiddels ook `espressif/esp_lcd_co5300`; niet
  gebruikt omdat de Waveshare-combinatie bewezen is op dit bord.
- Reset via ALDO3 uit/aan (100 ms), zoals de demo.
- De CO5300 wil gebieden met even start en even breedte/hoogte: rounder in de
  LVGL-port. LVGL rondt daarmee ook het aantal regels per flush af.
- RGB565 moet big-endian over de bus: bytes worden in `flush_cb` omgedraaid
  (`lv_draw_sw_rgb565_swap`). De LVGL 8-optie `LV_COLOR_16_SWAP` uit de
  Waveshare-config bestaat niet meer in LVGL 9.
- Touch: `swap_xy = 1`, `mirror_y = 1` zoals de demo. `x_max/y_max = 479`
  (XiaoZhi) in plaats van 480 (09-demo), anders loopt de gespiegelde as tot 480.

### LVGL

- **LVGL 9.5.0**, vastgezet met `~9.5.0`. 9.6.0 heeft Kconfig-afhankelijke
  dependencies waardoor component manager 2.4.2 een kapot
  `dependencies.lock` schrijft (alle componenten als `type: local`). 9.5 is ook
  de versie die Waveshare gebruikt.
- Eigen port (`components/ui/lvgl_port.c`), geen `esp_lvgl_port` of
  `esp_lvgl_adapter`: eigen FreeRTOS-taak (prio 4, 8 KB stack) en een
  recursieve mutex (`ui_lock`/`ui_unlock`) rond alle `lv_*`-calls.
  `LV_USE_OS = NONE`.
- Twee draw buffers van 480×48 px (2 × 46 080 B), `MALLOC_CAP_INTERNAL |
  MALLOC_CAP_DMA`, render mode PARTIAL. Terwijl buffer A via DMA naar het
  scherm gaat, rendert LVGL in buffer B.
- Wachten op een DMA-transfer gebeurt met `lv_display_set_flush_wait_cb` en een
  semafoor vanuit de transfer-done-interrupt: de CPU is dan vrij voor andere
  taken (zonder die callback zit LVGL in een busy-loop).
- Performance monitor: de CPU-waarde is **de hele chip** (FreeRTOS idle-tijd,
  `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`), niet alleen de LVGL-taak. Dat
  gaat via `LV_SYSMON_GET_IDLE=ui_cpu_idle_percent` (zie
  `components/ui/CMakeLists.txt`).
- Ook de memory monitor staat aan (linksonder): gebruik van de 64 KB LVGL-heap.
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` (stond aan bij Waveshare) staat uit:
  IRAM komt op de C6 uit hetzelfde 512 KB SRAM als de heap.
- De Montserrat-fonts bevatten alleen ASCII; geen ë/é in schermteksten tot er
  een eigen font is.

### BLE

- NimBLE, alleen peripheral-rol, max. 1 verbinding, geen Security Manager
  (geen pairing/bonding), ongebruikte standaardservices uit.
- Naam `Pukk-XXXX` = laatste 2 bytes van het BLE-adres zoals het
  geadverteerd wordt. De 128-bit service-UUID staat in het advertentiepakket,
  de naam in de scan response (samen passen ze niet in 31 bytes).
- UUID's: service `8e410001-0580-4871-83bd-51273ef66407`, CTRL `…0002…`,
  DATA `…0003…`, INFO `…0004…` (alleen het 3e/4e byte verschilt).
- CTRL (write + notify): skelet stuurt elke write terug als notificatie.
  DATA (write without response): alleen tellen, totaal in de log bij
  disconnect. INFO (read): `{"fw":"0.1.0","flash_total":N,"flash_free":N}` in
  bytes, gemeten op de LittleFS-partitie `packs`.
- Voorkeurs-MTU 247 (past met Data Length Extension in één link-layer-pakket).
- Advertising-interval 100–150 ms.

### Opslag en partities

- 16 MB flash: nvs 24 KB, otadata, phy_init, **2 × 3 MB OTA-slots**,
  coredump 64 KB, **packs 9,8 MB** LittleFS (`joltwallet/littlefs`), gemount
  op `/packs` en bij de eerste boot geformatteerd.
- OTA-slots nu al, omdat de partitietabel later wijzigen de packs wist.
  `idf.py flash` raakt de packs-partitie niet aan.
- Extra component `components/storage/` (stond niet in de oorspronkelijke
  structuur) voor het mounten en voor "vrije flash" in INFO.

### Overig

- Console via de ingebouwde USB Serial/JTAG (de USB-C-poort).
- Coredumps gaan naar flash: `idf.py coredump-info` na een crash.
- `MINIMAL_BUILD`: alleen componenten die `main` nodig heeft worden gebouwd
  (daarom staat `espcoredump` expliciet in `main/CMakeLists.txt`).
- PMU en scherm zijn fataal bij een fout; touch, knoppen, opslag en BLE loggen
  een fout en gaan door, zodat één haperend onderdeel geen rebootlus geeft.
- Main-taak 6 KB stack (default 3,5 KB): `app_main` bouwt het eerste scherm,
  formatteert LittleFS bij de eerste boot en start NimBLE. De statusregel
  (elke 30 s) logt hoeveel stack van main en de LVGL-taak nooit gebruikt is;
  daarmee kunnen beide later omlaag.
- LVGL-voorbeelden en -demo's worden niet meegebouwd.

## Geheugen: statisch (uit `idf.py size`)

Stand na fase 0 (v0.1.0):

| | Bytes |
|---|---|
| DIRAM totaal beschikbaar | 452 112 |
| `.text` in IRAM (o.a. BLE-controller, ISR's) | 91 376 |
| `.bss` (waarvan 64 KB LVGL-heap) | 74 408 |
| `.data` | 10 108 |
| **Over voor de heap bij boot** | **276 220** |
| App-image in flash | 1,16 MB (OTA-slot 3 MB) |

Daarvan gaan bij het opstarten o.a. af: 2 × 46 080 B draw buffers, de
LVGL-taak (8 KB), NimBLE-controller en -host, en de main-taak.

## Open vragen

1. **Accu.** Welke accu (capaciteit) zit erop? 500 mA laadstroom is alleen
   veilig vanaf ~500 mAh. De TS-pin heeft een 10k-NTC op het bord (RP2), maar
   de meting staat uit zoals in de demo's; daarmee is er waarschijnlijk geen
   temperatuurbewaking tijdens laden. Uitzoeken of we die aan willen zetten
   (registers 0x30 bit 1 en 0x50).
2. **Oriëntatie scherm/touch.** Op hardware controleren of tikken op elke
   knop de juiste knop raakt; anders `BOARD_TOUCH_*` in `board_config.h`
   aanpassen.
3. **Rommel bij opstarten?** De init-sequentie zet het scherm aan (0x29) vóór
   het eerste LVGL-frame. Als er kort ruis zichtbaar is: helderheid 0 tot het
   eerste frame.
4. **BLE-beveiliging.** Moet packoverdracht later met pairing/bonding?
5. **Firmware-update via BLE.** Willen we OTA? De partitietabel houdt er
   rekening mee.
6. **Packs op SD-kaart?** De TF-slot deelt de SPI-bus met het scherm; kan,
   maar SD-verkeer en schermupdates moeten dan om de bus delen.
7. **LVGL-heap.** 64 KB is een gok; bijstellen met de memory monitor.
8. **S3-variant.** Welk bord precies? Met PSRAM gelden andere geheugenkeuzes.
9. **Diakrieten.** Nederlandse vragen hebben é/ë nodig: eigen font met
   Latin-1 genereren (lv_font_conv).

## Geheugen bij boot

Invullen na de eerste boot op hardware (regels `heap after ...` in de log):

| Na stap | Intern vrij | Δ |
|---|---|---|
| boot | | |
| nvs | | |
| pmu | | |
| display | | |
| touch | | |
| lvgl | | |
| ui | | |
| buttons | | |
| storage | | |
| ble | | |
