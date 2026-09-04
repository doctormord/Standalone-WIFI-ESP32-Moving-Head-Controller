# Ausgebaute Verfahren — konzeptionelle Sicherung

Diese Datei hält fest, **was** einmal im Code stand, **warum** es dort stand und **warum es
wieder herausgeflogen ist**. Sie ist kein Backlog: hier steht nichts, was gebaut werden soll.
Sie existiert, damit ein ausgebautes Verfahren nicht in einem halben Jahr neu erfunden wird,
weil niemand mehr weiß, dass es schon einmal gemessen wurde.

Regel für Ergänzungen: ein Abschnitt pro Verfahren, immer mit dem **Messergebnis**, das zur
Entscheidung geführt hat. Ein Verfahren ohne Messung gehört nicht hierher, sondern ins Backlog.

---

## 1. Autokorrelation über den Flux-Strom (ausgebaut 2026-09-02)

**Idee.** Der Intervall-Median scheitert strukturell an synkopiertem Material: er nimmt den
mittleren Abstand zwischen erkannten Kicks, und bei Hip-Hop sind diese Abstände ¾, ½, 1¼ Beats
— deren Median ist nicht der Beat. Die Autokorrelation stellt stattdessen die andere Frage:
*welche Periode erklärt alle Onsets am besten?* Dazu wird der Onset-Strom in 10-ms-Bins
gerastert, geglättet ([1 2 1]), autokorreliert, und über vier Harmonische summiert
(Gewichte 1,0 / 0,5 / 0,34 / 0,25), damit eine Periode auch dann gewinnt, wenn nur jeder
zweite Schlag getroffen wurde. Darüber ein log-gaußscher Tempo-Prior nach Ellis
(Zentrum 120 BPM, σ = 0,9 Oktaven), der die Oktaventscheidung trifft.

**Was gemessen wurde.** Im Simulator über 5 Muster × 6 Tempi, identischer Onset-Strom:

    Intervall-Median (nur Bass)                31 %
    Autokorrelation + Harmonische (nur Bass)   15 %
      + Tempo-Prior                            25 %
      + alle drei Bänder                       81 %
      + Tap-Anker                             100 %

**Warum trotzdem ausgebaut.** Am echten Hip-Hop-Track erreichte „alle Bänder + Prior" **7 %**.
Der Prior bei 120 BPM zieht dort aktiv zur falschen Antwort. Erst mit Anker stimmte es (51 %
nur Bass → 91 % alle Bänder) — und *mit* Anker löst das vorhandene Ratio-Folding denselben Fall
zum Nulltarif. Der einzige Mehrwert der Autokorrelation wäre also Betrieb **ohne** Tap gewesen,
und genau ohne Tap versagt sie auf echtem Material. Dazu ~180k MAC pro Auswertung auf einem
Chip ohne FPU.

**Falls es je zurückkommt:** vorher `sim/ --beats` gegen echte annotierte Tracks laufen lassen,
nicht gegen den synthetischen. Der Simulator sagte 81 %, das Gerät 7 % — das ist die ganze
Lehre.

## 2. Breitband-Hüllkurvenpfad (`/audio_tune?fft=0`, ausgebaut 2026-09-02)

**Idee.** Drei Hüllkurvenfolger (schnell/mittel/langsam) über den Betrag der Rohsamples, ohne
FFT. „Bass" war die langsame Hüllkurve, „Mid" und „High" die Differenzen zwischen den
Geschwindigkeiten. Billig und ohne Frequenzanalyse.

**Warum ausgebaut.** Es trennt nicht nach **Frequenz**, sondern danach, **wie schnell der
Gesamtpegel steigt** — eine Hi-Hat, eine Snare und ein Klick sind darin ununterscheidbar, und
„Bass" ist nur der geglättete Gesamtpegel. Diese Konstruktion war der Grund, warum `midEnergy`
strukturell nahe 0 lag. Die Mid/High-Schwellen wurden zudem aus der **Bass**-Schwelle
heruntergeschoben, weshalb ein bassbetonter Track die High-Schwelle über alles hob, was das
High-Band je produzierte — „Strobe auf Hi-Hat" funktionierte deshalb nie.

**Nebenwirkung, die den Ausbau erzwang.** Der Pfad hinterließ `sdOnsetMs` eingefroren und
fütterte den Tempo-Schätzer mit einem konstanten Zeitstempel; außerdem lagen Pegelanzeige und
Auto-Gain im FFT-Zweig und froren unter `fft=0` mit ein. Ein Notausgang, der beim Betreten
stillschweigend drei andere Dinge kaputtmacht, ist kein Notausgang.

