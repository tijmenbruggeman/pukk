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
6. **Touch-chip.** Waveshare gebruikt de driver
   `waveshare/esp_lcd_touch_cst9217`; het bord heeft een CST9220. Geverifieerd:
   de driver werkt en logt `Chip Type: 0x9220`, resolutie 480×480.
7. **"KEY"-knop.** Op de silkscreen heten de knoppen BOOT, PWR en IO10.
   KEY = IO10 (GPIO10); geverifieerd op board 1.

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
- ALDO1/ALDO2 (audio) laten we zoals ze na power-on staan. Gemeten: na
  power-on staan DCDC1–4 aan (`0x0f`) en **alle** LDO's (`0xff`, ook BLDO,
  CPUSLDO, DLDO1). Later voor stroombesparing uitzetten wat niet gebruikt
  wordt.

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
  `esp_lvgl_adapter`: eigen FreeRTOS-taak (prio 4, 10 KB stack sinds fase 2) en een
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
- Ook de memory monitor (linksonder): gebruik van de 64 KB LVGL-heap. Sinds
  fase 2 staan beide overlays standaard uit; BOOT zet ze aan.
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` (stond aan bij Waveshare) staat uit:
  IRAM komt op de C6 uit hetzelfde 512 KB SRAM als de heap.
- Fonts: zie fase 2 (Fredoka met Latin-1).

### BLE

- NimBLE, peripheral-rol, geen Security Manager (geen pairing/bonding),
  ongebruikte standaardservices uit. Sinds fase 4 ook central en observer,
  met max. 2 verbindingen (zie Fase 4).
- Naam `Pukk-XXXX` = laatste 2 bytes van het BLE-adres zoals het
  geadverteerd wordt. De 128-bit service-UUID staat in het advertentiepakket,
  de naam in de scan response (samen passen ze niet in 31 bytes).
- UUID's: service `8e410001-0580-4871-83bd-51273ef66407`, CTRL `…0002…`,
  DATA `…0003…`, INFO `…0004…` (alleen het 3e/4e byte verschilt).
- Characteristics en protocol: zie [PACKS.md](PACKS.md) en hieronder bij
  fase 1. INFO (read): `{"fw":"0.1.0","flash_total":N,"flash_free":N}` in
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
- **Uitzetten**: de knoppen zitten (scherm rechtop) van links naar rechts als
  BOOT, PWR, IO10. BOOT 1,5 s vasthouden zet bit 0 van AXP2101-register 0x10
  ("soft power off"): alle rails uit, ook die van de C6. Ook met USB eraan
  blijft het bord uit; PWR zet het weer aan. Bij elke boot logt de PMU
  waardoor hij aanging en waardoor hij de vorige keer uitging (registers
  0x20/0x21), bijv. `switched on by: PWR key; last off by: software`.

## Fase 1: packs

### Formaat

- JSON als bronformaat (makkelijk te schrijven), op het apparaat een eigen
  binair formaat `.pukk` (zie [PACKS.md](PACKS.md)). JSON op het apparaat
  parsen kost te veel RAM: een pack van 1000 vragen zou als cJSON-boom al snel
  100+ KB worden. Het binaire formaat leest één vraag tegelijk direct uit
  flash (vaste vragentabel + stringtabel).
- Vragen max. 120 bytes en antwoorden max. 32 bytes: meer past niet met font 24
  in het vraagvlak en de knoppen.
- 2 tot 4 antwoorden; tijd per vraag 5–60 s (standaard 15).
- De reader (`components/packs/pack_format.c`) vertrouwt geen enkele offset uit
  het bestand en heeft geen ESP-IDF-afhankelijkheden. De host-test
  (`tools/test_host.sh`) draait hem met ASan/UBSan tegen elk bestand met één
  beschadigde byte, alle afgekapte versies en 20 000 willekeurig beschadigde
  packs met een kloppende CRC.

### Upload via BLE

- Eén verzoek tegelijk op CTRL, data op DATA met een offset in elk pakket.
  Herhaalde data wordt genegeerd, een gat breekt de upload af. CRC-32 over het
  hele bestand in BEGIN; het apparaat controleert bij END grootte, CRC en het
  volledige packformaat.
- De upload schrijft naar `/packs/.upload` en hernoemt pas na controle. Een
  afgebroken upload (ook door een reset) laat het oude pack staan.
- De packlijst is een leesbare characteristic (PACKS, JSON) in plaats van een
  reeks notificaties: een lange read regelt NimBLE zelf, terwijl tientallen
  notificaties achter elkaar de buffers van de stack kunnen laten overlopen.
- Geen pairing: iedereen in de buurt kan uploaden. Zie open vragen.
- Uploaden gebeurt in de NimBLE-hosttaak (schrijven + controleren op flash);
  daarom is die stack 6 KB. Gemeten: ~2,4 KB gebruikt.
- Writes zonder response kunnen verloren gaan: bleak wacht op macOS niet op
  CoreBluetooth's `canSendWriteWithoutResponse`, en CoreBluetooth gooit
  writes weg als zijn buffer vol is. Eerste test met 71 KB: de staart ging
  verloren. Daarom een venster van 8 KiB, `RESEND` bij een gat, hervatten na
  2 s stilte, en END pas na `PROGRESS(size)`. De tool pollt daarnaast zelf
  `canSendWriteWithoutResponse` (via een interne bleak-variabele, met fallback).
- Na het verbinden vraagt het apparaat om Data Length Extension (251), 2M PHY
  en een interval van 15–30 ms. macOS gaf: DLE 251, 2M PHY, interval 30 ms.

### UI

- Home = packlijst met de BLE-naam bovenaan. Spel: vraag voor vraag, na een
  antwoord 2 s feedback, dan de volgende; score aan het eind. KEY gaat van
  spel naar lijst.
- Vragen worden één voor één uit flash gelezen (~9 ms per vraag); een pack dat
  tijdens het spelen wordt vervangen, wordt netjes afgesloten in plaats van te
  crashen.
- Een pack starten leest alleen de header. De volledige controle (CRC en elke
  vraag) gebeurt bij de upload; elke vraag wordt bij het lezen opnieuw
  gecontroleerd.

### Packs lezen: geen stdio

- Packs worden met `open`/`lseek`/`read` gelezen, met een eigen cache van twee
  vensters van 512 bytes. Met `fopen`/`fread` kostte newlib een VFS-aanroep
  per 128 bytes (standaardbuffer) of zelfs per byte (`_IONBF`). Gemeten op de
  rekentest (71 KB, 1000 vragen): controleren duurde 15,2 s met `_IONBF`,
  ~4 s met de standaardbuffer en 0,96 s met `read()`. De flash zelf leest
  64 KB in 10 ms; LittleFS besteedde maar 16 ms aan flash-reads.
- 15 s rekenen in de BLE-hosttaak liet de task-watchdog afgaan (IDLE kwam niet
  meer aan de beurt). Bij 1 s speelt dat niet meer.
- Van scherm wisselen gebeurt via `lv_async_call`, niet in de klik-callback
  zelf: die verwijdert anders de knop waarvan het event nog loopt.
- Het upload-venster staat op LVGL's top layer en blijft dus staan als het
  scherm eronder wisselt.

### Tool

- `tools/pukk.py`: `build`/`inspect` zonder extra packages; `scan`, `info`,
  `list`, `upload` en `delete` met bleak.
- macOS: alleen vanuit Terminal of iTerm2. Warp en de shell van de Claude-app
  hebben geen `NSBluetoothAlwaysUsageDescription`; macOS stopt Python dan met
  een TCC-crash.

### Gemeten (board 2, `Pukk-AF8E`)

- Voorbeeldpack (667 bytes): upload incl. controle ~0,1 s na BEGIN; verbinding
  op MTU 247. Gespeeld: 7 van 8 goed.
- Rekentest (1000 vragen, 71 KB, `build-flash/rekentest.json`, gegenereerd):
  datadeel ~16 s (~4,5 KB/s), geen verloren pakketten. Het apparaat schrijft
  71 KB in 0,4 s, dus de rem zit aan de kant van de Mac (bleak/CoreBluetooth,
  ~0,5 pakket per verbindingsinterval van 30 ms). Goed genoeg voor nu;
  opnieuw bekijken met de app.
- Heap na boot 197 KB vrij, laagste punt tijdens upload/spel 187 KB.
- Stack nooit gebruikt: LVGL 3 848 van 8 192 B, BLE-host 3 752 van 6 144 B,
  main 4 680 van 6 144 B.

## Fase 2: huisstijl

Gekozen uit drie richtingen (speels, strak, quizshow): **speels**.

### Stijl

- Font **Fredoka** (SIL OFL, `components/ui/assets/Fredoka-OFL.txt`), Medium
  voor tekst en SemiBold voor koppen: 20, 24, 28, 32 en 48 px plus een
  woordmerk-font van 120 px met alleen "pukk". Latin-1 plus ‘ ’ “ ” – — • … €.
  20 en 32 px bevatten ook de LVGL-symbolen die we gebruiken (batterij,
  Bluetooth, bliksem, vinkje, kruis, pijl, ster).
- Gegenereerd met `lv_font_conv` 1.5.3 uit google/fonts commit `a60a77e`
  (variabel font, met fontTools vastgezet op gewicht 500/600), 4 bpp,
  ongecomprimeerd zoals de ingebouwde LVGL-fonts. Script:
  `tools/assets/gen_fonts.sh`.
- Palet: zwart (AMOLED: pixels uit), geel `#FFD23F` als merkkleur, vlakken
  `#1C1C24`/`#2A2A35`. Antwoorden hebben vaste kleur + vorm, zodat ze ook
  zonder kleurzien te onderscheiden zijn: rood/driehoek, blauw/ruit,
  oranje/cirkel, groen/vierkant. Tekst op oranje en groen is donker.
