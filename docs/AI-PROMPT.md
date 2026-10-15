# Een quizpack laten maken door een AI

Kopieer de prompt hieronder naar een AI-chat (Claude, ChatGPT, …) en vul
onder **Opdracht** in wat tussen [ ] staat. Sla het antwoord op als `.json`-bestand en
zet het op een Pukk:

- in de web-app: **Importeren**, nakijken in de editor, dan naar een Pukk
  sturen; of
- met de tool: `.venv/bin/python tools/pukk.py upload mijnpack.json`.

Lees de vragen altijd even na: een AI kan zich vergissen, en een fout "goed"
antwoord is bij een quiz extra vervelend. De editor en
`tools/pukk.py build` melden het als iets niet aan het formaat voldoet.

## Prompt

````text
Je maakt een quizpack voor Pukk: een klein quizapparaat met een vierkant
scherm van ongeveer 4 bij 4 cm waarop kinderen en volwassenen
meerkeuzevragen beantwoorden, alleen of tegen elkaar (wie het snelst goed
antwoordt, wint).

Opdracht
- Onderwerp: [ONDERWERP]
- Voor wie: [LEEFTIJD OF NIVEAU, bijv. "groep 6 (9-10 jaar)"]
- Aantal vragen in het pack: [AANTAL, bijv. 40]
- Vragen per spel: [AANTAL, bijv. 10, of "alle"]
- Volgorde: [door elkaar / op volgorde]
- Taal: Nederlands

Antwoord met alleen geldige JSON in één codeblok, in precies deze vorm:

{
  "title": "Dieren van de wereld",
  "questions_per_game": 10,
  "order": "random",
  "questions": [
    {
      "question": "Welk dier is het grootste dat ooit heeft geleefd?",
      "answers": ["Olifant", "Blauwe vinvis", "T. rex", "Giraf"],
      "correct": 1
    },
    {
      "question": "Is een walvis een vis?",
      "answers": ["Ja", "Nee"],
      "correct": 1,
      "time": 10
    }
  ]
}

Formaat (anders weigert Pukk het pack)
- "title": de naam van het pack, hoogstens 30 tekens.
- "questions_per_game": hoeveel vragen één spel heeft (1-1000). Laat het veld
  weg als elk spel alle vragen moet hebben. Heeft het pack meer vragen, dan
  kiest Pukk er per spel willekeurig zoveel uit.
- "order": "random" (door elkaar) of "sequential" (in de volgorde van het
  pack).
- "question": hoogstens 100 tekens.
- "answers": 2 tot 4 antwoorden, elk hoogstens 25 tekens.
- "correct": de positie van het goede antwoord, tellend vanaf 0 (het eerste
  antwoord is 0, het tweede 1, enzovoort).
- "time": seconden om te antwoorden, 5 tot 60. Laat het veld weg voor de
  standaard van 15 seconden. Geef 10 voor korte, snelle vragen en 20 tot 30
  voor lange vragen of rekenwerk.
- Gebruik alleen: letters (ook é, ë, ü, ç, ß en andere Europese accenten),
  cijfers, gewone leestekens en × ÷ ° € ‘ ’ “ ” – …
  Niet: emoji, pijlen, tekens als √ ≤ π, of letters uit een ander schrift.
- Geen opmaak (geen markdown, geen HTML) en geen regeleinden binnen een tekst.

Goede vragen
- Precies één antwoord is juist. Controleer elk feit; laat een vraag liever
  weg dan dat je twijfelt.
- Foute antwoorden zijn geloofwaardig: dezelfde soort en ongeveer dezelfde
  lengte als het goede, bijvoorbeeld een veelgemaakte vergissing. Geen
  grapantwoorden, geen "alle bovenstaande" of "geen van beide".
- Verdeel het goede antwoord eerlijk over de posities; niet steeds dezelfde.
- Kort en duidelijk, passend bij de leeftijd: het scherm is klein en er is
  weinig tijd.
- Elke vraag staat op zichzelf: vragen worden willekeurig gekozen, dus nooit
  "en de vorige?" of "zie vraag 3".
- Geen dubbele vragen, en varieer de vorm (niet tien keer "Wat is de
  hoofdstad van ...").
- Vermijd feiten die snel veranderen (huidige records, wie nu ergens de baas
  is), tenzij het onderwerp erom vraagt.
- Meestal 4 antwoorden; 2 voor ja/nee- of waar/niet-waar-vragen.

Controleer voor je antwoordt: het aantal vragen klopt, elke "correct" wijst
naar het juiste antwoord, alle teksten zijn binnen de lengtes, en de JSON is
geldig (dubbele aanhalingstekens, geen komma na het laatste element).
````
