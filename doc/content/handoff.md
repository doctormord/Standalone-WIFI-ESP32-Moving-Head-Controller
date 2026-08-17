# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Dritte Runde direktes Nutzer-Feedback, noch am selben Tag wie der
> zweite Hands-on-Test.** User hatte die 6 Fixes aus der vorigen Runde
> (Motor-Stop, Modulator-Speed, Poll-Race, Mode/Curve, Jog-Snapback,
> Joystick-Curve v1) getestet und meldete: die Joystick-Curve wirkt
> immer noch nicht spürbar, der Speed/Curve/Momentum-Kontrollblock fehlt
> im Programmer- und Followspot-Tab, ein toter „Curve"-Button im
> Followspot-Tab, der „Advanced Motors"-Block im Programmer-Tab sei jetzt
> redundant, ein optisch „eingefrorener" gestrichelter Marker im
> Followspot-Joystick, und F5/Reload springt immer zurück ins Live-Tab.
> Alle 6 Punkte bearbeitet, neu geflasht, per `curl` als online bestätigt.
>
> **Gefixt (Details in `history.md`, 2026-08-17 zweite Fortsetzung):**
> 1. **Joystick-Curve komplett neu gebaut** (v1 aus der vorigen Runde war
>    technisch korrekt, aber praktisch unsichtbar, weil der
>    Momentum-Blend in ~150ms konvergiert — jede Kurven-Umformung darüber
>    ist zu kurz, um wahrgenommen zu werden). Jetzt eine eigene,
>    zeitbasierte 2-Sekunden-Rampe (`joyHoldTime`/`accelMul`), komplett
>    entkoppelt vom Momentum-Blend, die NUR beim aktiven Halten der
>    Auslenkung greift — Loslassen/Abbremsen bleibt exakt wie vorher
>    (unverändertes Momentum-Verhalten).
> 2. **Neue gemeinsame Komponente `JoystickAdvancedControls`** (Max
>    Speed/Curve/Momentum), jetzt in Live-, Programmer- UND
>    Followspot-Tab eingebunden (vorher nur inline im Live-Tab).
> 3. **Toter „Curve"-Button im Followspot-Tab entfernt** (war ein
>    `<Pill>` ganz ohne `onClick`), ersetzt durch den echten,
>    funktionierenden Kontrollblock.
> 4. **„Advanced Motors"-Accordion im Programmer-Tab entfernt** (Motor
>    Speed CH5/Pan Fine CH15/Tilt Fine CH16 manuelle Regler) — auf
>    expliziten Wunsch, da mit dem neuen gemeinsamen Block redundant.
>    State/Sync dieser Felder bleibt bestehen, nur die UI-Regler sind weg.
> 5. **Followspot-Joystick-Marker geglättet** — der gestrichelte
>    „reale Position"-Ring sprang bisher nur alle ~2s (Poll-Kadenz) und
>    stand dazwischen still, wirkte wie ein eingefrorener Fremdkörper.
>    Jetzt per kleiner `requestAnimationFrame`-Ease-Schleife kontinuierlich
>    geglättet.
> 6. **Tab-Wahl übersteht jetzt F5/Reload** (`localStorage`, Key `hz_tab`,
>    gleiches Muster wie `night`/`accent`).
>
> **Was noch NICHT visuell/physisch verifiziert ist** (alle 6 Punkte nur
> am Code + durch Kompilieren/Flashen bestätigt):
> - Joystick-Curve fühlt sich jetzt tatsächlich wie eine 2s-Beschleunigung
>   an (nicht nur theoretisch berechnet).
> - Alle drei Tabs zeigen den Speed/Curve/Momentum-Block konsistent.
> - Followspot-Marker bewegt sich jetzt sichtbar statt zu „kleben".
> - F5 im Browser öffnet wieder den zuletzt aktiven Tab.
> - Weiterhin unverifiziert aus der vorigen Runde: Motor stoppt wirklich,
>   Rotation läuft ruckelfrei, FX-Toggle bleibt stabil, Jog snappt zurück.
> - Aus früheren Runden weiterhin offen: 4 FX-Panels, Blackout-Panic-
>   Button, Chaser-Restart-Verhalten, Stage-Map-Kalibrierung im Browser.
>
> **Bewusst nicht blind gefixt (unverändert, Fixture-DMX-Personality-
> Daten, siehe `backlog.md` → „Bekannte kleine Issues"):** Gobo-6-static-
> Nummerierung, Gobo-Chaser-Shake-Offset (`STEPFX_SCRATCH_OFFSET`).
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit der alte
> `firmware/`-Ordner beim GitHub-Push entfernt wurde.
>
> **Drei Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen (relevant für den geglätteten Followspot-
> Marker — eine echte Merge würde die zugrundeliegende ~2s-Sprunghaftigkeit
> an der Quelle beheben statt sie nur clientseitig zu kaschieren).
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Am Fixture/im Browser nachtesten, ob alle Fixes aus dieser UND der
>    vorigen Runde tatsächlich wie erwartet wirken — das ist der
>    wichtigste nächste Schritt, den nur der User erledigen kann.
> 2. Falls noch nötig: DMX-Sweep für Gobo-Nummerierung und Shake-Zone.
> 3. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 4. Danach frei: die zurückgestellten Restrukturierungen (inkl.
>    Polling-Loop-Merge), Preset-Engine-Split, ADS1115-Hardware-Joystick,
>    `jogBend` fertigbauen oder entfernen, übrige Tech-Debt-Punkte.

## Aktueller Status

Acht Review-/Test-Runden durch: `/code-review max` (15 Findings),
`/code-review` Vollcodebase (9 Findings), `/ultrareview` teilweise (18
gemeldet, 15 gefixt) + vollständig nachgeholt (8 Findings, alle gefixt),
erster echter Hardware-Test (1 Bug), zweiter echter Hands-on-Test (7
gemeldete Punkte, 5+2 gefixt, 2 zurückgestellt), dritte Runde direktes
Feedback zum Joystick (6 weitere Punkte, alle gefixt). Zielhardware:
**ESP32-C3 Supermini**. Repository ist sowohl lokal als auch auf GitHub
(`future`-Branch) git-versioniert und läuft auf echter Hardware.
Verbleibender Blocker: die zuletzt gefixten Verhaltensänderungen sind noch
nicht am laufenden Gerät/im Browser bestätigt.

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 17) gemacht wurde