- Vormen zijn A8-plaatjes (alleen alfa) die LVGL in elke kleur tekent:
  `tools/assets/gen_shapes.py`.
- Alles in `components/ui/ui_theme.[ch]`: kleuren, fonts, gedeelde styles.

### Animaties zonder extra geheugen

- Schermen schuiven 24 px omhoog (220 ms), knoppen tekenen 4 px kleiner
  zolang je drukt, het goede antwoord en de score-sterren "poppen".
- Alleen verplaatsen (`translate`) en tekengrootte (`transform_width/height`)
  en `text_opa` op losse labels. Transparantie op een hele container zou
  LVGL in een tussenlaag laten tekenen, uit de 64 KB LVGL-heap.

### Statusbalk, opstartscherm, debug

- Batterij uit de AXP2101 (fuel gauge), elke 30 s bijgewerkt; rood onder 15%
  zonder lader. Bluetooth-icoon blauw zolang er een verbinding is.
- Opstartscherm zodra LVGL draait, minstens 1,4 s, tot opslag, knoppen en BLE
  klaar zijn.
- FPS/CPU- en geheugenoverlays standaard verborgen; BOOT wisselt.

### Preview-tool

`tools/preview/` compileert LVGL 9.5 en `components/ui` voor de host met
nep-packs, een nep-touchscreen (tikken op de tekst van een knop) en
gesimuleerde tijd. Elk scherm wordt een PNG. Een volledige run duurt ~15 s.

