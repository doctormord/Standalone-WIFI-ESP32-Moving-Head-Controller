# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Vier weitere Live-Test-Fixes, plus drei Punkte bewusst zur Rückfrage
> an den User gestellt statt eines dritten Blindschusses.** Details in
> `history.md` (2026-08-17, letzte Fortsetzung).
>
> **Gefixt:**
> 1. „Manual speed"-Regler (Dimmer-/Gobo-Rot-/Prisma-Rot-FX) zeigten
>    irreführend „ms" an obwohl es eine echte 0–10000-Speed-Zahl ist
>    (höher=schneller, nicht länger=langsamer). `TriggerBlock` bekommt
>    einen `holdUnit`-Prop, für die drei Modulator-Stellen jetzt leer.
> 2. Gobo-Chaser-Stop geht jetzt atomar auf den manuellen Setup-Wert
>    (CH7/8) zurück statt auf die letzte Chaser-Position — neuer
>    `mv`-Parameter an `/sgobfx`/`/rgobfx`.
> 3. Abstand zwischen „Shake speed"/„Shake range"-Reglern vergrößert.
> 4. Shake-Oszillation lief vorher an der absoluten Systemzeit gekoppelt
>    unverändert über Gobo-Wechsel hinweg weiter (wirkte wie „eine Rampe
>    über mehrere Changes") — jetzt an `lastStepTime` gekoppelt, jedes
>    Gobo bekommt einen frischen, eigenen Shake-Zyklus.
>
> **Bewusst zur Rückfrage gestellt** (nach mehreren erfolglosen
> Guess-Runden, siehe Chat für die genaue Frage an den User):
> - Rotating-Gobo-Shake weiterhin „murksig", Range scheint Speed zu
>   beeinflussen — plausibel, dass das Fixture die Shake-Zone nur binär
>   interpretiert (feste interne Rate) und mein Sub-Zonen-Oszillations-
>   Modell am tatsächlichen Fixture-Verhalten vorbeirät. Braucht echte
>   Hardware-Rückmeldung statt weiterem Raten.
> - Curve/Momentum=0: 1 Keyboard-Tick bei Max Speed 2000 bewegt ~11 Steps
>   — könnte inhärente Konsequenz von „Curve=0 = wirklich sofort" sein
>   (explizit so gewünscht) oder ein eigener Bug.
> - Movement-Stop mit Momentum stoppt laut User abrupt statt weich zu
>   faden — kein Bug im Code gefunden, evtl. wird der On-Screen-Joystick
>   (eigene, unabhängige Spring-Animation) mit der physischen
>   Fixture-Bewegung verwechselt, oder der jetzt gefixte Release-Burst-Bug
>   hat das vorher überdeckt.
>
> **Was noch NICHT visuell/physisch verifiziert ist:** alle vier neuen
> Fixes, plus alles aus den vorigen Runden am selben Tag — siehe die
> vorigen Handoff-Runden in `history.md`.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit der alte
> `firmware/`-Ordner beim GitHub-Push entfernt wurde. Layout-Bug bei
> „MAX"-Reglern in Movement FX weiterhin ungeklärt (braucht Screenshot/
> genauere Beschreibung vom User).
>
> **Vier Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen, der Layout-Bug bei den „MAX"-Reglern.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Antworten auf die drei offenen Rückfragen (Shake-Design-Richtung,
>    Curve=0-Tap-Erwartung, Momentum-Fade-Reproduktion) — der wichtigste
>    nächste Schritt, damit die letzten drei Punkte gezielt statt blind
>    angegangen werden können.
> 2. Am Fixture/im Browser die vier neuen Fixes aus dieser Runde
>    nachtesten.
> 3. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 4. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 5. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Zwölf Review-/Test-Runden durch (Details siehe `history.md`), inklusive
eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Diese Session zeigt weiterhin das Muster: sehr schnelle,
iterative Fix-Runden auf direktes Live-Feedback funktionieren gut für
klar diagnostizierbare Bugs, stoßen aber bei Punkten, die von
undokumentiertem Fixture-internen Verhalten abhängen (Shake-Sub-Zonen),
an eine Grenze — dort ist eine gezielte Rückfrage inzwischen sinnvoller
als ein weiterer Blindschuss.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–11: siehe vorige Handoff-Snapshots / `history.md`.
12. Sechste Live-Test-Runde: „Manual speed"-Einheit korrigiert
    (Label-Bug, kein Formel-Bug), Gobo-Chaser-Stop geht jetzt atomar auf
    den manuellen Setup-Wert zurück, Shake-Slider-Abstand vergrößert,
    Shake-Phase pro Gobo-Wechsel zurückgesetzt (behebt „Rampe über
    mehrere Changes"). Drei Punkte (Shake-Sub-Zonen-Verhalten,
    Curve=0-Tap-Distanz, Momentum-Fade-Abruptheit) bewusst als
    Rückfrage an den User gestellt statt eines dritten Blindschusses.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