**Was an seine Stelle trat.** Echte FFT-Bänder, und pro Band wählbar, ob der Detektor auf
Pegel (`energy`) oder auf Anstieg (`flux`) reagiert — `/audio_tune?db=&dm=&dh=`. Das ist das,
was der alte globale `flux`-Schalter gemeint hat, nur richtig aufgelöst: Bass liest sich besser
über Pegel, Mid und High über den Anstieg.

## 3. Rahmenbasierte Bass-Erkennung (`/audio_tune?bsd=0`, ausgebaut 2026-09-02)

**Idee.** Beat, wenn die Bandenergie eines 32-ms-Rahmens über einer geglätteten Schwelle liegt.

**Warum ausgebaut.** Ein Zeitstempel mit 32 ms Auflösung ist für einen **Trigger** brauchbar,
für **Tempo** nicht: bei 128 BPM sind 32 ms bereits 7 % eines Beats, und der Schätzer arbeitet
auf genau diesen Abständen. Der abtastratengenaue Detektor (die DJM-artige zeitkontinuierliche
Kette) stempelt auf der Audio-Uhr und ist der Grund, warum die Tempoerkennung überhaupt
funktioniert.

**Was bewusst geblieben ist.** Für **Mid und High** existiert der rahmenbasierte Vergleich
weiter — er ist der Rückfall unter `/audio_tune?sab=0`, dem Notausgang, mit dem sich messen
lässt, was die drei Detektorbänder wirklich kosten. Bass benutzt ihn nie.

## 4. Doppelte Tempo-Schalter (`trk`, `tap`, ausgebaut 2026-09-02)

`audioUseTracker` und `tempoAuto` waren zwei Schalter für dieselbe Sache — beide mussten wahr
sein, damit Audio das Tempo setzt. `tempoTapLock` war zusätzlich eine gespiegelte Kopie von
`!tempoAuto`. Die Oberfläche bot daraus ein Dreifach-Menü mit den Optionen „auto tracker" und
„interval median", die zwei Schätzer benannten, von denen es nur noch einen gibt.

Übrig ist **ein** Flag, `tempoAuto`. Beide API-Schreibweisen bleiben bestehen, weil sie von
verschiedenen Stellen der Oberfläche kommen: `?tap=1` heißt „halte das Getappte", `?auto=1`
heißt dasselbe umgekehrt.

---

## 5. Psychoakustisches Front-End (gebaut und vermessen 2026-09-02, nicht portiert)

Quelle: der audioXpress-Überblick zur Beat-Erkennung, dazu Bello et al. [1] für die
Onset-Grundlagen und Böck & Widmer (SuperFlux, DAFx-13) für den Maximum-Filter.

**Zuerst die Absage.** Von den sechs genannten Quellen sind [2], [3], [5] und [6] neuronale
Netze — BLSTM, TCN, Transformer — und [4] setzt ein dynamisches Bayes-Netz auf ein RNN. Das ist
der Stand der Technik und auf einem ESP32-C3 ohne FPU mit ~78 KB freiem Flash nicht erreichbar.
Was erreichbar ist, ist genau das, was diese Arbeiten als **Eingangsdarstellung** benutzen — und
dort steckt die eigentliche Wahrnehmungsmodellierung.

### Was gebaut wurde

Drei Stufen, in Ganzzahlarithmetik auf der **FFT der Firmware**, jede einzeln abschaltbar:

1. **Kritische Bänder (Bark).** 22 Bänder von 20 Hz bis 8 kHz statt linearer FFT-Bins. Gehör
   löst Frequenz nicht linear auf; ein Kick konkurriert mit der Energie in *seinem* Band, nicht
   mit dem ganzen Spektrum. Das ist der Grund, warum ein Mensch den Kick unter einer lauten
   Fläche noch hört.
2. **Logarithmische Kompression.** Lautheit ist etwa logarithmisch. Differenziert man rohe
   Beträge, ist derselbe Onset in einer lauten Passage numerisch riesig und in einer leisen
   unsichtbar; differenziert man Logarithmen, sind sie gleich.
3. **SuperFlux-Maximumfilter.** Vibrato und gleitende Partialtöne schieben Energie zwischen
   Nachbarbändern und erzeugen so falsche Onsets. Der Vergleich gegen das Maximum der
   Nachbarbänder des Vorframes lässt einen Partialton wandern, ohne dass es zählt.

Dazu der Standard-Peak-Picker aus [1]: lokales Maximum über ±3 Frames, über
Mittelwert + δ·MAD eines nachlaufenden Fensters, plus Refraktärzeit.

### Was gemessen wurde