### Gemeten (board 2)

- App 1,28 MB (+110 KB: Fredoka erin, Montserrat 24/40 eruit).
- Heap na opstarten ongewijzigd: 197 KB vrij.
- LVGL-taakstack: ~5,6 KB gebruikt na een spel (fase 1: ~4,3 KB). Stack van
  8 naar 10 KB voor marge.

### Flashen zonder NVS te wissen

Een met `esptool merge_bin` samengevoegde image vult gaten op met `0xFF` en
wist daarmee de NVS (o.a. RF-kalibratie, straks instellingen). Flash daarom
de losse bestanden met `flash_args`, zoals `idf.py flash` doet.

## Fase 3: web-app

### Keuzes

- **Web-app** met Web Bluetooth: geen installatie, werkt in Chrome/Edge op
  computer en Android. Niet op iPhone (Safari heeft geen Web Bluetooth);
  daarvoor zou later een native app nodig zijn.
- **Online opslag, zelf gehost**: één container (Node 24) met de gebouwde
  frontend en een API, SQLite (`node:sqlite`, ingebouwd in Node 24) in een
  volume. Draait via Docker Compose op de Synology achter de bestaande
  reverse proxy (https, nodig voor Web Bluetooth).
- **Eén wachtwoord** uit `PUKK_PASSWORD`. Sessie = ondertekende cookie
  (`HttpOnly`, `Secure`, `SameSite=Strict`, 30 dagen) met de wachtwoordhash in
  de handtekening, zodat een nieuw wachtwoord alle sessies ongeldig maakt.
  Maximaal 10 foute pogingen per minuut (voor iedereen samen).
