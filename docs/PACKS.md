# Quizpacks: formaat en BLE-protocol (v1)

Dit is het contract tussen het apparaat en alles wat packs maakt of uploadt
(`tools/pukk.py`, later een app of webpagina). Alle getallen zijn
little-endian.

## Bronformaat (JSON)

Packs worden geschreven als JSON en met `tools/pukk.py build` omgezet naar het
binaire `.pukk`-bestand dat het apparaat leest.

```json
{
  "title": "Algemene kennis",
  "questions": [
    {
      "question": "Welke planeet staat het dichtst bij de zon?",
      "answers": ["Venus", "Mercurius", "Mars", "Aarde"],
      "correct": 1,
      "time": 15
    }
  ]
}
```

- `answers`: 2 tot 4 antwoorden. `correct` is de index (vanaf 0).
- `time`: seconden voor deze vraag, 5–60. Mag weg; standaard 15.
- Tot fase 2 (eigen font) zet `build` letters met accenten om naar ASCII
  (é → e) en waarschuwt daarbij.

Limieten (in bytes UTF-8, zonder afsluitende 0):

| Veld | Max |
|---|---|
| titel | 40 |
| vraag | 120 |
| antwoord | 32 |
| aantal vragen | 1000 |
| packbestand | 1 MiB |

## Binair formaat (`.pukk`)

```
Header, 32 bytes
  0   char[4]  magic          "PUKK"
  4   u8       version        1
  5   u8       flags          0
  6   u16      question_count
  8   u32      file_size      hele bestand, in bytes
  12  u32      crc32          CRC-32 (IEEE, zoals zlib.crc32) van het hele
                              bestand, berekend met dit veld op 0
  16  u32      index_offset   begin van de vragentabel (32)
  20  u32      strings_offset begin van de stringtabel
  24  u32      strings_size
  28  u32      title          stringoffset van de titel

Vragentabel, question_count × 24 bytes, vanaf index_offset
  0   u32      text           stringoffset
  4   u32[4]   answers        stringoffsets; ongebruikt = 0xFFFFFFFF
  20  u8       answer_count   2..4
  21  u8       correct        0..answer_count-1
  22  u8       time_s         0 = standaard (15 s)
  23  u8       reserved       0

Stringtabel, strings_size bytes, vanaf strings_offset
  UTF-8, elke string afgesloten met 0. Stringoffsets zijn relatief aan
  strings_offset. De laatste byte van de tabel is 0.
```

`strings_offset + strings_size == file_size`. Het apparaat controleert bij
upload en bij het starten van een pack de header, alle offsets en limieten en
de CRC.

## BLE-protocol

Service `8e410001-0580-4871-83bd-51273ef66407`.

| Characteristic | UUID | Gebruik |
|---|---|---|
| CTRL | `8e410002-…` | write: verzoeken; notify: antwoorden |
| DATA | `8e410003-…` | write without response: bestandsdata |
| INFO | `8e410004-…` | read: JSON met firmwareversie en vrije flash |
| PACKS | `8e410005-…` | read: JSON-lijst van de packs op het apparaat |

PACKS geeft bijvoorbeeld:

```json
[{"name":"voorbeeld","title":"Voorbeeld","questions":8,"size":667}]
```

Langer dan één MTU is geen probleem: de client doet dan zelf een lange read.

Zet eerst notificaties op CTRL aan. Eén verzoek tegelijk; wacht op het
antwoord.

### Verzoeken (client → CTRL)

| Opcode | Naam | Payload |
|---|---|---|
| `0x01` | BEGIN | `u32 size, u32 crc32, u8 name_len, name` |
| `0x02` | END | – |
| `0x03` | ABORT | – |
| `0x04` | DELETE | `u8 name_len, name` |

`name`: 1–32 tekens `a-z`, `0-9`, `-`, `_`. Het bestand wordt
`/packs/<name>.pukk`; een bestaand pack met dezelfde naam wordt vervangen.

### Antwoorden (CTRL → client, notify)

| Opcode | Naam | Payload |
|---|---|---|
| `0x80` | OK | `u8 request_opcode` + bij BEGIN: `u16 max_chunk` |
| `0x81` | PROGRESS | `u32 received` |
| `0x82` | RESEND | `u32 received` |
| `0x8F` | ERROR | `u8 request_opcode, u8 code` |

Fouten in DATA-writes melden `ERROR` met request_opcode `0x10`.

Foutcodes:

| Code | Betekenis |
|---|---|
| 1 | bezig (er loopt al een upload) |
| 2 | ongeldig verzoek (lengte, naam, opcode) |
| 3 | niet genoeg ruimte |
| 4 | geen upload actief |
| 5 | data niet op volgorde (wordt als RESEND gemeld, niet als ERROR) |
| 6 | grootte of CRC klopt niet |
| 7 | geen geldig pack |
| 8 | schrijf- of leesfout |
| 9 | niet gevonden |

### Upload

1. `BEGIN(size, crc32, name)` → `OK(BEGIN, max_chunk)`.
2. DATA-writes: `u32 offset` + maximaal `max_chunk` bytes data, op volgorde.
   Elke 4 KiB en aan het eind stuurt het apparaat `PROGRESS(received)`.
   - Houd hoogstens 8 KiB onbevestigd onderweg (offset − laatste PROGRESS).
     Writes zonder response kunnen onderweg verloren gaan; CoreBluetooth
     gooit ze bijvoorbeeld weg als zijn buffer vol is.
   - Mist er data, dan negeert het apparaat wat erna komt en stuurt één keer
     `RESEND(received)`: ga verder vanaf `received`. Data die het apparaat al
     heeft wordt genegeerd, dus opnieuw sturen is altijd veilig.
   - Komt er 2 s geen PROGRESS of RESEND terwijl er nog data onbevestigd is,
     ga dan verder vanaf de laatste PROGRESS.
3. Wacht op `PROGRESS(size)` en stuur dan `END` → het apparaat controleert
   grootte, CRC en het packformaat, en antwoordt `OK(END)` of
   `ERROR(END, code)`.

`max_chunk` = ATT-MTU − 3 − 4. Een verbroken verbinding breekt een lopende
upload af; de oude versie van het pack blijft dan staan.