Gegen die annotierte 124-BPM-Datei (Kicks auf den Beats, Hi-Hats auf den Off-Beats), Wahrheit
aus `--beats`:

    Variante          Onsets  pro s   Prec  Recall     F    Versatz
    Firmware              91   2.02   100%    98%   0.989   +10.7 ms
    Bark, linear         158   3.51    58%    99%   0.733   +14.2 ms
    + log                180   4.00    49%    96%   0.652    +2.1 ms
    + SuperFlux          179   3.98    50%    96%   0.654    +2.1 ms
    SuperFlux nur Bass   177   3.93    52%    99%   0.681    +4.1 ms

**Drei Befunde, in absteigender Belastbarkeit:**

**(a) Die logarithmische Kompression halbiert den systematischen Zeitversatz** — von +14,2 ms
auf **+2,1 ms**, und sie schlägt damit auch die Firmware (+10,7 ms) um 8,6 ms. Bei 124 BPM sind
das 1,8 % eines Beats. Das ist die einzige Zahl hier, die sauber, vergleichbar und gegen echte
Ground Truth gemessen ist. Wenn je ein spektraler ODF kommt, dann mit log.

**(b) Das Front-End hört alles (Recall 96–99 %) und hört mehr als den Beat (Precision ~50 %).**
Die zusätzlichen Onsets sind keine Fehler — es sind die Hi-Hats, die wirklich da sind. Ein
Breitband-Onsetdetektor macht genau das, was er soll.

**(c) Und deshalb ist er mit dem Abstands-Median unvereinbar.** Der Median unterstellt, jeder
Abstand *sei* der Beat — das gilt nur für einen absichtlich schmalbandigen Detektor. Auf
derselben Datei:

    Tempo, Wahrheit 124 BPM        Median   ACF+H+Prior
    Firmware                          124       124
    Bark, linear                      125       125
    + log                               0       125
    + SuperFlux                         0       125
    SuperFlux nur Bass                  0       125

Der Median liefert bei drei Varianten **gar keine Antwort** (das Übereinstimmungs-Gate lehnt ab),
die Autokorrelation bei allen die richtige. Ein psychoakustisches Front-End ist also **kein
Austauschteil, sondern ein anderer Pipeline-Zweig**: Breitband-ODF *verlangt* Autokorrelation
oder Kammfilter — genau das, was in Abschnitt 1 mangels Tap-freier Leistung am echten Material
ausgebaut wurde.

**Der SuperFlux-Maximumfilter änderte hier nichts Messbares.** Das ist kein Gegenbeweis: er
zielt auf Vibrato, und weder das synthetische Material noch die Testdatei enthalten welches.
Ungeprüft gegen das, wofür er gemacht ist.

### Und die Zahl, der ich nicht traue

Über 5 synkopierte Muster × 6 Tempi meldet der Simulator: Firmware 20 %, Breitband-ODF +
Autokorrelation **57 %**. Das sieht nach einer Verdreifachung aus. **Es ist kein Ergebnis.**
Derselbe Simulator meldete am Vortag 81 % für eine Kette, die am Gerät 7 % erreichte. Die Zahl
ist eine Hypothese, bis sie an echter annotierter Musik gemessen ist — was mit `--beats` jetzt
möglich ist und vorher nicht war.

### Kosten, falls es je portiert wird

Der Rechenaufwand über die FFT hinaus ist vernachlässigbar: 256 Additionen für die Bandsummen,
22 Ganzzahl-`log2` (je ~6 Instruktionen), 22×3 Maxima — zusammen etwa 500 Ganzzahloperationen je
Frame, 31 Frames/s. Der reale Preis ist ein anderer: **die FFT müsste dauerhaft laufen** statt
wie heute nur auf Anforderung. Diese Kosten sind nicht zu schätzen, sondern abzulesen — `fftUs`
liegt bereits auf `/api/audio_debug`.

### Werkzeuge, die dabei entstanden sind

    ./simbeat --mode psy                    # Ablation über alle Muster und Tempi
    ./simbeat --mode psyscore --file X.wav --beats X.txt   # Onset-Güte gegen Ground Truth

---

## 6. Validierungssammlung und die Bass-Dosiskurve (2026-09-03)

Der Simulator hatte bis hierher genau einen Track: ein Klickmuster. Gut genug, um Fehler im
Detektor zu finden, unbrauchbar für die Frage, welches Verfahren gewinnt. `sim/mktracks.py`
erzeugt jetzt sieben synthetisierte Tracks mit exakten Beat-Annotationen — bewusst weg von
120 BPM und mit dem, was Onsetdetektoren wirklich bricht:

    boombap-86      synkopierte Kicks neben dem Puls, geswingte Hats, liegender Bass
    trap-70         Halftime, spaerliche Kicks, 808-Sub mit langem Ausklang, 1/16-Hatrolls
    strings-92      Streicher MIT Vibrato ueber leisem Kick -- wofuer SuperFlux gebaut ist
    breakdown-132   Drums, dann 8 Takte nur Flaeche OHNE Beat, dann wieder Drums
    house-145       Four-on-the-floor, Offbeat-Openhat und Offbeat-Bass
    techno-150      Four-on-the-floor unter lautem liegendem Bass
    dnb-174         Two-step, Kicks auf 1 und dem "und" der 3

