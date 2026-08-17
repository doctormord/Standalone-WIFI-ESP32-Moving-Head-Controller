# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Shake bekommt echte Speed-/Range-Parameter, plus ein eigener
> Folgefehler aus der vorigen Runde gefunden und gefixt.** Noch am selben
> Tag wie die Joystick-Curve-v3-Runde kam weiteres Live-Test-Feedback:
> der Gobo-Chaser-Shake läuft mit korrektem Delay, aber Speed/Range lassen
> sich nicht einstellen; nach dem Stoppen schüttelt das Gobo-Rad
> eigenständig weiter; nach dem Stoppen des Gobo-Rot-Chasers bringt die
> Auswahl von „White(0)" im Programmer-Tab nichts, erst ein anderer Wert
> und dann zurück auf 0 funktioniert; Movement-FX „Size" bei 0 sollte
> geklammert werden; ein Layout-Bug bei zwei „MAX"-Reglern (noch offen).
>
> **Gefixt (Details in `history.md`, 2026-08-17 vierte Fortsetzung):**
> 1. **Shake ist jetzt eine echte, einstellbare Ramp** statt eines reinen
>    An/Aus auf einen fixen Wert: neue `scratchSpeed`/`scratchRange`-
>    Parameter, kontinuierliche Dreieckswellen-Oszillation innerhalb der
>    5-DMX-breiten Shake-Zone (jeden Frame neu berechnet). Neue UI-Regler
>    „Shake speed"/„Shake range" in `ChaserFx`. **Bewusst nicht in
>    `SceneData`/NVS persistiert** (würde die Größe des Preset-Blobs
>    ändern und echte gespeicherte Presets auf diesem Gerät zurücksetzen)
>    — live-only, resettet bei Preset-Recall/Neustart auf Default.
> 2. **Shake lief nach dem Stoppen weiter** — gleiches Muster wie der
>    CH9-Bug: `runStep()` schrieb den Kanal nur bei jedem Chase-Schritt,
>    nie beim Stoppen. Landete der Kanal gerade in der Shake-Zone, schüttelt
>    das Fixture mit seiner EIGENEN Firmware weiter. Jetzt schreibt
>    `runStep()` beim Stoppen einmalig den regulären (nicht-shakenden)
>    Wert des zuletzt gewählten Gobos, für alle drei Wheel-Chaser
>    (Color/Static-Gobo/Rot-Gobo).
> 3. **Eigener Folgefehler:** der `track()`-Skip-Fix aus der vorigen Runde
>    hielt die Vergleichs-Baseline still auf dem unveränderten manuellen
>    Wert synchron — traf der erste manuelle Wert nach dem Stoppen
>    zufällig damit zusammen (meist: beide „0"), erkannte `track()`
>    fälschlich „keine Änderung" und sendete nichts. Jetzt erzwingt der
>    erste Aufruf nach Skip-Ende immer einen Resend, unabhängig vom
>    Baseline-Vergleich.
> 4. **Movement-FX „Size" jetzt auch serverseitig auf 1–100 geklammert**
>    (`/fx` und der Preset-Ladepfad) — vorher nur im Frontend, ein
>    gespeichertes `size=0` hätte die Bewegung unsichtbar kollabieren
>    lassen, während die FX weiter „läuft" gemeldet wird.
>
> **Bewusst zurückgestellt, nicht geraten:** „Die beiden Slider für MAX
> in Movement FX sind größer als das Layout initial" — ohne Browser-
> Zugriff nicht sicher genug lokalisierbar (welche zwei Regler genau?
> Speed/Size End in Movement FX, oder der neue „Max Speed"-Regler im
> Joystick-Block?). Braucht eine genauere Beschreibung oder einen
> Screenshot vom User, statt blind an der falschen Stelle zu suchen.
>
> **Was noch NICHT visuell/physisch verifiziert ist:** ob sich Shake jetzt
> wie eine echte Ramp anfühlt und Speed/Range wirken, ob Shake nach dem
> Stoppen wirklich aufhört, ob „White(0) nach Stop" jetzt sofort
> funktioniert. Plus alles aus den vorigen Runden am selben Tag (Motor-
> Stop, Modulator-Speed, Joystick-Curve v3, Fixture-Datenblatt) —
> siehe die vorigen Handoff-Runden in `history.md`.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit der alte
> `firmware/`-Ordner beim GitHub-Push entfernt wurde.
>
> **Vier Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung (jetzt auch relevant für die neuen
> Shake-Parameter, falls die live-only-Einschränkung mal stört),
> `/api/get_dmx`s JSON-String-Bau auf `snprintf`/ArduinoJson umstellen,
> die zwei unsynchronisierten Frontend-Polling-Loops zusammenlegen, der
> Layout-Bug bei den „MAX"-Reglern (braucht erst mehr Info).
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Am Fixture/im Browser nachtesten — insbesondere Shake-Speed/Range,
>    Shake-Stop-Verhalten, und den „White(0) nach Stop"-Fix. Wichtigster
>    nächster Schritt, den nur der User erledigen kann.
> 2. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug
>    liefern, damit er gezielt gefixt werden kann.
> 3. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 4. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Elf Review-/Test-Runden durch (Details siehe `history.md`), inklusive
eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert und läuft auf
echter Hardware. Diese Session zeigt inzwischen ein wiederkehrendes
Muster: sehr schnelle, iterative Fix-Runden auf direktes Live-Feedback
hin führen gelegentlich selbst zu neuen, subtilen Bugs (siehe die
Curve-Snap-Regression und den Track-Force-Resend-Folgefehler) — beide
wurden nur gefunden, weil der User konsequent nach jedem Fix weiter live
am Gerät testet, nicht durch eigene Vorab-Verifikation.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1–10: siehe vorige Handoff-Snapshots / `history.md`.
11. Fünfte Live-Test-Runde: Gobo-Chaser-Shake von reinem An/Aus zu
    echten Speed-/Range-Parametern ausgebaut (bewusst live-only, nicht
    NVS-persistiert); Shake-Weiterlaufen nach Stop gefixt (gleiches
    Muster wie CH9); eigenen Folgefehler im `track()`-Skip-Mechanismus
    gefunden und mit einem Force-Resend-Flag gefixt; Movement-FX-Size
    serverseitig auf 1–100 geklammert; ein Layout-Bug bewusst
    zurückgestellt statt blind geraten.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
