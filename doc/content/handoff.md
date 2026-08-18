# Horizon Light Controller — Project Handoff & Status

> ## ⏭️ NEXT CHAT STARTS HERE (2026-08-18, Fortsetzung 2)
> Direkt im Anschluss an den Start/Stop-Race-Fix für alle FX-Typen kamen
> drei weitere, root-caused und gefixte Meldungen:
>
> 1. **„manual speed beim dimmer sollte milliseconds, ist es aktuell
>    nicht."** Bestätigt: `Modulator::process()` (`FX_Engine.h`)
>    interpretierte `speed` als inversen Kehrwert (Periode =
>    `1.000.000/speed` ms), nicht als literalen ms-Wert — obwohl der
>    Slider ("Manual speed", 0–10000/Step 100, `holdUnit=""`) eine
>    ms-Eingabe suggerierte. Gefixt: `speed` ist jetzt buchstäblich die
>    volle Zyklusdauer in ms, für `dimFX`/`gRotFX`/`pRotFX` (gemeinsame
>    `Modulator`-Klasse). `holdUnit=""` im Frontend entfernt (zeigt jetzt
>    "ms"). **Live per curl verifiziert:** gemessene Phasenposition über
>    5 Samples (inkl. zweier Wraps) stimmte exakt mit `speed=1000` →
>    1000ms-Periode überein.
> 2. **„der sync auf beat nicht sauber, mainly ist das licht iwie aus
>    anstatt an."** Root-Cause: Default `mode=0`(Forward)+`curve=3`(Sine)
>    machte das Licht exakt AUF dem Beat dunkel, kurz davor hell, dann
>    Sprung zurück auf dunkel — Gegenteil vom erwarteten "Flash on beat".
>    Fix: Default auf `mode=2` (Reverse/Decay) geändert (Backend-Klasse +
>    Frontend-Startzustand) → hell exakt auf dem Beat, Abfall bis zum
>    nächsten. Andere Modi bleiben per Dropdown wählbar. Nebenbefund
>    geprüft und ausgeschlossen: `globalBPM` kann nie 0 werden
>    (`constrain(60,180)` in `Audio_Engine.h`, `/beat` schreibt es nie) —
>    kein Divide-by-zero-Risiko. **Live per curl verifiziert:** `/sync` +
>    Start mit `mo=2,cu=3,tr=1` → CH1 direkt nach Sync-Reset bei 245
>    (nahe Maximum), danach abfallend — bestätigt "hell auf dem Beat".
> 3. **„hw mic im programmer button tut nichts... genauso wue ix
>    sensitity."** Root-Cause: Programmer-Tabs "HW MIC"-Button und
>    "Mic sensitivity"-Slider hingen an `state.micSync`/`state.micSens` —
>    zwei Felder, die *nirgendwo sonst* im Code vorkommen: nie
>    initialisiert, nie gelesen, nie ans Backend geschickt. Kein
>    Sync-Bug zwischen zwei echten Zuständen, sondern zwei tote,
>    rein dekorative Duplikate ohne jede `/hwaudio`-Anbindung. Fix:
>    `ProgrammerTab` bekommt jetzt dieselben Props (`micOn`,
>    `onMicToggle`, `hwSens`, `setHwSens`) wie `LiveTab` von der
>    App-Komponente durchgereicht.
>
> **Gebaut:** `FX_Engine.h` (`Modulator::process()` Phasenformel +
> Default-`mode`), `data/index.html` (`dimMo`-Default, `holdUnit`-Labels,
> `ProgrammerTab`-Props + Render-Aufruf).
>
> `pio run` + `pio run -t buildfs` beide `[SUCCESS]`, auf dem echten
> Gerät geflasht (`upload` + `uploadfs`), Gerät danach unter
> `192.168.8.113` erreichbar. Dimmer-Speed-Timing und Beat-Sync-Richtung
> live per curl bestätigt (Details oben). Änderungen noch NICHT committet
> — steht als nächstes an.
>
> **Noch NICHT vom User im Browser/an der echten Hardware bestätigt:**
> - Fühlt sich "Manual speed" jetzt wie eine echte ms-Angabe an (z.B.
>   2000 = 2 Sekunden Zykluszeit, spürbar langsamer als vorher)?
> - Fühlt sich Beat-Sync jetzt richtig an ("Licht an auf dem Beat" statt
>   "aus")?
> - Funktioniert der HW-Mic-Button jetzt im Programmer-Tab genauso wie
>   im Live-Tab (beide zeigen denselben Zustand, beide schalten das
>   Mikro tatsächlich um)?
> - **Aus vorigen Runden weiterhin offen:** Greift Stop jetzt bei allen
>   FX-Typen zuverlässig beim ersten Versuch? Wackeln die CH1/CH6/CH9/
>   CH11-Regler nicht mehr sichtbar während die FX läuft? Shake-Amplitude
>   bei niedriger Speed und das Settle-Fenster (kein Choppy-Effekt) am
>   echten Gerät bestätigt?
>
> **Nächste Schritte (Auswahl, keine feste Reihenfolge vorgegeben):**
> 1. Alle oben genannten offenen Punkte am echten Gerät/im Browser
>    nachtesten, dann committen + pushen.
> 2. Falls alles sich bestätigt: Zeit-Rampen für den Shake ("wa-wa-wosh"),
>    falls weiterhin gewünscht.
> 3. Klären, ob ein separates Followspot-Joystick-Profil gewünscht ist.
> 4. Genauere Beschreibung/Screenshot für den MAX-Slider-Layout-Bug.
> 5. `firmware/`-Ordner neu aufbauen für den One-Click-Installer.
> 6. Danach frei: zurückgestellte Restrukturierungen, Preset-Engine-Split,
>    ADS1115-Hardware-Joystick, `jogBend` fertigbauen oder entfernen,
>    übrige Tech-Debt-Punkte.

