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

## Testplan 2026-09-02 (geführt, am Fixture)

Aufteilung: **du machst das Physische und beurteilst das Sichtbare, ich messe über die API.**
Rund 45 Minuten. Abbruchkriterien stehen jeweils dabei — wenn eines greift, wird repariert und
nicht weitergetestet.

**Vorbereitung (du):** Fixture an DMX und Strom, frei sichtbar. Gerät per USB dran (zum Flashen)
und im WLAN. Musik bereit: zuerst **ein Track, dessen Tempo du kennst** (sag es mir erst nach
meiner Messung), später zwei **fremde** Tracks anderer Machart. Dann Bescheid sagen.

### A — Grundfunktion, ohne Musik
| | du | ich |
|---|---|---|
| A1 | bestätigst, dass die Web-UI lädt | flashe Firmware + Dateisystem |
| A2 | — | prüfe `drift`. **Abbruch, wenn nicht ~0** — dann geht die Sample-Uhr nach und jede Tempomessung fällt zu hoch aus, alles Weitere wäre wertlos |
| A3 | fährst den Kopf mit dem Joystick | frage dich: fühlt sich die Reaktion unverändert an? (Regression des Kadenz-Umbaus) |
| A4 | beurteilst die Bewegung mit einem Movement FX (z. B. Circle) | frage dich: flüssig oder ruckelt es? Die Ausgabe läuft jetzt mit 33 Hz statt ~420 |

**Abbruch bei A3/A4**, wenn Bewegung stockt oder der Joystick träge wirkt → `/api/asmmode?every=1`
stellt das alte Verhalten sofort wieder her, dann ist der Umbau falsch abgegrenzt.

### B — Was der Kadenz-Umbau wirklich bringt (ohne Musik, Movement FX an)
Du musst nur das FX laufen lassen. Ich messe `engUs`/`engMax`/`lps` im neuen Modus, schalte per
`/api/asmmode?every=1` auf das alte Verhalten, messe erneut, und schalte zurück. Damit steht die
reale Ersparnis statt meiner Herleitung (erwartet: `engUs` fällt im Mittel deutlich, `lps` steigt).

### C — Audio, mit Musik
| | du | ich |
|---|---|---|
| C1 | startest Musik, Mikrofon in üblicher Position | schalte Audio ein, setze die Startwerte |
| C2 | drehst einmal deutlich lauter, dann wieder leiser | beobachte `pk`/`clip`/`rclip` und ob der Auto-Gain nachzieht |
| C3 | — | messe, ob er **erkennt oder oszilliert**: Sperre steht auf 60 ms; liegt der Onset-Median bei ~100 ms, ist es ein freilaufender Oszillator |
| C4 | nennst mir jetzt das echte Tempo | habe vorher meine Messung genannt |

### D — Die neuen Trigger (der eigentliche Zweck)
Ich setze Movement FX auf `bass` und Dimmer FX auf `high`. **Du schaust hin und beschreibst:**
fährt der Kopf auf dem Kick? Blitzt der Dimmer *zwischen* den Kicks auf den Hi-Hats, oder blitzt
alles gleichzeitig? Das kann nur das Auge beurteilen — ich messe parallel die Raten der drei
Bänder (erwartet: Bass ~halb so oft wie High).

### E — Die Härteprüfung
Zwei **fremde** Tracks, anderes Genre und Tempo. **Es wird nichts nachgestellt.** Ich messe BPM
und Bandraten, du beurteilst die Optik. Muss ich nachjustieren, ist die skalenfreie Schwelle
gescheitert und das gehört so ins Protokoll.

### F — Last
Alles gleichzeitig: Movement + Dimmer + Audio + AUDIO-Tab im Browser offen. Ich messe `loopMax`,
`audUs`, `fftUs`, `lps`. Dann schließt du den Tab und ich messe erneut — **`fftUs` muss auf 0
fallen**, sonst greift die Bedarfssteuerung nicht.

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
