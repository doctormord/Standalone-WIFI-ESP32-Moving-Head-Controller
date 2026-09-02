# NEXT CHAT STARTS HERE

Stand 2026-09-02, nach dem Test am Fixture. Branch `future`, alles gepusht, Flash 94,1 %.

## Wo wir stehen

Die Beat-Erkennung ist umgebaut und **auf echter Hardware getestet**. Was der Prüfplan
bestätigt hat: Uhrenabweichung 0, Kadenz-Umbau real 9,0 % → 4,0 % der Wanduhr, bedarfsgesteuerte
FFT (`fftUs = 0` bei geschlossenem AUDIO-Tab), Erkennung statt Oszillation, Bandtrennung 2,3×
und 2,79× auf Material mit Hi-Hats. Die Erkennung selbst läuft laut User „bei House erstmal gut".

Nebenbei gefunden und behoben: Joystick-Runaway bis zum Anschlag bei verlorenem Paket,
Pan/Tilt-Geschwindigkeit gemessen (330/165 °/s, in der Fixture-Doku), CH5 als zweite unbekannte
Bremse (Standard jetzt 0), toter SYNC-Knopf im PROGRAMMER-Tab, fehlende Tastenquittung,
Auto-Gain-Verklemmung bei 0, Tempo-Reset nach 2,5 s Stille.

Tempo-Bedienung neu: **Tap ankert, sperrt nicht.** Modus Auto/Manuell am TAP-Knopf (kurz =
tappen, lang = umschalten, leuchtet im Auto-Modus), persistiert, startet auf Auto.

## Der nächste inhaltliche Schritt: Periodensucher im Simulator

**Das ist die einzige verbliebene strukturelle Schwäche.** Der Tempo-Schätzer ist der Median der
Abstände zwischen aufeinanderfolgenden Kicks. Das setzt voraus, dass die Kicks auf einem
gleichmäßigen Raster liegen — bei Four-on-the-Floor stimmt das (Techno 156 ✓, House 119 ✓), bei
synkopiertem Material nicht. Am Gerät gemessen: Hip-Hop mit 98 BPM (Beat 612 ms) rastet auf
454 ms ein, und das ist auf 1 % genau **¾ Beat**. Die Onsets sind dabei in Ordnung, sie liegen
nur nicht auf dem Raster.

Kein Parameter behebt das. Ein Sweep über `agr` wählt nur aus, **welche** falsche Periode stabil
herauskommt (77 bei einem Wert, 128 bei einem anderen — beides reale Periodizitäten, keine davon
der Beat).

**Nötig ist ein Verfahren, das die Periode sucht, die *alle* Onsets erklärt** — Autokorrelation
oder Kammfilter über den Onset-Strom — statt aufeinanderfolgende Abstände zu mitteln. Genau so
ein Verfahren (der Phasen-DFT) lag im Code und wurde am 2026-09-02 als toter Code entfernt. Das
war richtig: er lief auf den unbrauchbaren Onsets des damals defekten Komparators. Mit den jetzt
sauberen Onsets ist er das passende Werkzeug.

**Vorgehen — und das ist nicht verhandelbar:** Entwicklung in `sim/`, nicht am Gerät. Der
Simulator braucht dafür zuerst ein **synkopiertes Kick-Muster** als Testfall (Kicks auf 1, dem
„und" der Zwei, vor der Vier — Abstände ¾, ½, 1¼ Beats), damit es überhaupt exakte Wahrheit für
den Fall gibt, der scheitert. Dann Verfahren gegeneinander messen. Am Gerät wird verifiziert,
nicht gesucht.

    cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp
    ./simbeat --mode tempo --sens 100      # 90..174 BPM, Vier-Viertel
    ./simbeat --mode bands --file X.wav    # echte Datei, drei Bänder
    LC_ALL=C setzen, wenn die Ausgabe mit awk geparst wird

## Regeln, die aus dieser Session kommen

- **Parametersuche gehört in `sim/`.** An einem laufenden Track wurden an einem Tag dreimal
  Werte eingestellt, die sich alle als trackspezifisch erwiesen.
- **Wahrheit wird zum Zeitpunkt der Messung erhoben, nicht erinnert.** Zweimal wurde gegen ein
  Tempo aus einer früheren Nachricht verglichen, das nicht mehr galt. Der Tap-Anker ist das
  Instrument dafür: der User tappt, `tAnchor` gegen `tBPM` liefert Wahrheit und Messung im selben
  Moment.
- **Jede Tempomessung führt Pegel und Onset-Rate mit.** Ohne sie ist nicht unterscheidbar, ob ein
  stabiler Wert Qualität oder Stille bedeutet — genau das ist einmal passiert.
- **Dateigröße ist kein Flash-Beweis.** Ein fehlgeschlagener OTA (Portkonflikt) hinterließ eine
  veränderte Größe. Erst die Suche nach einem konkreten neuen Bezeichner im ausgelieferten HTML
  entscheidet.

## Offene Punkte

Siehe `backlog.md` → „Offen aus der Audio-Überarbeitung". Kurz: Jog-Rad (bewusst aufgeschoben,
weil erst zu klären ist, worauf es wirken soll), kurzes Clipping trotz Auto-Gain,
Preset-Engine-Split als eigentlich anstehende Funktion.