- **Image via GitHub Actions** naar `ghcr.io/tijmenbruggeman/pukk-app`
  (amd64 + arm64). De bouwstap draait op het platform van de builder; de
  runtime-stap kopieert alleen bestanden, dus geen emulatie nodig.
- Stack: React 19 + Vite 8 (frontend), Hono 4 (server), TypeScript 7,
  Vitest 5. Versies vastgezet op releases van minstens twee weken oud.

### Gedeelde packcode

`app/src/shared/pack.ts` doet in TypeScript wat `tools/pukk.py` doet:
dezelfde regels (limieten, tekenset, vervangingen) en dezelfde encoder. Een
test bouwt het voorbeeldpack en een pack van 1000 vragen met beide en eist
identieke bytes. De browser gebruikt de code voor validatie per veld en om
het `.pukk`-bestand te maken; de server alleen voor de "geldig"-vlag.

### Concepten

Opslaan kan altijd, ook als een pack nog niet af is; de server controleert
alleen de vorm (en maxima) en markeert ongeldige packs als concept. Pushen
naar een Pukk kan pas als er geen fouten meer zijn.

Elk pack heeft een unieke **naam op de Pukk** (bestandsnaam,
`a-z0-9_-`), afgeleid van de titel bij het aanmaken (`-2`, `-3` bij
dubbelingen) en daarna vast, zodat een titelwijziging het pack op de Pukk
vervangt in plaats van een tweede aan te maken.

### Bluetooth in de browser

- Het protocol (`app/src/client/ble/protocol.ts`) is dezelfde
  vensterlogica als `tools/pukk.py` en los van Web Bluetooth getest tegen
  een gesimuleerde Pukk (`sim.ts`, dezelfde regels als `protocol.c`) die
  writes kwijtraakt, data beschadigt of stil valt.
- Web Bluetooth staat per apparaat maar één GATT-operatie tegelijk toe; de
  adapter zet ze in een rij.
- Meerdere Pukks tegelijk verbonden (al nodig voor de battle-modus).
- De browser laat je een Pukk kiezen in zijn eigen venster; je hoeft dat
  per sessie één keer te doen.

## Fase 4: battle

Spelregels en protocol: [BATTLE.md](BATTLE.md).

### Keuzes

- **Pukk tegen Pukk**, geen telefoon: één Pukk is host (kiest het pack,
  beslist elke vraag), de ander gast. De host is BLE-central; de gast blijft
  peripheral, zodat een app of tool tegelijk met de gast verbonden kan zijn.
  Daarvoor staat NimBLE nu op 2 verbindingen met central- en observer-rol.
- **Vinden zonder kiezen**: de gast zet een "meedoen"-vlag in zijn
  advertentie (manufacturer data `FF FF 'P' 'K' 01`, past naast de
  128-bit service-UUID); de host verbindt met de eerste die hij ziet. Met
  twee Pukks in de kamer is kiezen overbodig; bij meer zou een lijst nodig
  zijn.
- **Snelheid telt, eerlijk gemeten**: elke Pukk meet de reactietijd vanaf zijn
  eigen scherm. De host wacht op de gast 200 ms langer dan op zichzelf (de
  vraag van de gast verschijnt later, zijn antwoord komt later binnen). Bij
  een verbindingsinterval van 15 ms is dat ruim. Het eerste goede antwoord
  wint 500–1000 punten; de ander is "te laat". Fout = alleen jij ligt eruit.
