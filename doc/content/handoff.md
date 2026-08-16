# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-16)
> Der Code lebt auf GitHub:
> `github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller`,
> Branch **`future`**. Dieses lokale Verzeichnis ist selbst ein Git-Repo,
> das diesen Branch trackt (`origin`, SSH). `README.md` (Repo-Root) ist
> die kanonische Projektbeschreibung.
>
> **Kompilier-Stand:** `pio run` und `pio run -t buildfs` liefen zuletzt
> beide `[SUCCESS]`. **Noch nicht auf echter Hardware getestet** — das ist
> nach wie vor der größte offene Punkt vor jedem Live-Einsatz.
>
> **`/ultrareview` lief nur teilweise durch:** Hauptorchestrator und
> mehrere Teil-Agenten sind an einem Account-Session-Limit gescheitert
> (Reset war 3:10 Uhr Europe/Berlin). Drei Teilreviews liefen komplett,
> 15 von 18 daraus gemeldeten Findings wurden noch gefixt (siehe unten).
> **Ein erneuter `/ultrareview`-Lauf** (jetzt, nach Ablauf des Limits)
> würde die ausgefallenen Teil-Agenten (line-by-line-Scan, removed-
> behavior, cross-file-tracer, wrapper/proxy) nachholen — sinnvoller
> nächster Schritt, falls noch nicht gemacht.
>
> **Bekannter offener Punkt:** Der One-Click-Web-Installer (`install.html`
> → `firmware/manifest.json`) ist kaputt, seit der alte `firmware/`-Ordner
> beim GitHub-Push entfernt wurde. Braucht einen frisch gebauten
> `firmware/`-Ordner (echter Hardware-Build + Upload) — bewusst nicht in
> dieser Session erledigt (Quellcode-Push war der Auftrag, keine
> Binary-Artefakte).
>
> **Drei Findings bewusst zurückgestellt** (echte Restrukturierungen,
> siehe `backlog.md` → Tech Debt): `SceneData`-NVS-Format-Versionierung
> (würde bestehende gespeicherte Presets ohne Migration brechen),
> `/api/get_dmx`s JSON-String-Bau auf `snprintf`/ArduinoJson umstellen,
> die zwei unsynchronisierten Frontend-Polling-Loops zusammenlegen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Auf echter Hardware flashen und verifizieren — insbesondere den
>    Blackout-Panic-Button-Fix, die Chaser-Konfig-Persistierung, und die
>    `dt`-Clamp-Änderung in `updateEngines()` (Bewegungsverhalten nach
>    einem WLAN-Verbindungsaufbau-Stall).
> 2. `/ultrareview` erneut laufen lassen, um die ausgefallenen Teil-Agenten
>    nachzuholen.
> 3. `firmware/`-Ordner neu aufbauen, damit der One-Click-Installer wieder
>    funktioniert.
> 4. Danach frei: die drei zurückgestellten Restrukturierungen, Preset-
>    Engine-Split, ADS1115-Hardware-Joystick, oder die übrigen
>    Tech-Debt-Punkte in `backlog.md`.

## Aktueller Status

Vier vollständige (bzw. teilweise vollständige) Review-Runden durch:
`/code-review max` (15 Findings), `/code-review` Vollcodebase (9 Findings),
`/ultrareview` teilweise (18 gemeldete, 15 gefixt). Alle gefixten Findings
sind kompilier-/größen-verifiziert. Zielhardware: **ESP32-C3 Supermini**.
Repository ist sowohl lokal als auch auf GitHub (`future`-Branch)
git-versioniert. **Einziger großer Blocker vor Live-Einsatz: kein Test auf
echter Hardware bisher.**

## Was in dieser Session (Fortsetzung, 2026-08-15/16) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet —
   dabei den konkreten Fund gemacht, dass React/Babel bisher per CDN
   geladen wurden (bricht im AP-Only-Offline-Betrieb). Auf Wunsch gefixt:
   lokal gehostet, gzip-komprimiert (`data/vendor/`), neue `/vendor/*`-
   Routen in `WebAPI.h`.
2. Stage-Map-Bild-Speichern/Laden (Follower-Modus) geprüft, drei kleine
   Robustheits-/UX-Punkte gefunden und gefixt.
3. `/code-review` (Vollcodebase) ergab 9 weitere Findings — alle gefixt,
   inkl. eines Zusatzfunds: `/chaser_cfg` war der einzige Pfad, der
   Chaser-Konfiguration in NVS persistierte.
4. Projekt auf GitHub gepusht (`future`-Branch): `V1`/`V2`/`V3`/`firmware`
   entfernt, `images` behalten, aktueller Code + Doku hinzugefügt,
   bestehendes `README.md` gezielt aktualisiert statt ersetzt.
5. `/ultrareview` gestartet — Session-Limit-Ausfälle bei mehreren
   Teil-Agenten, 3 liefen durch, 15 von 18 gemeldeten Findings gefixt
   (6 davon selbst verifizierte, echte Bugs: `/autofade`-NaN-Risiko,
   `dt`-Clamp, `/set_all`-Clamp, `beatInterval`-Zero-Guard, doppelte
   Magic-Channel-Numbers, `presetActive`-Falsy-Zero-Bug).

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15/16).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
