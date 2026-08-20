# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-20)
> **Gerät braucht manuelle Neukonfiguration, bevor irgendetwas anderes
> Sinn macht.** Ein `esptool`-Flash-Workaround hat versehentlich die
> komplette `nvs`-Partition gelöscht (WLAN-Zugangsdaten, Fixture-Patch,
> Master-Brightness, Smoothing, alle 10 Preset-/Chaser-Slots) — Details/
> Root-Cause siehe `history.md` (2026-08-20, Fortsetzung) und `backlog.md`
> → „Kürzlich gefixt" (Post-Mortem-Eintrag). Kein Firmware-Bug: die neue
> Firmware läuft korrekt, fällt aber mangels gespeicherter WLAN-Daten auf
> den AP-Fallback zurück. **Schritte:**
> 1. Mit WiFi `Moving_Head_Ctrl` / `12345678` verbinden.
> 2. `http://192.168.4.1` (oder `http://movinghead.local` im selben Netz)
>    öffnen, echte WLAN-Zugangsdaten über das Settings-Panel setzen.
> 3. Fixture-Patch neu eintragen.
> 4. Alle vorher gespeicherten Presets/Chaser-Szenen neu anlegen — kein
>    Backup vorhanden.
> 5. Erst danach: die acht `/ultrareview`-Fixes und der `beatCount`-Fix
>    von unten live gegenprüfen (besonders Hard Sync, `/colfx`-`mv`,
>    Rotation-Pulse-Timing, 16-Beat-Movement-Sync).
>
> **Guard gegen eine Wiederholung ist bereits gebaut und committed:**
> `scripts/flash_esptool.sh` flasht Bootloader/Partitionstabelle/
> `boot_app0`/Firmware jetzt einzeln an ihren echten Offsets statt als
> einen gemergten Blob — die `nvs`-Lücke bleibt dabei unangetastet.
> `CLAUDE.md` warnt jetzt explizit vor dem gemergten-Blob-Fehler.
>
> ---
>
> **Was vor dem Flash-Vorfall fertig war (Code-seitig unverändert gültig):**
> `/ultrareview` gegen `origin/main` (kein lokaler `main`, siehe
> `history.md`) fand acht Findings im aktuellen Code, alle gefixt:
>
> 1. `triggerSceneFX()`/`onArtDmx()` liessen `colWasActive`/`sgWasActive`/
>    `rgWasActive` stehen — Preset-Recall bzw. Art-Net-Übernahme konnten
>    einen Tick später vom `runStep()`-Stop-Fallback überschrieben werden.
> 2. `/modfx` klammerte `st`/`en` nicht (jetzt 0–255, wie alle Schwester-
>    Routen).
> 3. `/colfx` bekam die `mv`-Restore-on-Stop-Logik, die `/sgobfx`/
>    `/rgobfx` schon hatten (Frontend sendet jetzt `colorBase + colorOff`).
> 4. Hard Sync (`/sync`) war für `trigger==1` (BPM-sync) FX wirkungslos —
>    `/sync` und `manualTap` setzen jetzt zusätzlich `beatCount = 0;
>    lastBeatTime = now`, das die tatsächlich massgebliche Phase treibt.
> 5. `float(millis())`-Präzisionsverlust im Rotation-Pulse-Shake nach
>    ~4,66 h Laufzeit behoben (Modulo jetzt im Integer-Bereich).
> 6. Totes `d.fw`-Feld im Settings-Panel — neues `FW_VERSION`-Define jetzt
>    über `/api/state` exponiert.
> 7. (Simplification) Acht fast identische Diff/Fetch-Blöcke in
>    `data/index.html` zu einem `syncFx()`-Helper zusammengefasst.
>
> `functions.md` für die geänderten Routen-Parameter aktualisiert.
> `pio run` + `pio run -t buildfs` beide `[SUCCESS]`. Alles **committed und
> gepusht** (`future` == `origin/future`).
>
> **Nächste Schritte (nach der Neukonfiguration oben, keine feste
> Reihenfolge):**
> 1. Zeit-Rampen für den Shake ("wa-wa-wosh"), falls weiterhin gewünscht.
> 2. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 3. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 4. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 5. Danach frei: zurückgestellte Restrukturierungen, Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen,
>    übrige Tech-Debt-Punkte.

## Aktueller Status

Neunzehn-plus Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`), einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake, und — neu in dieser
Session — dem ersten vollständigen `/ultrareview`-Durchlauf gegen den
realen aktuellen Quellcode. Zielhardware: **ESP32-C3 Supermini** (Fixture:
SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl lokal als
auch auf GitHub (`future`-Branch) git-versioniert und aktuell deckungsgleich
mit `origin/future`. **Gerät selbst ist aktuell nicht konfiguriert** — läuft
mit korrekter neuer Firmware, aber ohne WLAN-Zugangsdaten/Patch/Presets
(siehe Banner oben) im AP-Fallback-Modus (`Moving_Head_Ctrl`).

## Was in dieser Session (2026-08-20) gemacht wurde

1. `/ultrareview` gegen `origin/main` (Altlasten/Docs/Vendor-Binaries
   ausgeschlossen) lief nach zwei fehlgeschlagenen Versuchen beim dritten
   Mal durch, fand acht Findings — alle gefixt, `functions.md`
   aktualisiert, kompiliert, committed, gepusht.
2. Beim Versuch, das Gerät zu flashen, scheiterte `pio run -t upload`
   zweimal am Reset-Handling. Der `esptool`-Workaround dafür hat
   versehentlich die `nvs`-Partition gelöscht (siehe Banner oben) —
   Post-Mortem in `history.md`/`backlog.md`, Guard-Script
   `scripts/flash_esptool.sh` gebaut und `CLAUDE.md` entsprechend ergänzt,
   beides committed.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-20).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