1. Exploratorische Frage zu React-Code-Splitting/Vite beantwortet — dabei
   den Fund gemacht, dass React/Babel per CDN geladen wurden. Gefixt:
   lokal gehostet, gzip-komprimiert.
2. Stage-Map-Bild-Speichern/Laden geprüft, drei kleine Punkte gefixt.
3. `/code-review` (Vollcodebase) ergab 9 Findings — alle gefixt.
4. Projekt auf GitHub gepusht (`future`-Branch): `V1`/`V2`/`V3`/`firmware`
   entfernt, `images` behalten, `README.md` gezielt aktualisiert.
5. `/ultrareview` (1. Anlauf, Session-Limit-Ausfälle) — 15 von 18 gefixt.
6. `/ultrareview` (2. Anlauf, „nachholen") — 8 von 8 gefixt, größter Fund:
   4 von 6 FX-Panels an tote State-Keys gebunden.
7. Erster echter Hardware-Test: geflasht, doppelten `Content-Encoding`-
   Header gefunden und gefixt, erneut geflasht und verifiziert.
8. Zweiter echter Hands-on-Test: 7 vom User live am Fixture beobachtete
   Bugs plus 2 Nachträge — 5+2 root-caused und gefixt (FX-Stop-Reset,
   Modulator-Speed-Skalierung, Poll/Toggle-Race, Mode/Curve-Key-Mismatch,
   Jog-Snapback, Joystick-Curve v1), 2 bewusst nicht blind gefixt
   (Gobo-Nummerierung, Chaser-Shake).
9. Dritte Runde, direktes Feedback zum Joystick: Curve v1 reichte nicht
   (neu gebaut, zeitbasierte 2s-Rampe statt pow-Reshape), gemeinsame
   `JoystickAdvancedControls`-Komponente in alle drei Bewegungs-Tabs
   eingebaut, toter Curve-Button im Followspot-Tab entfernt, „Advanced
   Motors"-Accordion im Programmer-Tab entfernt, Followspot-Positions-
   Marker geglättet, Tab-Wahl übersteht jetzt F5/Reload.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
