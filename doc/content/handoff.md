# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-18)
> **Drei Nachschärfungen am Rotation-Pulse-Shake (CH7) + Stop-Race im
> Gobo-Chaser-Frontend gefixt, nach echtem UI-Test-Feedback.** User testete
> die am 2026-08-17 gebaute Rotation-Pulse-Shake-Technik im Browser und
> meldete drei getrennte Probleme: (1) Shake zu groß bei niedriger Speed,
> Gobo rollt zum Nachbarn raus; (2) Shake sollte während eines Gobo-Wechsels
> nicht laufen (choppy); (3) Stop-Druck bei Static/Rotating-Gobo-Chaser
> nimmt manchmal nicht an — springt nach ~1s wieder auf „run".
>
> **Gebaut:**
> - **Fix 1** (`runStep()` in `Moving_Head_Horizon.ino`): Puls-Dauer ist
>   jetzt eine feste Konstante (50ms, gedeckelt bei sehr hohen Speeds),
>   statt `period/4` — `scratchSpeed` steuert nur noch die Ruhezeit
>   zwischen Pulsen, nicht mehr die Pulsweite. Verhindert Drift bei
>   niedrigen Speeds.
> - **Fix 2** (`runStep()`): neues Settle-Fenster (`SHAKE_SETTLE_MS = 220`)
>   nach jedem Gobo-Schritt (`fx.lastStepTime`) — unterdrückt Rotation-Pulse
>   UND den nativen CH8-Shake-Fallback für 220ms nach einem Wechsel, fällt
>   in der Zeit auf den planen Gobo-Wert zurück.
> - **Fix 3** (`data/index.html`): neue Funktion `tFetchImmediate()` (gleiches
>   Bypass-Muster wie `sendJoy`s Stop-Fall) — Stop-Kommandos für
>   `sgFxRunning`/`rgFxRunning` umgehen jetzt `tFetch`s Debounce-Queue
>   komplett, statt sich nur auf das 2,5s-`dirtyUntilRef`-Fenster zu
>   verlassen.
>
> **Live per curl verifiziert:** Fix 1 — bei `spd=0.3`/`rng=60` auf Gobo 5
> blieb CH7 in 34 von 35 Samples (100ms-Takt) auf dem Anker-Wert 50, ein
> Sample traf den erwarteten, begrenzten Pulswert 112. Backend-seitiger
> Stop-Restore (atomares `mv`) bestätigt weiterhin sauber — die gemeldete
> Stop-Race war also rein im Frontend, nicht im Backend.
>
> **Noch NICHT vom User im Browser/an der echten Hardware bestätigt:**
> - Fix 1: fühlt sich die Amplitude bei niedriger Speed jetzt physisch
>   richtig an (nicht nur die DMX-Rohwerte per curl)?
> - Fix 2: wirkt der Gobo-Wechsel jetzt sauber statt choppy?
> - Fix 3: greift der Stop im Browser jetzt zuverlässig beim ersten Versuch?
>
> **Nebenbefund, kein Bug:** Nach dem Flashen war das Gerät mehrere Minuten
> weder per `movinghead.local` noch curl erreichbar — Ursache war (wieder)
> nur eine flakey mDNS-Auflösung, das Gerät lief die ganze Zeit sauber unter
> seiner festen LAN-IP (`192.168.8.113`). Dabei auch geklärt: bei
> deaktiviertem „USB CDC on Boot" (Projektkonvention) trägt der USB-Port nur
> die ROM-Bootloader-Konsole, keine Sketch-`Serial.print()`-Ausgaben — von
> dieser Sandbox aus ist Live-Serial-Monitoring des laufenden Sketches daher
> grundsätzlich nicht möglich, nur der Bootloader ist sichtbar. Ein
> manueller DTR/RTS-Diagnoseversuch landete den Chip kurz im
> Bootloader-Downloadmodus — harmlos, durch erneutes Flashen behoben.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Die drei neuen Fixes am echten Gerät/im Browser nachtesten (siehe oben).
> 2. Falls alle drei sich bestätigen: das ist der stabile Stand, ab dem die
>    zuvor besprochenen Zeit-Rampen für den Shake („wa-wa-wosh") aufgesetzt
>    werden könnten, falls gewünscht.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: die zurückgestellten Restrukturierungen, Preset-Engine-
>    Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Siebzehn Review-/Test-Runden durch (Details siehe `history.md`), inklusive
eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake — von „Idee
verworfen" über „User korrigiert die Testtechnik" zu „funktioniert live
bestätigt" zu „nach echtem UI-Test nachgeschärft" in derselben
fortlaufenden Session. Zielhardware: **ESP32-C3 Supermini** (Fixture:
SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl lokal als
auch auf GitHub (`future`-Branch) git-versioniert und läuft auf echter
Hardware, aktuell erreichbar unter der festen LAN-IP `192.168.8.113`
(mDNS `movinghead.local` bleibt bekannt-flakey, siehe Nebenbefund oben).

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 18) gemacht wurde

1–16: siehe vorige Handoff-Snapshots / `history.md`.
17. Nach echtem Browser-UI-Test des Rotation-Pulse-Shakes kam Feedback zu
    drei konkreten Problemen zurück (Amplitude bei niedriger Speed,
    Choppy-Wirkung bei Gobo-Wechsel, Stop-Race im Frontend) — alle drei
    gefixt, kompiliert, geflasht, Backend-Seite live per curl bestätigt.
    Browser-/Hardware-seitige Bestätigung der drei Fixes steht noch aus.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 18).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
