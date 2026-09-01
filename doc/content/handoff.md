# NEXT CHAT STARTS HERE

Stand 2026-09-01, Ende. Branch `worktree-audio-panel` im Worktree `audio-panel`,
7 Commits noch nicht nach `future` gepusht.

## Wo wir stehen

Der Beat-Detektor wurde vollständig umgebaut. Vorher lagen fünf Tempo-Schätzer übereinander,
von denen zwei unabhängig `globalBPM` schrieben; darunter ein Komparator, dessen rollende
Referenz wegen einer Integer-Totzone **nie funktioniert hat**. Alle Einmessungen der Tage davor
waren dadurch bedeutungslos.

Jetzt: **eine** Kette. Bass 40–160 Hz → Hüllkurve (Release ~128 ms) → Schwelle im
Dynamikbereich gegen einen **Median** → Varianz-Gate → Onset **am Gipfel** → Intervall-Median
über ein Zeitfenster → Konsistenzprüfung → 60–200 BPM.

**Alles ab Commit `b6bcc92` ist auf Hardware unverifiziert.** Verifiziert ist nur der Stand
davor: Onset-Median 456 ms = 131,5 BPM gegen wahre 133, ohne einen einzigen Abstand unter
200 ms bei 120 ms Sperre.

## Zuerst flashen

Gerät hat alte Werte in NVS, die die neuen Standardwerte überschreiben. Nach dem Flashen setzen:

    /hwaudio?en=1&sens=60
    /audio_tune?ig=3&blk=60&blo=6&bhi=4&brl=11&tw=6000&vmp=25&pfp=70&pmw=60&agr=20

`hwAudioEnabled` steht nach einem Reboot auf 0 — Audio muss explizit eingeschaltet werden.

## Vor dem Gerät: der Simulator

`sim/` fährt den echten Detektorcode nativ gegen synthetische Musik mit **exakt bekannter**
Beat-Position. Bauen und laufen lassen:

    cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp
    ./simbeat --mode tempo --sens 60        # 90..174 BPM
    ./simbeat --mode level --auto           # 50-facher Pegelbereich

Aktueller Stand dort: F = 0,988 bei 130 BPM, Treffergenauigkeit 100 %, Zeitfehler 6,9 ms,
Tempo über 90–174 BPM auf 2 BPM genau, null verworfene Samples, null Uhrenabweichung.
Der Simulator hat drei echte Fehler gefunden, die sonst erst am Gerät aufgefallen wären —
darunter eine dauerhafte Stummschaltung nach dem ersten Onset.

**Er ersetzt die Hardware nicht.** Kein Mikrofon, kein Raum, keine PA-Kompression, und die
Kick/Bass-Balance des Kunsttracks ist erfunden. Ein Ergebnis dort ist notwendig, nicht
hinreichend.

## Prüfplan, in dieser Reihenfolge

**0. Zuerst die Uhr prüfen — das ist der wichtigste Test.** `drift` auf `/api/audio_debug` ist
die Abweichung der Sample-Uhr von der Wanduhr in Promille. Sie **muss bei etwa null liegen**.
Steht dort ein zweistelliger positiver Wert, verliert die Abholung immer noch Audio, die
Sample-Uhr geht nach, und **jedes gemessene Tempo fällt zu hoch aus** — genau der Fehler, der
diese Session über für einen Schätzerfehler gehalten wurde. Alle folgenden Schritte sind
wertlos, solange dieser Wert nicht stimmt.

**Vorher: feste Referenzaufnahme besorgen.** Loop oder Metronom bei bekanntem Tempo, mehrere
Minuten konstant. Ohne das ist Schritt 3 sinnlos — dreimal in einer Session daran gescheitert,
dass der Track währenddessen weiterlief. Siehe `history.md` Einträge 5 und 6.

1. **Erkennt er überhaupt?** Sperre steht auf 60 ms. Liegt der Onset-Median bei ~100 ms, ist es
   wieder ein freilaufender Oszillator. Liegt er beim Beat, erkennt er. Das ist der einzige
   Test, der zwischen den beiden unterscheidet.
2. **Gipfel statt Flanke** — der Kern dieses Umbaus. Onset-Streuung gegen den Stand von
   Commit `6abd2d8` vergleichen. Sie muss sinken; tut sie das nicht, hat das Peak-Tracking
   keinen Effekt und `pfp`/`pmw` sind falsch dimensioniert.
3. **Der eine verbliebene Regler**: `sens` positioniert die Schwelle im Dynamikbereich
   (100 = 30 % hoch, 0 = 90 %). Gegen die Referenzaufnahme einmessen, **einmal**.
   **Vorher Auto-Gain abschalten** (`/audio_tune?ag=0`, oder im Dropdown eine feste Stufe
   wählen — ein explizites `ig=` schaltet Auto ohnehin ab). Sonst verstellt die automatische
   Bereichswahl während der Messreihe den Pegel und die Reihe misst zwei Dinge gleichzeitig.
4. **Gegenprobe an zwei fremden Tracks.** Der Sinn der ganzen Übung ist, dass Schritt 3 danach
   nicht wiederholt werden muss. Tut er es doch, ist die Schwelle immer noch nicht skalenfrei.

5. **Auto-Gain im Betrieb**: laut aufdrehen bis es clippt — die Kopfzeile muss rot werden und
   der Pegel innerhalb von ~1 s von selbst zurückgehen. Dann leise drehen: hoch geht es
   absichtlich erst nach ~20 s, damit ein Breakdown die Verstärkung nicht aufreißt.

Neue Diagnosefelder auf `/api/audio_debug`: `drift`, `sdFloor`, `sdPeak`, `sdMad`, `sdTrans`,
`agree`, `agrMax`, `pfp`, `pmw`, `vmp`, `ag`, `agPk`, `tw`.

`/api/spectrum` gibt es nicht mehr — das Spektrum kommt jetzt über
`/api/audio_debug?spec=1` in derselben Antwort. Messskripte, die nur Bandwerte brauchen,
lassen `spec=1` weg und zahlen nichts für die 256 Bins.

## Danach

Preset-Engine-Split (Layer) — die eigentlich anstehende Funktion, braucht kein neues
NVS-Format. Siehe `backlog.md`.