## Aktueller Status

Achtzehn-plus Review-/Test-Runden durch (Details siehe `history.md`),
inklusive eines offiziellen Herstellerdatenblatts als Referenz
(`mapping_sheds_160w_3in1_gobo.md`) und einer erfolgreichen, mehrstufigen
interaktiven Hardware-Kalibrierung für den Gobo-Shake — von „Idee
verworfen" über „User korrigiert die Testtechnik" zu „funktioniert live
bestätigt" zu „nach echtem UI-Test nachgeschärft" zu „Start/Stop-Race und
Kanal-Clobber für alle FX-Typen geprüft und gefixt" zu „Dimmer-Speed-
Einheit, Beat-Sync-Default und HW-Mic-Programmer-Button root-caused und
gefixt" in derselben fortlaufenden Session. Zielhardware: **ESP32-C3
Supermini** (Fixture: SHEHDS 160W 3in1 GOBO / „Pro Beam 280"). Repository
ist sowohl lokal als auch auf GitHub (`future`-Branch) git-versioniert
und läuft auf echter Hardware, aktuell erreichbar unter der festen
LAN-IP `192.168.8.113` (mDNS `movinghead.local` bleibt bekannt-flakey).

## Was in dieser Session (Fortsetzung, 2026-08-15 bis 18) gemacht wurde

1–18: siehe vorige Handoff-Snapshots / `history.md`.
19. Drei neue Meldungen direkt im Anschluss an Punkt 18: Dimmer-"Manual
    speed" sollte ms sein (war ein inverser Kehrwert) — gefixt für
    `dimFX`/`gRotFX`/`pRotFX` (gemeinsame `Modulator`-Klasse). Beat-Sync
    fühlte sich "aus statt an" an — Default-Modus war Forward+Sine
    (dunkel exakt auf dem Beat), auf Reverse+Sine geändert (hell auf dem
    Beat, Abfall danach). HW-Mic-Button im Programmer-Tab tat nichts —
    war an tote, nie ans Backend angebundene Felder gebunden statt an
    den echten Live-Tab-Zustand. Alle drei kompiliert, geflasht,
    Backend-Verhalten live per curl bestätigt. Browser-/Hardware-seitige
    Bestätigung steht noch aus.

Details zu allem: `history.md` (mehrere Einträge vom 2026-08-15 bis 18).

## Doku-Hinweis

`handover.md` + `backlog.md` sind lebende Dokumente (überschreiben/ergänzen).
Dieser Handoff ist der **einzige aktuelle Snapshot** — beim nächsten Mal
ersetzen (Banner + Status aktualisieren), nicht anhäufen. `history.md` ist
Append-only — dort landen abgeschlossene Sessions als neuer, chronologischer
Eintrag, ohne bestehende Einträge zu verändern.
`mapping_sheds_160w_3in1_gobo.md` ist die Fixture-DMX-Referenz — kein
Session-Verlauf, bei Bedarf direkt erweitern/korrigieren.