- **Pack automatisch overzetten**: de gast vergelijkt naam en de CRC uit de
  header (die het hele bestand dekt). Bij verschil stuurt de host het pack
  met hetzelfde uploadprotocol als de app; de gast ziet de gewone
  upload-pop-up.
- **Berichten zonder antwoord** (write without response, notificaties): de
  link-laag van BLE herhaalt pakketten zelf, en NimBLE heeft geen rij voor
  meerdere writes met antwoord tegelijk. Writes met antwoord gebruikt de host
  alleen voor CCCD's en BEGIN/END, nooit twee tegelijk.
- **Eén taak voor de engine** (`components/battle/battle.c`) met een
  wachtrij: UI-tikken, BLE-events en timers komen daar binnen, dus geen
  locks. De regels (`battle_rules.c`) en berichten (`battle_msg.c`) zijn
  losse C-code met host-tests.
- **Tikken met tijdstempel**: de UI stempelt een tik meteen
  (`esp_timer`), de engine rekent de reactietijd uit vanaf het moment dat
  de vraag getoond is. Beide Pukks hebben dezelfde render- en touchvertraging,
  dus die valt weg.
- **Pack-pagina**: een pack aantikken opent eerst een pagina met Spelen en
  Battle (één tik extra voor solo, maar battle is zo vindbaar). Het
  beginscherm heeft bovenaan een kaart Meedoen.
- De gedeelde vraagweergave (`quiz_view.c`) wordt door solo en battle
  gebruikt; battle tekent er de scores van beide spelers en de keuze van de
  tegenstander (blauwe stip) omheen.

### Valkuilen

- **Scannen zonder duplicate filter.** De controller van de C6 filtert
  dubbele advertenties standaard op adres en vergeet er nooit één
  (`BT_LE_SCAN_DUPL_TYPE_DEVICE`, refresh 0). Zag de host een Pukk vóór die
  op Meedoen tikte, dan kwam de advertentie mét vlag nooit meer door.
- **Interval.** De gast vraagt na verbinden 15–30 ms (zoals bij telefoons) en
  krijgt dan 30 ms; de host vraagt daarna zelf weer 15 ms.

### Gemeten (beide borden, 0.4.0)

- Vrije heap na boot 183 KB (was 195 KB): de tweede verbinding, de
  central-rol en de battle-taak (5 KB stack, 2,8 KB nooit gebruikt).
- Verbinden: binnen 0,2 s als de host al zoekt; vanaf "gevonden" tot klaar
  (MTU, discovery, notificaties) ~0,5 s.
- Link: 15 ms, 2M PHY, data length 251.

## Roadmap

Geen geplande fases. **Geluid** (ES8311 + speaker bij goed, fout en tijd,
eerder fase 5) is geschrapt: een stille Pukk is juist prettig. De I2S-pinnen
staan nog in de pinnentabel voor als het ooit toch komt.

## Geheugen: statisch (uit `idf.py size`)

Stand na fase 0 (v0.1.0):