Die Wahrheit daneben ist das **Beat-Raster** — was ein Mensch tappt — und ausdruecklich nicht
die Kickpositionen. Auf synkopiertem Material sind das verschiedene Dinge, und sie zu
verwechseln ist genau die Art, wie ein Detektor fuer einen Puls gelobt wird, den niemand hoert.

### Der Befund: liegender Bass zerlegt die Erkennung des Geraets

Kontrolliertes Experiment, identischer Track, **eine** Variable — der Pegel des liegenden Basses
unter einem unveraenderten Four-on-the-floor. Recall gegen 120 echte Beats:

    Bass-Gain      0,00    0,50    0,90    1,25
    Firmware        92 %    47 %    23 %    11 %
    log-Flux (voll) 93 %    93 %    93 %    93 %
    log-Flux (Bass) 82 %    70 %    59 %    58 %

Das ist keine Streuung, das ist eine monotone Dosis-Wirkungs-Kurve. **Der Detektor des Geraets
verliert neun von zehn Kicks, sobald ein Sub-Bass in derselben Lautstaerke durchlaeuft.**

Der Mechanismus ist einfach und steht so in der Literatur: der vorhandene Detektor vergleicht
einen **Pegel** gegen einen nachgefuehrten Boden. Ein liegender Bass hebt den Boden in Richtung
Spitze, der Bereich dazwischen kollabiert, und die Schwelle sitzt dann knapp unter der Spitze.
Ein **Flux** ist eine Ableitung: ein konstanter Anteil traegt exakt null bei, egal wie laut er
ist. Deshalb ist die log-Flux-Kurve flach.

### Tuning holt das nicht ein

Auf dem haertesten Fall (Bass 1,25), Ausgangswert F 0,148:

    brl = 2 (schnelleres Release)     F 0,308   Recall 35 %
    mrp = 20 (Bereichs-Gate)          F 0,328   Recall 27 %
    vmp >= 20                         F 0,000   -- die Erkennung geht komplett aus
    beliebige Kombination             F 0,308   Recall 35 %
    brf (Referenz-Zeitkonstante)      ohne jede Wirkung

Die Decke liegt bei 35 % Recall gegen 93 % des Flux-Verfahrens. **Der Fehler ist strukturell,
nicht eingestellt.** Nebenbefund: `vmp` ab 20 schaltet die Erkennung auf diesem Material
vollstaendig ab — der Varianz-Gate ist auf dichtem Material gefaehrlich scharf.

### SuperFlux tut, wofuer es gebaut ist — auf dem richtigen Material

Auf `strings-92` (Vibrato), was am 2026-09-02 mangels passenden Materials nicht messbar war:

    + log            96 Onsets   Prec 64 %   Recall 64 %   F 0,635
    + Maximumfilter  77 Onsets   Prec 77 %   Recall 61 %   F 0,682

20 % weniger Onsets, 13 Punkte mehr Precision, 3 Punkte weniger Recall. Richtung und
Groessenordnung decken sich mit Boeck & Widmer. Der Filter arbeitet, er hatte vorher nur nichts
zu tun.

### Und die Ernuechterung: Tempo findet keiner zuverlaessig

Ueber alle sieben Tracks, gemeldetes Tempo innerhalb von 4 % der Wahrheit:

    Firmware, Abstands-Median      2 von 7
    Firmware, Autokorrelation      3 von 7
    psychoakustisch, Median        3 von 7
    psychoakustisch, ACF+Prior     4 von 7

Oktavfehler bei dnb-174 (87 statt 174) und techno-150 (75 statt 150) treffen jedes Verfahren.
**Kein Verfahren in dieser Untersuchung findet den Beat zuverlaessig ohne Tap.** Was das
psychoakustische Front-End kauft, ist Robustheit der *Erkennung*, nicht Richtigkeit des Tempos —
und das ist trotzdem viel wert, weil der Phasenregler des Geraets von jedem korrekt
gestempelten Onset lebt.

### Ein zweiter, unabhaengiger Befund: Phase auf dem Offbeat

Auf `house-145` meldet die Firmware das Tempo richtig (145) und rastet trotzdem **eine halbe
Beat-Laenge daneben** ein: konstanter Versatz -187,9 ms bei 414 ms Beat, mit 98 % Precision. Sie
findet also sauber und regelmaessig — nur den Offbeat, auf dem in diesem Track Openhat und
Bassnote liegen. Fuer Licht ist das der unangenehmste Fehler ueberhaupt: das Tempo stimmt, und
alles blitzt zwischen den Schlaegen.
