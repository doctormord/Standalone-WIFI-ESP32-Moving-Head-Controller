# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-20)
> `/ultrareview` (gegen `origin/main`, Details siehe `history.md` 2026-08-20)
> fand acht Findings im aktuellen Code (`Audio_Engine.h`, `FX_Engine.h`,
> `Moving_Head_Horizon.ino`, `WebAPI.h`, `data/index.html`), alle gefixt:
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
> `pio run` + `pio run -t buildfs` beide `[SUCCESS]`.
>
> **Geflasht** — aber nicht über `pio run -t upload` (scheiterte zweimal
> mit „No serial data received", weil PlatformIOs Default-Reset-Logik das
> Board vor der Verbindung zurücksetzte und damit den manuell erzwungenen
> Bootloader-Zustand aufhob). Stattdessen `esptool` direkt mit
> `--before no-reset` aufgerufen — falls das nächste Mal wieder manuell
> geflasht werden muss, siehe `history.md` 2026-08-20 für den exakten
> Befehl (Firmware auf `0x0`, LittleFS auf `0x290000`).
>
> **Noch NICHT verifiziert:**
> - Netzwerk-Reachability-Check von der Entwicklungsmaschine aus schlug
>   fehl (`movinghead.local` nicht erreichbar — vermutlich anderes WLAN,
>   kein Firmware-Hinweis). Boot-Erfolg über HTTP noch nicht bestätigt.
> - Live-Verhalten aller acht Fixes am echten Gerät, besonders Hard Sync
>   und der `/colfx`-Farbglitch-Fix.
> - Der `beatCount`-Movement-Sync-Fix und die neuen FX-Defaults aus der
>   vorigen Session (2026-08-19) liefen im selben Flash mit — ebenfalls
>   noch nicht live nachgetestet (16 Beats/120 BPM sollte jetzt sauber
>   rotieren, Movement-FX-Panel sollte mit 10 %/1000 ms öffnen).
>
> **Noch NICHT committed** — steht als nächstes an, danach `future` gegen
> `origin/future` push-abgleichen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Alle oben genannten offenen Punkte am echten Gerät/im Browser
>    nachtesten (auf demselben WLAN wie das Gerät), dann committen + pushen.
> 2. Zeit-Rampen für den Shake ("wa-wa-wosh"), falls weiterhin gewünscht.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: zurückgestellte Restrukturierungen, Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen,
>    übrige Tech-Debt-Punkte.

## Aktueller Status

Neunzehn-plus Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`), einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake, und — neu in dieser
Session — dem ersten vollständigen `/ultrareview`-Durchlauf gegen den
realen aktuellen Quellcode (nicht gegen den veralteten `origin/main`-Diff
mit alten V1/V2/V3-Snapshots). Zielhardware: **ESP32-C3 Supermini**
(Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository ist sowohl
lokal als auch auf GitHub (`future`-Branch) git-versioniert. Gerät wurde
in dieser Session neu geflasht (siehe oben); IP/mDNS-Erreichbarkeit von
der Entwicklungsmaschine aus aktuell nicht bestätigt.

## Was in dieser Session (2026-08-20) gemacht wurde

`/ultrareview` gegen `origin/main` (mit Altlasten/Docs/Vendor-Binaries
ausgeschlossen) lief nach zwei fehlgeschlagenen Versuchen (Session-Limit,
Rechner-Sleep) beim dritten Mal durch und fand acht Findings im aktuellen
Code. Alle acht gefixt (siehe Banner oben und `history.md` für Details je
Fix), `functions.md` für die geänderten Routen aktualisiert, kompiliert
(`pio run`, `pio run -t buildfs`) und auf das echte Gerät geflasht (via
direktem `esptool`-Aufruf, da `pio run -t upload` am Reset-Handling
scheiterte). Details: `history.md` (2026-08-20).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
