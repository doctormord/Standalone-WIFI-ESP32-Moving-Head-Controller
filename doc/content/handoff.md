# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-15)
> `Moving_Head_Horizon` (Merge vom 2026-08-14) **kompiliert jetzt wieder**
> (Build-Blocker gefixt, siehe unten) — aber noch **nicht auf Hardware
> geflasht/verifiziert** in diesem Zustand. Zielhardware: **ESP32-C3
> Supermini**.
>
> **Nächste Schritte (empfohlene Reihenfolge):**
> 1. **Einmal in der Arduino IDE gegenkompilieren und flashen**, um den
>    Fix aus dieser Session zu verifizieren (hier nicht kompilierbar
>    getestet, siehe `handover.md` → Setup & Flashen für Board-Settings).
> 2. **Regressionen aus der Juni-Session nachziehen:** `syncBeats[]`-Clamping
>    und Slot-Bounds-Guards waren bereits gefixt (siehe `history.md`,
>    2026-06-14), sind im gemergten Stand aber wieder offen. Siehe
>    `backlog.md` → Tech Debt.
> 3. Danach frei: Preset-Engine-Split oder ADS1115-Hardware-Joystick
>    angehen (siehe `handover.md` → Geplante Erweiterungen), oder die
>    kleineren Tech-Debt-Punkte (jogBend, fadeDuration-Kopplung,
>    Output-Build-Kadenz).
>
> Reihenfolge ab Schritt 3 offen — vom User abklären.

## Aktueller Status

Frisch gemergtes Repository, Build-Blocker behoben, **noch nicht auf
Hardware geflasht/verifiziert** in diesem Zustand. Zielhardware:
**ESP32-C3 Supermini**. Vorgänger-Backend lief stabil mit 2 Fixtures
(18-Channel Pro Beam 280) — dieser Stand hat zusätzlich das neuere
"V3"-Frontend.

## Was in dieser Session gemacht wurde

1. `CLAUDE.md` für zukünftige Claude-Code-Sessions erstellt (Architektur,
   Build-Hinweise, bekannte Lücken).
2. Alten Chat-Export (`claude-exports-ESP32_ArtnetDMX.zip`, Session vom
   2026-06-14 zum Vorgänger-Projekt `horizon_light_controller`) ausgewertet
   und dessen `backlog.md`/`handover.md`/`handoff.md`-Artefakte als
   Ausgangsbasis für die neue Doku-Struktur unter `doc/content/` übernommen
   und aktualisiert.
3. `Moving_Head_redesign.zip` (älterer Projektstand + Frontend-Redesign
   `index_claude.html`) ausgewertet und mit diesem Repo verglichen:
   - Build-Blocker-Ursache bestätigt und **gefixt**: `StepFX colFX, sgobFX,
     rgobFX;` in `Moving_Head_Horizon.ino` ergänzt.
   - `index_claude.html` (Frontend-Redesign-Prototyp) geprüft — keine
     verlorene Funktionalität gegenüber dem aktuellen `data/index.html`
     gefunden, kein Merge nötig.
   - Details in `history.md` (zwei Einträge vom 2026-08-15).
4. `Readme.md` (Englisch, Projekt-Root) aktualisiert/aktuell gehalten.
5. `doc/content/functions.md` (Englisch) als vollständige Funktions-/
   API-Referenz mit allen Parametern angelegt.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
