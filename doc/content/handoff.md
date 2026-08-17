# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-17)
> **Zweite Runde echter Hands-on-Hardware-Tests** — User hat das Gerät
> tatsächlich in Betrieb genommen (Fixture live beobachtet, nicht nur
> `curl`) und einen konkreten Fehlerbericht mit 7 Punkten geliefert, plus 2
> Nachträge zum Movement-Joystick/Jog. 5 von 7 Kernpunkten + beide
> Nachträge root-caused und gefixt, 2 bewusst nicht blind gefixt (Details
> unten). Neu geflasht (Firmware + LittleFS) und per `curl` als online
> bestätigt (reale Preset-Namen intakt).
>
> **Gefixt (Details in `history.md`, 2026-08-17 Fortsetzung):**
> 1. `kill_fx`/FX-Stop bewegte Gobo-Rotation/Prisma-Rotation nicht auf 0
>    (Motor lief nach Stop sichtbar weiter) — `updateEngines()` schrieb die
>    DMX-Kanäle nur bei aktivem FX, jetzt einmaliger Reset auf 0 bei der
>    aktiv→inaktiv-Flanke.
> 2. Dimmer-/Gobo-Rot-/Prisma-Rot-FX „extrem ruckelig" — `Modulator::
>    process()`s Speed-Divisor war nicht auf die Frontend-Defaults
>    kalibriert (~25ms statt ~0,5s Zykluszeit). Divisor 100→2000 angehoben.
> 3. Dimmer-FX „schaltet sich selbst aus" / Color-Chaser „läuft nach Stop
>    weiter oder geht selbst wieder an" — echte Race Condition zwischen
>    2s-Poll und lokalem Toggle (User-Diagnose „sync problem" war richtig).
>    Neuer `dirtyUntilRef`-Mechanismus schützt frisch geänderte
>    Running-Flags 2,5s vor überschreibenden Poll-Antworten.
> 4. „Curves haben keine Funktion" (Dimmer/Gobo-Rot/Prisma-Rot) — noch ein
>    Fall des Mode/Curve-Dropdown-Key-Mismatch-Musters aus der letzten
>    Runde (Langform- statt Kurzform-State-Keys), das damals bei
>    Trigger/Sync/Speed gefixt, bei Mode/Curve aber übersehen wurde.
> 5. Jog-Regler (Live-Tab) snappte nach Loslassen nicht auf Mitte zurück —
>    `onRelease` hing an einem toten, unsichtbaren Dummy-Element statt am
>    echten `RangeSlider`. **Wichtig:** `jogBend` selbst bewegt weiterhin
>    keine DMX-Kanäle (separates, älteres „toter Code"-Thema, siehe
>    `backlog.md`) — dieser Fix betrifft nur das visuelle Zurückspringen.
> 6. Movement-„Curve"-Regler (virtueller Joystick/Pfeiltasten) ohne
>    Wirkung — Tastatur-/Volldeflection-Input ist strukturell immer ein
>    Einheitsvektor (`pow(1, curve) == 1`), Kurve wirkte nie. Jetzt auf den
>    geglätteten Rampen-Wert statt auf den rohen Input angewendet.
>
> **Bewusst nicht blind gefixt** (Fixture-DMX-Personality-Daten, nicht am
> Code verifizierbar, in `backlog.md` unter „Bekannte kleine Issues"
> festgehalten):
> - „Gobo 6 static kommt nicht" — `SGOBOS`/`sGoboMap` sind seit Einführung
>   unverändert und intern konsistent (per `git log` verifiziert), also
>   vermutlich eine Diskrepanz zur echten Fixture-Personality. Braucht
>   Datenblatt oder manuellen DMX-Sweep auf CH7.
> - „Gobo chaser auf shake läuft einfach durch" — `STEPFX_SCRATCH_OFFSET
>   (183)` war selbst ein ungeprüfter Platzhalter aus der letzten Runde,
>   erzeugt nachweislich keinen echten Shake. Braucht Fixture-Datenblatt
>   oder Hardware-Sweep, kein zweiter Blindschuss.
>
> **Was noch NICHT visuell/physisch verifiziert ist** (alle Fixes wurden
> nur am Code + durch Kompilieren/Flashen bestätigt, nicht am laufenden
> Fixture beobachtet):
> - Motor stoppt wirklich bei Gobo-Rot/Prisma-Rot-FX-Stop und `kill_fx`.
> - Rotation läuft jetzt ruckelfrei bei Zeiten ≠ 1ms.
> - FX-Running-Toggle bleibt stabil (kein Selbst-An/Aus mehr).
> - Mode/Curve-Regler bei Dimmer-/Gobo-Rot-/Prisma-Rot-FX wirken sich aus.
> - Jog snappt visuell auf Mitte zurück.
> - Movement-Curve fühlt sich wie eine echte Anfangsbeschleunigung an.
> - Aus der vorherigen Runde weiterhin offen: UI im Browser, 4 FX-Panels,
>   Blackout-Panic-Button, Chaser-Restart-Verhalten, Stage-Map-Kalibrierung.
>
> **Bekannter offener Punkt (unverändert):** Der One-Click-Web-Installer
> (`install.html` → `firmware/manifest.json`) ist kaputt, seit der alte
> `firmware/`-Ordner beim GitHub-Push entfernt wurde. Braucht einen frisch
> gebauten `firmware/`-Ordner.
>
> **Drei Findings weiterhin bewusst zurückgestellt** (echte
> Restrukturierungen, siehe `backlog.md` → Tech Debt): `SceneData`-NVS-
> Format-Versionierung, `/api/get_dmx`s JSON-String-Bau auf
> `snprintf`/ArduinoJson umstellen, die zwei unsynchronisierten Frontend-
> Polling-Loops zusammenlegen.
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Am Fixture nachtesten, ob die 6 obigen Fixes tatsächlich wie erwartet
>    wirken — das ist der wichtigste nächste Schritt, den nur der User
>    (Augen auf UI + Lampe) erledigen kann.
> 2. Falls noch nötig: DMX-Sweep für Gobo-Nummerierung und Shake-Zone
>    (CH7/CH8, 0–255 langsam durchfahren, echte Werte notieren) — Basis für
>    einen zweiten, diesmal datengestützten Fix-Versuch.
> 3. `firmware/`-Ordner neu aufbauen, damit der One-Click-Installer wieder
>    funktioniert.
> 4. Danach frei: die drei zurückgestellten Restrukturierungen, Preset-
>    Engine-Split, ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder
>    entfernen, oder die übrigen Tech-Debt-Punkte in `backlog.md`.

## Aktueller Status

Sieben Review-/Test-Runden durch: `/code-review max` (15 Findings),
`/code-review` Vollcodebase (9 Findings), `/ultrareview` teilweise (18
gemeldet, 15 gefixt) + vollständig nachgeholt (8 Findings, alle gefixt),
erster echter Hardware-Test (1 Bug gefunden+gefixt), zweiter echter
Hands-on-Test (7 gemeldete Punkte, 5+2 gefixt, 2 bewusst zurückgestellt).
Zielhardware: **ESP32-C3 Supermini**. Repository ist sowohl lokal als auch
auf GitHub (`future`-Branch) git-versioniert und läuft auf echter Hardware.
Verbleibender Blocker: die gerade gefixten Verhaltensänderungen sind noch
nicht am laufenden Fixture bestätigt.

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
   Jog-Snapback, Joystick-Curve), 2 bewusst nicht blind gefixt
   (Gobo-Nummerierung, Chaser-Shake — beides Fixture-DMX-Personality-Daten
   ohne Datenblatt nicht verifizierbar).

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 17).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
