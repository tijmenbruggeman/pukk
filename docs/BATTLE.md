# Battle: twee Pukks, één quiz (v2)

Eén Pukk start een battle met een pack (de **host**), een tweede doet mee
(de **gast**). Beide spelen dezelfde vragen tegelijk; de host houdt de stand
bij en beslist per vraag wie wint. Geen telefoon of app nodig.

## Spelregels

- De host kiest bij Start de vragen van dit spel, volgens de instellingen
  van het pack (aantal per spel, op volgorde of door elkaar; zie
  [PACKS.md](PACKS.md)). Beide spelers krijgen dezelfde vragen in dezelfde
  volgorde.
- Elke speler ziet de vraag op zijn eigen scherm en heeft de tijd uit het
  pack (standaard 15 s).
- **Wie als eerste goed antwoordt, wint de vraag** en krijgt
  `500 + 500 × (resterende tijd / totale tijd)` punten (afgerond, dus 500 tot
  1000). Voor de ander is de vraag dan voorbij: **"Te laat!"**.
- Een **fout** antwoord kost niets, maar je kunt die vraag niet meer
  beantwoorden; de ander mag nog.
- Heeft niemand het goed (fout of tijd op), dan krijgt niemand punten.
- "Eerst" gaat op **reactietijd**: elke Pukk meet zelf vanaf het moment dat
  de vraag op zijn scherm verschijnt. De vraag van de gast verschijnt iets
  later en zijn antwoord komt iets later binnen; daarom wacht de host op de
  gast 200 ms langer dan op zichzelf, zowel voor "was de gast sneller?" als
  voor "is de tijd op?". Zo maakt Bluetooth-vertraging (enkele tientallen ms)
  niet uit.
- Precies even snel goed: de host wint.
- Na elke vraag tonen beide schermen ~2,5 s de uitslag, daarna de volgende.
  Aan het eind: de eindstand en wie er wint.
- KEY op een van beide Pukks stopt de battle voor allebei.

## Verloop

1. Host: pack aantikken › **Battle**. Hij wacht op een tegenstander.
2. Gast: **Meedoen** op het beginscherm. Hij laat in zijn advertentie zien
   dat hij wil meedoen.
3. De host zoekt (BLE-scan) naar Pukks die willen meedoen en verbindt met de
   eerste die hij vindt (de host is BLE-*central*, de gast *peripheral*).
4. HELLO: de host noemt het pack (naam en CRC). Heeft de gast dat pack niet
   (of een andere versie), dan stuurt de host het over met het
   uploadprotocol uit [PACKS.md](PACKS.md); daarna opnieuw HELLO.
5. Lobby: de gast is klaar; de host tikt **Start**. Aftellen 3-2-1.
6. Per vraag: QUESTION › antwoorden › RESULT. Na de laatste: END.

## Bluetooth

### Meedoen-advertentie

Een gast in "meedoen"-stand voegt manufacturer data toe aan zijn
advertentie: company ID `0xFFFF` (gereserveerd voor tests/intern gebruik),
dan `'P' 'K'` en een statusbyte: `1` = wil meedoen.

### BATTLE-characteristic

`8e410006-0580-4871-83bd-51273ef66407` in de Pukk-service, op elke Pukk.
De host schrijft (write with response), de gast antwoordt met notificaties.
Alle getallen little-endian.

Host → gast:

| Type | Naam | Payload |
|---|---|---|
| `0x01` | HELLO | `u8 version` (2), `u8 len, pack name`, `u32 pack_crc32`, `u16 question_count`, `u8 len, host name` |
| `0x03` | START | `u8 countdown_s`, `u16 rounds` |
| `0x04` | QUESTION | `u16 round`, `u16 question` |
| `0x06` | RESULT | `u16 round`, `u8 winner`, `u8 host_choice`, `u16 host_ms`, `u8 guest_choice`, `u16 guest_ms`, `u16 points`, `u32 host_total`, `u32 guest_total` |
| `0x07` | END | `u32 host_total`, `u32 guest_total` |
| `0x08` | ABORT | `u8 reason` |

Gast → host:

| Type | Naam | Payload |
|---|---|---|
| `0x81` | HELLO_ACK | `u8 version`, `u8 has_pack` |
| `0x84` | ANSWER | `u16 round`, `u8 choice`, `u16 reaction_ms` |
| `0x88` | LEAVE | – |

- `pack_crc32` is het `crc32`-veld uit de header van het `.pukk`-bestand.
  De gast heeft het pack als `/packs/<name>.pukk` bestaat met dezelfde CRC.
  Een pack met dezelfde naam maar een andere CRC wordt overschreven.
- `question_count` is het aantal vragen in het pack; `rounds` het aantal in
  dit spel. `round` telt vanaf 0, `question` is de index van de vraag in het
  pack.
- `winner`: `0` host, `1` gast, `0xFF` niemand.
- `choice` in RESULT: `0`–`3` het gekozen antwoord, `0xFE` geen antwoord en
  de ander was al goed ("te laat"), `0xFF` geen antwoord en de tijd is op.
- `reaction_ms`: van het verschijnen van de vraag tot de tik, gemeten door de
  Pukk die antwoordt. Na de tijdslimiet stuurt de gast niets.
- ABORT-redenen: `1` gestopt (KEY), `2` andere protocolversie, `3` pack kon
  niet worden overgezet.
- Versie 2 (firmware 0.5) voegde `rounds` en `question` toe; met een Pukk op
  versie 1 stopt de battle met "andere Pukk-versie".
- Een verbroken verbinding stopt de battle op beide Pukks.
