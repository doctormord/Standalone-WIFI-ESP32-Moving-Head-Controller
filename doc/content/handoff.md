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
> der einzige noch verbleibende große Blocker vor jedem Live-Einsatz.
>
> **`/ultrareview` ist jetzt vollständig durchgelaufen** (zweiter Anlauf,
> mit explizit vorgegebenem `git diff origin/main...origin/future` als
> Diff-Base) und hat 8 Findings geliefert — alle gefixt. Der größte davon:
> vier der sechs FX-Panels im Programmer-Tab (Movement/Dimmer/Gobo-Rot/
> Prisma-Rot) waren an tote State-Keys gebunden und ihre Trigger/Sync/
> Speed-Regler dadurch komplett wirkungslos — vermutlich ein sehr alter,
> unbemerkter Bug. **Dieser Fix verdient besonders einen echten Browser-
> Test**, da UI-Verhalten sich per Compile-Check nicht beobachten lässt.
>
> **Bekannter offener Punkt:** Der One-Click-Web-Installer (`install.html`
> → `firmware/manifest.json`) ist kaputt, seit der alte `firmware/`-Ordner
> beim GitHub-Push entfernt wurde. Braucht einen frisch gebauten
> `firmware/`-Ordner (echter Hardware-Build + Upload) — bewusst nicht
> automatisch erledigt (Quellcode-Push war der Auftrag, keine
> Binary-Artefakte).
>
> **Drei Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung (würde bestehende gespeicherte Presets ohne
> Migration brechen), `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Auf echter Hardware flashen und verifizieren — insbesondere die
>    4 neu wieder funktionsfähigen FX-Panels, den Blackout-Panic-Button,
>    die Chaser-Konfig-Persistierung/Restart-Fix, und die `dt`-Clamp in
>    `updateEngines()`.
> 2. `firmware/`-Ordner neu aufbauen, damit der One-Click-Installer wieder
>    funktioniert.
> 3. Danach frei: die drei zurückgestellten Restrukturierungen, Preset-
>    Engine-Split, ADS1115-Hardware-Joystick, oder die übrigen
>    Tech-Debt-Punkte in `backlog.md`.

## Aktueller Status

Fünf Review-Runden durch: `/code-review max` (15 Findings), `/code-review`
Vollcodebase (9 Findings), `/ultrareview` teilweise (18 gemeldet, 15
gefixt) + vollständig nachgeholt (8 Findings, alle gefixt). Alle gefixten
Findings sind kompilier-/größen-verifiziert. Zielhardware:
**ESP32-C3 Supermini**. Repository ist sowohl lokal als auch auf GitHub
(`future`-Branch) git-versioniert. **Einziger großer Blocker vor
Live-Einsatz: kein Test auf echter Hardware bisher.**

## Was in dieser Session (Fortsetzung, 2026-08-15/16) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet — dabei
   den Fund gemacht, dass React/Babel per CDN geladen wurden (bricht im
   AP-Only-Offline-Betrieb). Gefixt: lokal gehostet, gzip-komprimiert.
2. Stage-Map-Bild-Speichern/Laden geprüft, drei kleine Punkte gefixt.
3. `/code-review` (Vollcodebase) ergab 9 Findings — alle gefixt, inkl.
   Zusatzfund: `/chaser_cfg` war der einzige Persistierungs-Pfad für
   Chaser-Konfiguration.
4. Projekt auf GitHub gepusht (`future`-Branch): `V1`/`V2`/`V3`/`firmware`
   entfernt, `images` behalten, `README.md` gezielt aktualisiert.
5. `/ultrareview` (1. Anlauf) — Session-Limit-Ausfälle, 3 Teilreviews
   liefen durch, 15 von 18 Findings gefixt.
6. `/ultrareview` (2. Anlauf, „nachholen") — mit explizitem Diff-Befehl
   vollständig durchgelaufen, 8 von 8 Findings gefixt. Größter Fund: 4 von
   6 FX-Panels an tote State-Keys gebunden (Trigger/Sync/Speed-Regler
   komplett wirkungslos), plus `/chaser`-Restart-Bug, Frontend-`track()`-
   Drop-Bug, fehlender Swap-Guard in drei Endpunkten, eingefrorene
   Pan/Tilt-Anzeige, und eine selbst verursachte NVS-Check-Regression aus
   der letzten Runde.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15/16).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
