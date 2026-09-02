# NEXT CHAT STARTS HERE

Stand 2026-09-02. Branch `future`.

## Wo wir stehen

Die Beat-Erkennung wurde von Grund auf ersetzt. Vorher lagen fünf Tempo-Schätzer übereinander,
von denen zwei unabhängig `globalBPM` schrieben; darunter ein Komparator, dessen rollende Referenz
wegen einer Integer-Totzone **nie funktioniert hat**. Sämtliche Einmessungen davor waren dadurch
bedeutungslos.

Jetzt: **eine** Kette, dreimal instanziiert. Pro Band Bandpass → Hüllkurve → Komparator gegen eine
Schwelle *im Dynamikbereich* über einem **Median** → Varianz-Gate → Onset **am Gipfel** →
Intervall-Median → Konsistenzprüfung, Bereich 60–200 BPM. Bass 40–159 Hz, Mid 159–637 Hz, High
über 1273 Hz, jedes mit eigener Erholzeit. Architekturbeschreibung in `handover.md`, Parameter in
`functions.md`.

**Im Simulator verifiziert, auf Hardware nicht.** Alles ab Commit `b6bcc92` hat nie auf dem Gerät
gelaufen.

## Zuerst flashen und setzen

NVS hält alte Werte, die die neuen Standardwerte überschreiben. Nach dem Flashen:

    /hwaudio?en=1&sens=60
    /audio_tune?ig=3&ag=1&blk=60&brl=8&brf=14&tw=6000&vmp=10&mrp=25&pfp=70&pmw=60&agr=20&sab=1

`hwAudioEnabled` steht nach jedem Reboot auf 0 — Audio muss explizit eingeschaltet werden.

## Prüfplan, in dieser Reihenfolge

**0. Die Uhr — wichtigster Test.** `drift` auf `/api/audio_debug` ist die Abweichung der
Sample-Uhr von der Wanduhr in Promille und **muss bei etwa null liegen**. Steht dort ein
zweistelliger positiver Wert, verliert die Abholung Audio, die Sample-Uhr geht nach, und **jedes
gemessene Tempo fällt zu hoch aus** — genau der Fehler, der lange für Schätzerschwäche gehalten
wurde. Alle weiteren Schritte sind wertlos, solange das nicht stimmt.

**1. CPU, zweimal messen.** `audUs`/`fftUs`/`loopMax` auf `/api/state`, **einmal mit geschlossenem
Browser und einmal mit offenem AUDIO-Tab.** Bei geschlossenem Tab muss `fftUs` **0** sein und es
dürfen nur die Bänder laufen, auf die ein FX geroutet ist. Referenz vorher: `audUs` 1200–2400 µs
pro 32-ms-Frame, davon `fftUs` 1127–1207. Bleibt `loopMax` unter ~25 ms, ist alles gut.
Notausgang: `/audio_tune?sab=0`.

**2. Erkennt er, oder oszilliert er?** Die Sperre steht auf 60 ms. Liegt der Onset-Median bei
~100 ms, ist es ein freilaufender Oszillator (dieser Fall hat schon einmal wie ein Erfolg
ausgesehen). Liegt er beim Beat, erkennt er. Das ist der einzige Test, der die beiden trennt.

**3. Die neuen Trigger.** Movement FX auf `bass`, Dimmer FX auf `high`. Der Dimmer muss auf den
Hats blitzen, nicht auf dem Kick. Im Simulator am 145-BPM-Track: Bass 2,35/s auf der Viertel,
High 4,33/s auf der Achtel, nur 1–2 % Überlappung.

**4. Auto-Gain.** Laut aufdrehen bis es clippt — die Kopfzeile wird rot, der Pegel geht innerhalb
~1 s von selbst zurück. Dann leise drehen: hoch geht es absichtlich erst nach ~20 s.

**5. Gegenprobe an zwei fremden Tracks, ohne etwas nachzustellen.** Das ist der eigentliche
Prüfstein — die skalenfreie Schwelle soll genau das leisten. Muss doch nachjustiert werden, ist
sie es nicht.

Für eine Feineinmessung von `sens`: **feste Referenzaufnahme** (Loop oder Metronom, mehrere Minuten
konstant), kein laufendes Set. Daran sind am 2026-09-01 drei Messreihen gescheitert, siehe
`history.md`.

## Werkzeuge

- `cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp` — Detektor ohne Hardware testen,
  gegen synthetische Musik oder eine `.wav`. `--mode tempo|sens|level|bands|trace|csv`.
  `LC_ALL=C` setzen, wenn die Ausgabe mit awk geparst wird. Details in `sim/README.md`.
- `./scripts/check_ui.sh` — prüft `data/index.html` vor dem Hochladen auf Syntaxfehler.
- `pio run` / `pio run -t buildfs` — Firmware- und Dateisystem-Prüfung.

## Danach

Preset-Engine-Split (Layer) — die eigentlich anstehende Funktion, braucht kein neues NVS-Format.
Siehe `backlog.md`. Flash steht bei 93,7 %, also ~83 KB frei; das ist die Decke, gegen die zu
planen ist.