| | Bytes |
|---|---|
| DIRAM totaal beschikbaar | 452 112 |
| `.text` in IRAM (o.a. BLE-controller, ISR's) | 91 376 |
| `.bss` (waarvan 64 KB LVGL-heap) | 74 408 |
| `.data` | 10 108 |
| "Remain" volgens `idf.py size` | 276 220 |
| App-image in flash | 1,16 MB (OTA-slot 3 MB) |

De echte heap bij de start van `app_main` is groter: 340 716 B intern (zie
hieronder). `idf.py size` rekent geheugen dat de ROM-bootloader na het
opstarten vrijgeeft niet mee.

## Open vragen

1. **Accu.** Welke accu (capaciteit) zit erop? 500 mA laadstroom is alleen
   veilig vanaf ~500 mAh. De TS-pin heeft een 10k-NTC op het bord (RP2), maar
   de meting staat uit zoals in de demo's; daarmee is er waarschijnlijk geen
   temperatuurbewaking tijdens laden. Uitzoeken of we die aan willen zetten
   (registers 0x30 bit 1 en 0x50).
2. **Rommel bij opstarten?** De init-sequentie zet het scherm aan (0x29) vóór
   het eerste LVGL-frame. Als er kort ruis zichtbaar is: helderheid 0 tot het
   eerste frame.
3. **Uploadsnelheid.** ~4,5 KB/s vanaf de Mac met bleak, terwijl link en
   apparaat veel meer aankunnen. Meten met de web-app (Chrome).
4. **BLE-beveiliging.** Nu kan iedereen in de buurt packs uploaden en
   verwijderen. Pairing/bonding toevoegen?
5. **Firmware-update via BLE.** Willen we OTA? De partitietabel houdt er
   rekening mee.
6. **Packs op SD-kaart?** De TF-slot deelt de SPI-bus met het scherm; kan,
   maar SD-verkeer en schermupdates moeten dan om de bus delen.
7. **LVGL-heap.** 64 KB is een gok; bijstellen met de memory monitor.
8. **S3-variant.** Welk bord precies? Met PSRAM gelden andere geheugenkeuzes.


## Geheugen bij boot (gemeten)

Board 1, v0.1.0, 14-10-2026. Regels `heap after ...` uit de boot-log. "Intern"
is inclusief 15 KB LP-RAM (RTC RAM), die niet DMA-capable is; daarom ligt
"DMA" steeds ~14,6 KB lager.

| Na stap | Intern vrij | Δ | DMA vrij | Grootste DMA-blok |
|---|---|---|---|---|
| boot (start `app_main`) | 340 716 | | 326 112 | 311 296 |
| nvs | 339 672 | −1 044 | 325 068 | 311 296 |
| pmu | 338 316 | −1 356 | 323 712 | 311 296 |
| display | 336 252 | −2 064 | 321 648 | 311 296 |
| touch | 335 992 | −260 | 321 388 | 311 296 |
| lvgl | 235 084 | **−100 908** | 220 480 | 212 992 |
| ui | 235 084 | 0 | 220 480 | 212 992 |
| buttons | 230 344 | −4 740 | 215 740 | 212 992 |
| storage | 228 672 | −1 672 | 214 068 | 212 992 |
| ble | 201 792 | **−26 880** | 187 188 | 184 320 |
| na 60 s (statusregel) | 201 776 | | | |

Wat het kost:

- **LVGL: ~165 KB** = 101 KB heap (2 × 46 080 B draw buffers, 8 KB taakstack,
  driverstructuren) + 64 KB vaste LVGL-heap in `.bss`. Het testscherm zelf kost
  geen systeemheap; objecten staan in de LVGL-heap.
- **BLE: ~70 KB** = 27 KB heap (controller, host, host-taak) + ~43 KB statisch
  (vooral controllercode in IRAM).
- Knoppen: 4 KB taakstack. LittleFS: 1,7 KB.
- Na alles is er **~200 KB** heap vrij, in één blok van 180 KB.

Stacks (nooit gebruikt, na 60 s): main 4 556 van 6 144 B, LVGL 4 472 van
8 192 B. Main kan terug naar 4 KB. LVGL ging in fase 2 naar 10 KB (zie daar).

Overige observaties uit dezelfde boot:

- Accu aanwezig: 4 184 mV (100%), laden klaar, USB 5,24 V, systeem 4,39 V.
- Scherm-init duurt ~1 s: 300 ms ALDO3-reset plus 600 ms na sleep-out (de
  waarden uit de Waveshare-sequentie).
- Timer: na 15 s verschijnt "Tijd is om!".
- Handmatig getest en in orde: touch raakt de juiste knoppen (oriëntatie uit
  de Waveshare-demo klopt), IO10 start de vraag opnieuw, BLE-verbinding.
- BLE-naam van board 1: `Pukk-EA66` (MAC `d4:05:92:b8:ea:66`).
- `E esp_core_dump_flash: Incorrect size of core dump image`: de
  coredump-partitie bevat nog resten van de vorige (Waveshare-)firmware. Eén
  keer per bord leegmaken:
  `esptool -p PORT erase-region 0x620000 0x10000`.
