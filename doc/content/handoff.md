# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-20)
> **Gerät ist konfiguriert, erreichbar und läuft mit der neuesten Firmware.**
> Kein offener Blocker. Der einzige unbestätigte Punkt: der User sollte die
> Movement-„Figure 8"-Optik nochmal testen, diesmal mit dem Fixture auf eine
> entfernte Wand/Fläche gerichtet (projizierter Punkt, nicht das Gehäuse
> selbst filmen) — siehe „Offener Punkt" unten und `backlog.md` → „Offen".
>
> **Was in dieser Session gemacht wurde (alles geflasht, live per `curl`
> gegengeprüft, Details in `history.md` 2026-08-20 „dritte Fortsetzung" und
> `backlog.md` → „Kürzlich gefixt"):**
>
> 1. Drei vom User gemeldete Frontend/Backend-Sync-Bugs gefixt: `syncFx()`
>    markierte FX-Start/Stop als „gesendet", obwohl der Request wegen des
>    Poll-Zeitfensters übersprungen wurde (Button sprang nach ~2s zurück);
>    Gobo-Dropdown wurde nie vom Gerät zurückgelesen (zeigte nach Preset-
>    Recall die falsche Position); `master`/`damping`/`transMode`/`joyKey`
>    hatten dasselbe Snapshot-Bug-Muster wie `syncFx()`.
> 2. `index.html` bekam `Cache-Control: no-store` — vorher konnte ein
>    offener Browser-Tab beliebig lange alte JS im Speicher behalten und
>    sah wie ein Backend-Bug aus.
> 3. **Echter Root Cause für „Movement zuckt nur, keine Umdrehung" bei
>    Global-BPM-Sync gefunden:** jeder echte Mic-Beat setzte `manualTap =
>    true`, dessen Handler `beatCount = 0` macht — nullte den Beat-Zähler
>    im selben Loop-Durchlauf, in dem er gerade erst hochgezählt worden
>    war. Fix in `Audio_Engine.h`. Plus ein zweiter, unabhängiger Bug in
>    `MovementEngine::process()`s Size/Speed-Modulator (`modSp`-Formel).
> 4. **Figure-8-Meldung untersucht, kein Code-Bug gefunden.** Video-Frames
>    analysiert (dafür `ffmpeg` lokal installiert) — beide Videos zeigen
>    den Fixture-Kopf aus der Nähe, nicht einen projizierten Punkt auf
>    einer Wand. Wahrscheinlichste Erklärung: 3D-Projektionsgeometrie
>    einer Doppelachs-Rotation aus einem seitlichen Blickwinkel, kein
>    Software-Bug — unbestätigt, siehe oben.
> 5. **Neuer AUDIO-Tab gebaut** (auf User-Wunsch): Echtzeit-EKG-Graph
>    (Low/Mid/High + Threshold, ~15Hz) plus Live-Tuning für die vorher
>    hartkodierten Envelope-Follower-Parameter. Direkt beim ersten Live-
>    Test zwei echte, vorher unsichtbare Bugs damit gefunden und gefixt:
>    Beat-Tick-Anzeige las die falschen (nicht latchbaren) Flags; Mid-Band
>    war strukturell auf ~0 geklammert (Mid-/Slow-Attack hatten denselben
>    Shift-Wert).
>
> Alles mit `pio run` + `pio run -t buildfs` gegenkompiliert, Firmware +
> Filesystem mehrfach per `pio run -t upload`/`-t uploadfs` geflasht (der
> normale Auto-Reset funktionierte diesmal durchgehend).
>
> ---
>
> **Offener Punkt:** Figure-8-Test mit Wand-Projektion (siehe oben). Falls
> die Acht dort weiterhin auftritt, ist es ein echter Bug und braucht eine
> neue Untersuchung (dann bitte auch: exakte Size-%/Rotation-Werte, Anzahl
> Fixtures, Kamera-Position relativ zur Wand).
>
> **Danach frei (keine feste Reihenfolge):**
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
3in1 GOBO / „Pro Beam 280", Pan 540°/Tilt 270°). Gerät ist konfiguriert,
über `movinghead.local` erreichbar, läuft mit der neuesten Firmware und dem
neuesten Filesystem-Image (beide in dieser Session geflasht). Kein
Backup-/Config-Verlust mehr offen.

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
