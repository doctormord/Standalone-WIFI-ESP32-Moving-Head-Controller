# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-20)
> **Gerät ist konfiguriert, erreichbar und läuft mit der neuesten Firmware.
> Kein offener Blocker.**
>
> **Was in dieser Session gemacht wurde (alles geflasht, live per `curl`
> gegengeprüft, Details in `history.md` 2026-08-20 „dritte"/„vierte
> Fortsetzung" und `backlog.md` → „Kürzlich gefixt"):**
>
> 1. Drei vom User gemeldete Frontend/Backend-Sync-Bugs gefixt (FX-Start/
>    Stop-Button-Race, Gobo-Dropdown-Readback, `Cache-Control: no-store`
>    gegen Browser-Cache-Verwirrung).
> 2. **Echter Root Cause für „Movement zuckt nur" bei Global-BPM-Sync
>    gefunden und gefixt:** jeder echte Mic-Beat setzte `manualTap = true`,
>    dessen Handler `beatCount` sofort wieder auf 0 setzte. Plus ein
>    unabhängiger `modSp`-Formel-Bug im Movement-Size/Speed-Modulator.
> 3. **Movement-„Figure 8"-Bug vollständig aufgeklärt und gefixt.** Kein
>    Software-Bug in der Pattern-Mathematik (per Live-Telemetrie bewiesen:
>    das gesendete DMX-Signal ist ein perfekter Kreis) — echter,
>    physischer Tilt-Encoder-/Motor-Defekt dieser konkreten Fixture-
>    Einheit unterhalb von DMX 16-Bit 32768 (~Tilt 127/128), bestätigt über
>    drei unabhängige Wege (projizierter Lichtpunkt direkt gefilmt, alle
>    Mirror/Invert-Settings ausgeschlossen, Fixture-eigenes „Tilt
>    Calibration"-Menü auf 0 getestet und ausgeschlossen, Fixture-eigenes
>    „Sensor Monitor"-Diagnosemenü zeigt den eingefrorenen Tilt-Encoder
>    direkt). Software-Workaround aktiv: `MovementEngine::getValues()`
>    verschiebt das Tilt-Zentrum eines Patterns automatisch so weit nach
>    oben, dass sein voller Bewegungsradius die Problemzone nie erreicht —
>    Kreis bleibt ein echter Kreis, nur automatisch höher zentriert.
>    Volle technische Doku in `mapping_sheds_160w_3in1_gobo.md` → CH3/CH4.
> 4. Neuer AUDIO-Tab gebaut (Echtzeit-Band-Graph + Live-Tuning), dabei
>    zwei weitere echte Bugs im Audio-Pipeline gefunden und gefixt (Beat-
>    Tick-Latch, strukturell totes Mid-Band).
>
> Alles mit `pio run` + `pio run -t buildfs` gegenkompiliert, Firmware +
> Filesystem mehrfach geflasht, jeder Fix live per `curl`/Telemetrie-
> Sampling gegengeprüft — keine Vermutungen ungetestet stehen gelassen.
>
> ---
>
> **Kein offener Punkt aus dieser Session.** Danach frei (keine feste
> Reihenfolge):
> 1. Mic-Sensitivity subjektiv mit echter Musik über den neuen AUDIO-Tab
>    gegenprüfen (Wirkung ist per `curl` bestätigt, Wertebereich ggf. zu
>    fein — siehe `backlog.md`).
> 2. Mic-BPM-Oktave-Korrektur (aus einer früheren Session) noch nicht mit
>    echtem Audio verifiziert.
> 3. Zeit-Rampen für den Shake ("wa-wa-wosh"), falls weiterhin gewünscht.
> 4. Separates Followspot-Joystick-Profil?
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Übrige Tech-Debt-Punkte in `backlog.md` (Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen, etc.)

## Aktueller Status

Repository ist sowohl lokal als auch auf GitHub (`future`-Branch)
git-versioniert. Zielhardware: **ESP32-C3 Supermini** (Fixture: SHEHDS 160W
3in1 GOBO / „Pro Beam 280", Pan 540°/Tilt 270°, bekannter physischer
Tilt-Encoder-Defekt um DMX 127/128 — siehe oben und `mapping_sheds_160w_
3in1_gobo.md`). Gerät ist konfiguriert, über `movinghead.local` erreichbar,
läuft mit der neuesten Firmware und dem neuesten Filesystem-Image (beide in
dieser Session mehrfach geflasht).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
