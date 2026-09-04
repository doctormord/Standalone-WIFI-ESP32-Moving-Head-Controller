# Fixture-Mapping — SHEHDS 160W 3in1 GOBO (Pro Beam 280)

> Extrahiert aus den Original-Herstellerunterlagen am 2026-08-17, weil
> genau diese Zahlen (Gobo-Werte, Shake-Zonen, Prism/Frost-Schwellen) in
> der vorigen Session mehrfach blind geraten werden mussten und laut User
> „eigentlich irgendwo hätten stehen sollen". Ab jetzt stehen sie hier.
>
> **Quelldateien** (User-Downloads, nicht Teil des Repos — bei Bedarf neu
> beschaffen von `shehds.com` / dem Händler):
> - `160W三合一光束灯-LED-说明书.pdf` — offizielles Herstellerhandbuch
>   (Englisch + Chinesisch), enthält die vollständige, autoritative
>   DMX-Kanaltabelle (Abschnitt „19-DMX Channel"). **Primärquelle für alle
>   Zahlen unten.**
> - `SHEHDS_160W3in1GOBO.d4` — Avolites-Fixture-Personality (XML). Bestätigt
>   dieselbe Kanalreihenfolge/-anzahl (18CH-Mode) und liefert zusätzlich
>   die Gobo-1-Shake-Zone (211–255) sowie den „Open"-Wert (130–134) explizit
>   als eigene `<Function>`-Einträge.
>   Company/Fixture-Name laut Datei: `SHEHDS` / `160W3in1GOBO`.
> - `160W gobo.R20` — MagicQ/Chamsys-artiges Personality-Format (Klartext).
>   Bestätigt Kanalreihenfolge, Pan/Tilt-Auflösung (540°/270°) und die
>   Attribut-Buchstaben-Zuordnung (E=Pan, F=Tilt, H=Dimmer, I=Gobo1,
>   J=Gobo2, K=Gobo1_Rotate, M=Gobo2_Rotate, P=Prism, R=Effect_Rotate,
>   S=Frost, L=Focus, Q=Zoom) — nützlich als Kreuzcheck, liefert aber
>   keine feineren Gobo-Einzelwerte als das PDF.
> - `160W gobo.ssl2` — proprietäres/binäres Format (vermutlich Chamsys
>   MagicQ Show-Datei-Export), **nicht auslesbar** (kein Klartext, `strings`
>   liefert nur unstrukturierten Datenmüll — wahrscheinlich komprimiert
>   oder verschlüsselt). Keine zusätzlichen Daten daraus gewonnen.
>
> Wo Quellen sich widersprachen, gilt das PDF-Handbuch als Wahrheit (es ist
> die einzige Quelle mit vollständigen, granularen Gobo-/Shake-Zonen).

## Technische Daten (aus dem Handbuch, Abschnitt 6)

| Feld | Wert |
|---|---|
| Spannung | AC 100–240V, 50–60Hz |
| Leistung | 160W (19×15W RGBW LEDs) |
| Beam-Winkel | linearer Zoom, 10–20° |
| Steuerung | DMX512 / Master-Slave / Auto / Sound |
| Kanäle | **18 CH** (Std.18ch-Modus) |
| Pan/Tilt | 540° / 270° |
| Pan/Tilt-Geschwindigkeit | **~330°/s Pan, ~165°/s Tilt** (gemessen, siehe unten — steht nicht im Handbuch) |
| Dimmer | 0–100%, linear |
| Farbrad | 9 Farben + Open |
| Gobo-1-Rad (statisch, CH7) | 9 Gobos + Open |
| Gobo-2-Rad (rotierend, CH8/9) | 6 Gobos + Open + Rotation |
| Prisma | 6-Facetten, rotierend |
| Effekt | Frost (Wash) |
| Funktionen | RDM, intelligente Temperaturregelung, DMX-Monitor, manuelle Steuerung |

Deckt sich mit unserem `NUM_CHANNELS`/18-Kanal-Profil in
`Moving_Head_Horizon.ino` — **kein struktureller Mismatch**, die
Kanalanzahl und -reihenfolge waren schon immer korrekt.

## Vollständige DMX-Kanaltabelle (offiziell, aus dem Handbuch)

### CH1 — Dimmer
`0–255` linear.

### CH2 — Strobe
`0–255`.

### CH3/CH4 — Pan/Tilt
`0–255` (8-Bit-Grobwert), Pan 540°, Tilt 270°. Feinwerte auf CH15/CH16.

**„Kreis wird zur Acht" bei Movement-FX um den Zenit-Punkt (~DMX-Tilt 32767/16-Bit,
~127/8-Bit) — kein Defekt dieser Einheit, sondern eine bekannte, branchenweite
Eigenschaft jedes 2-Achsen-Pan/Tilt-Movers (live untersucht 2026-08-20, Ursache
recherchiert und Fix implementiert 2026-08-21).** Ein Movement-FX-Kreis, dessen Bahn
durch diesen Winkel läuft, wurde am realen Gerät zu einer sich selbst kreuzenden Acht
statt eines Kreises — bestätigt am projizierten Lichtpunkt (nicht nur am Gehäuse),
unabhängig von Geschwindigkeit (auch sehr langsam getestet) und unabhängig vom
Trigger-Modus. Verschwand vollständig, sobald das Pattern-Zentrum diesen Winkel nicht
mehr kreuzte.

**Root-Cause-Verlauf (vier Hypothesen geprüft):**
1. *Kalibrierung* — Fixture-eigenes „Tilt Calibration" stand auf `-037`; live auf `0` gesetzt und
   erneut getestet, Fehler blieb identisch (und `0` machte zusätzlich die Geradeaus-nach-oben-
   Referenz sichtbar falsch — `-037` war also die echte, gebrauchte Kalibrierung). **Verworfen.**
2. *Nicht-monotone DMX→Winkel-Abbildung* — ursprüngliche Annahme, ein per Software-Fix
   (Pattern-Zentrum automatisch über den fraglichen Punkt hinausschieben) kompensiert wurde.
   Per gezieltem, statischem Kalibrier-Sweep widerlegt: CH4 wurde in Einzelschritten von 90 bis
   165 gesetzt (jeder Wert ca. 2,3 s **gehalten**, keine Bewegung), dabei der `Tilt Codewheel
   Step` im fixture-eigenen „Sensor Monitor"-Menü per Video mitgeschnitten. Ergebnis: **glatt
   monoton über den gesamten Bereich, keine Umkehrung, keine Diskontinuität, keine Anomalie genau
   am vermuteten Punkt** (Rohdaten: DMX 93→Step -12, 108→+1, 120→+11, 127→+19, 128→+20,
   135→+24, 150→+37, 165→+50 — durchgehend linear). **Verworfen; der Software-Fix dafür wurde
   deshalb wieder entfernt** (`FX_Engine.h`, `MovementEngine::getValues()`) — er hatte zusätzlich
   den Nebeneffekt, ungefähr die halbe nutzbare Tilt-Range für JEDES Pattern sperrten, und stand
   absichtlicher User-Positionierung im Weg (z. B. ein „Clover"-Pattern, das bewusst über diesen
   Punkt hinaus fahren soll).
3. *Physischer Defekt an einem festen absoluten Winkel, nur beim tatsächlichen Durchfahren
   ausgelöst* — Zwischenstand nach Sweep 2, erklärte alle bis dahin gesammelten Beobachtungen,
   blieb aber beim genauen Mechanismus vage. **Durch Hypothese 4 ersetzt/verfeinert**, siehe
   unten — keine Auflösung durch Reparatur nötig, weil es sich als kein Defekt herausstellte.
4. *Gimbal-Pol-Singularität (bestätigt, branchenweit bekanntes Phänomen)* — Web-Recherche
   (2026-08-21) fand praktisch identische Bug-Reports in mehreren unabhängigen
   Lighting-Foren: [QLC+](https://www.qlcplus.org/forum/viewtopic.php?t=7497) („head doesnt
   move in circle but rather a figure 8"), [Avolites](https://forum.avolites.com/viewtopic.php?t=811)
   ( „When pointing straight up/down this will indeed result in an 8 figure"),
   [grandMA2](https://forum.malighting.com/forum/thread/63918-pan-tilt-circle-effect-question/)
   und [DMXControl Projects](https://forum.dmxcontrol-projects.org/thread/3092-movinghead-bewegungsszene-kreis/)
   (deutsch, mit expliziter geometrischer Herleitung). **Mechanismus:** ein Pan/Tilt-Mover ist
   ein 2-Achsen-Gimbal. Am Zenit (Beam zeigt exakt entlang der Pan-Rotationsachse) wird Pan
   geometrisch degeneriert — jeder Pan-Wert erzeugt dieselbe physische Richtung („Gimbal Lock",
   dieselbe Mathematik wie die Pol-Singularität in Kugelkoordinaten). Ein per unabhängigem
   Sinus/Cosinus auf Pan/Tilt gezeichneter Kreis (das macht praktisch jeder Konsolen-Kreisgenerator,
   inkl. unserer `MovementEngine::getValues()`) ist nur dann ein echter Kreis im physischen Raum,
   wenn er den Pol nicht kreuzt — kreuzt er ihn, faltet sich dieselbe DMX-Bahn zu einer Acht.
   Erklärt lückenlos alle bisherigen Beobachtungen: DMX-Telemetrie zeigte einen „perfekten Kreis"
   (stimmt — im DMX-Wertebereich ist er das auch), der Kalibrier-Sweep war glatt monoton (er testete
   nur die Encoder-Abbildung, nicht die Kreis-nahe-dem-Pol-Projektion), der Effekt ist
   geschwindigkeitsunabhängig (rein geometrisch, keine Dynamik), und tritt exakt um den
   Zenit-Winkel auf. **Kein Defekt dieser Einheit — jeder Pan/Tilt-Mover zeigt das.**

**Zweiter Fix-Versuch (2026-08-21) live getestet, wieder verworfen — kein aktiver
Software-Fix.** `MovementEngine::getValues()` (`FX_Engine.h`) blendete pro Sample zwischen dem
alten linearen Modell (weit vom Pol entfernt) und einem polaren Modell (Pan = Azimut des
Offset-Vektors, Tilt = Radius vom Pol) für Samples nahe am Zenit. Kompilierte sauber, wurde
geflasht und live an einem exakt auf dem Zenit zentrierten Kreis getestet (der schwierigste
Fall — deckt sich mit dem User-Vorschlag „bei Tilt=127 einfach 360° Pan durchfahren"). Ergebnis:
**funktioniert nicht** — der Tilt-Wert sprang jede halbe Umdrehung zwischen den beiden Polseiten
hin und her statt konstant zu bleiben, sichtbar als „durchgestrichener Kreis"/erneute Acht.
Root Cause identifiziert (per Telemetrie UND Live-Beobachtung bestätigt, siehe `history.md`
2026-08-24): die Formel entschied „welche Seite des Pols" pro Sample anhand des Vorzeichens des
*naiven linearen* Tilt-Ergebnisses — genau die Größe, die beim Kreisen exakt um den Zenit zweimal
pro Umdrehung das Vorzeichen wechselt, wodurch der Tilt-Ausgang zwischen `Pol+Radius` und
`Pol-Radius` hin- und herspringt statt konstant zu bleiben. Ein nicht mehr getesteter Ansatz
(immer dieselbe, feste Polseite verwenden, da Pan mit vollem Azimut-Bereich ohnehin jede Richtung
abdecken kann) wurde identifiziert, aber auf User-Wunsch nicht mehr ausprobiert — Code wurde
komplett auf den vorigen, dokumentierten Stand (`git checkout`, entspricht Commit `07c5d38`)
zurückgesetzt und erneut geflasht. **Aktueller Stand: kein Software-Fix aktiv**, siehe
Workaround-Hinweis oben (Root-Cause-Punkt 4). Ein künftiger Versuch sollte bei der oben
beschriebenen Vorzeichen-Instabilität ansetzen, nicht von vorne anfangen.

**Fixture-eigene Sensor-Monitor-Referenzwerte (2026-08-20, nach Kalibrierung `-037`):** Tilt-
Codewheel-Range laut Menü **-90 bis +130** (`+021` = gerade nach oben, ≈ Range-Mittelpunkt —
bestätigt, dass die Kalibrierung den Zenit auf DMX-Tilt-Center legt). Pan-Codewheel-Range
**-83 bis +517**.

### Pan/Tilt-Geschwindigkeit (gemessen 2026-09-02, nicht aus dem Handbuch)

Das Handbuch nennt die Verfahrwinkel, aber **keine Geschwindigkeit**. Für den Controller ist sie
trotzdem nötig: DMX hat keine Positionsrückmeldung, also fährt jede kommandierte Position ins
Blaue. Wird schneller kommandiert, als die Mechanik kann, läuft der Sollwert dem Kopf davon — und
nach dem Loslassen fährt er den aufgelaufenen Rückstand stur zu Ende. Das sieht aus wie Trägheit,
ist aber keine.

**Messwerte:**

| Achse | Grenzrate | Winkelgeschwindigkeit | volle Strecke |
|---|---|---|---|
| Pan | 40 000 Einheiten/s | ~330 °/s (über 540°) | ~1,6 s |
| Tilt | 40 000 Einheiten/s | ~165 °/s (über 270°) | ~1,6 s |

Beide Achsen liegen beim selben Zahlenwert in 16-Bit-Einheiten — die Mechanik braucht für ihren
vollen Bereich also gleich lang, die Winkelgeschwindigkeiten unterscheiden sich nur, weil die
Bögen unterschiedlich groß sind. Das ist plausibel für identische Motoren und Treiber auf beiden
Achsen.

**Messverfahren — und warum das naheliegende nicht funktioniert.** Der erste Versuch leitete die
Grenze aus dem Nachlaufen ab: die höchste Rate suchen, bei der noch kein Nachlauf auftritt. Das
liefert **nur eine Untergrenze** — kein Nachlauf beweist, dass die Mechanik *mindestens* so
schnell ist, nie dass sie nicht schneller kann. So kamen 165°/s für Pan heraus, also genau die
Hälfte des wahren Werts, was den Kopf spürbar langsamer machte als vorher.

Das taugliche Verfahren ist **das Motorgeräusch**: denselben Schwenk mit steigender Sollrate
fahren und hinhören. Solange der Motor hörbar schneller wird, ist die Mechanik noch nicht am
Anschlag; sobald sich der Klang bei weiterer Erhöhung nicht mehr ändert, ist die Grenze erreicht.
Genommen wird der letzte Wert, bei dem er noch schneller wurde. (Im Firmware-Repo: `ptMaxRatePan`
und `ptMaxRateTilt` in `Moving_Head_Horizon.ino`, live setzbar über `/joy_cfg?ratep=&ratet=`.)

### CH5 — Speed
`0–255`, „Pan/Tilt speed, Pan/Tilt time" — das Handbuch spezifiziert
**keinen** exakten Split-Punkt zwischen den beiden Modi (typisch bei
Billig-Movern: oft eine Hälfte „Zeit bis Ziel" (Fade), die andere
„Geschwindigkeit"/Road-Runner). Unser Projekt behandelt CH5
(`state.motorSpeed`) aktuell als einfachen linearen 0–255-Wert — laut
Handbuch nicht widerlegt, aber auch nicht vollständig bestätigt. Kein
Fix nötig, nur zur Kenntnis: falls sich CH5 am echten Gerät „springend"
statt linear verhält, ist das der wahrscheinliche Grund.

**Praxisbefund 2026-09-02: CH5 gehört für Live-Bewegung auf 0.** Der Kanal ist die
fixture-eigene Anfahrglättung und wirkt auf *jede* kommandierte Position. Der frühere
Vorgabewert 128 legte damit eine zweite, unbekannte Glättung über alles, was der Controller
selbst rechnet — am Gerät als durchgehend indirektes Anfahren spürbar, egal was an der eigenen
Joystick-Glättung verstellt wurde. Auf 0 wird das Anfahren sofort direkt. Für das Movement FX
gilt dasselbe, dort würde die Fixture-Glättung die Figur zusätzlich verschmieren, weil der Kopf
einer ständig wandernden Position hinterherschleift.

Sinnvoll bleibt CH5 für **Szenenwechsel**: ein Preset schreibt die Position als Sprung, und bei
höherem CH5 gleitet der Kopf hinein statt mit vollen 330°/s hinzuknallen. Also bewusst
hochdrehen, wenn ein Wechsel getragen wirken soll — nicht als Dauerzustand.

### CH6 — Color
| DMX | Farbe |
|---|---|
| 0–4 | Color 1 |
| 5–9 | Color 2 |
| 10–14 | Color 3 |
| 15–19 | Color 4 |
| 20–24 | Color 5 |
| 25–29 | Color 6 |
| 30–34 | Color 7 |
| 35–39 | Color 8 |
| 40–44 | Color 9 |
| 45–49 | Color 10 |
| 50–54 | Half Color 1 |
| 55–59 | Half Color 2 |
| 60–64 | Half Color 3 |
| 65–69 | Half Color 4 |
| 70–74 | Half Color 5 |
| 75–79 | Half Color 6 |
| 80–84 | Half Color 7 |
| 85–89 | Half Color 8 |
| 90–94 | Half Color 9 |
| 95–99 | Half Color 10 |
| 100–255 | Clockwise Rotation (Speed) |

**Abgleich mit Code:** `wheelMap[20]` in `Moving_Head_Horizon.ino`
(`{0,50,5,55,10,60,15,65,20,70,25,75,30,80,35,85,40,90,45,95}`) nutzt genau
die Zonen-Startwerte, interleaved Solid/Half — **korrekt**, deckt sich
1:1 mit `COLOR_STEPS` im Frontend. Keine Änderung nötig.

### CH7 — Gobo (statisch, Rad 1, 9 Gobos + Open)
| DMX | Funktion |
|---|---|
| 0–9 | White (Open) |
| 10–19 | Gobo 1 |
| 20–29 | Gobo 2 |
| 30–39 | Gobo 3 |
| 40–49 | Gobo 4 |
| 50–59 | Gobo 5 |
| 60–69 | Gobo 6 |
| 70–79 | Gobo 7 |
| 80–89 | Gobo 8 |
| 90–99 | Gobo 9 |
| 100–129 | Clockwise Rotation (Scroll) |
| 130–134 | White (zweite Open-Zone) |
| 135–210 | Anti-clockwise Rotation (Scroll) |
| 211–215 | Gobo 1 Shake |
| 216–220 | Gobo 2 Shake |
| 221–225 | Gobo 3 Shake |
| 226–230 | Gobo 4 Shake |
| 231–235 | Gobo 5 Shake |
| 236–240 | Gobo 6 Shake |
| 241–245 | Gobo 7 Shake |
| 246–250 | Gobo 8 Shake |
| 251–255 | Gobo 9 Shake |

**Abgleich mit Code:** `sGoboMap[10]` (`{0,10,20,30,40,50,60,70,80,90}`) —
nutzt exakt die Zonen-Startwerte für White+Gobo1–9. **Korrekt, deckt sich
1:1 mit dem Handbuch.** Damit ist bestätigt: der vom User gemeldete Bug
„Gobo 6 static kommt nicht" liegt **nicht** an falschen DMX-Zahlen im
Code — `sGoboMap[6] = 60` trifft exakt die Mitte der offiziellen Gobo-6-
Zone (60–69). Wahrscheinlichste Erklärung: physische Abweichung des
tatsächlichen Glas-/Metallrads dieses konkreten Geräts vom gedruckten
Handbuch (bei diesem Preissegment keine Seltenheit — Handbücher werden
oft geräteübergreifend wiederverwendet), oder ein mechanischer Defekt an
genau dieser Radposition. Nur durch Sichtprüfung am Gerät zu klären, nicht
im Code zu fixen.

**Shake-Zonen-Formel:** `211 + (gobo_nr - 1) × 5` für `gobo_nr` 1–9
(kein Shake für „White"/Index 0 — dafür gibt es im Handbuch keine Zone).

**Live am Gerät bestätigt (2026-08-17):** Die 5 DMX-Werte innerhalb einer
Shake-Zone sind **keine kontinuierliche Geschwindigkeitsregelung**,
sondern exakt **5 diskrete, aufsteigende Shake-Geschwindigkeiten**
(Stufe 1 = langsamstes Wackeln bei `zone_start`, Stufe 5 = schnellstes
bei `zone_start+4`), fest in der Fixture-Firmware verdrahtet. Per
manuellem DMX-Sweep über Gobo 1 (CH7=211…215, je 6s gehalten, User
beobachtete live) verifiziert: „wackelt links rechts aufsteigend speed
gesteppt, 5 stufen". Der Code nutzt das jetzt direkt (`sgobFX.scratch
Speed`/`rgobFX.scratchSpeed` als Stufe 1–5, siehe `runStep()` in
`Moving_Head_Horizon.ino`) — keine Software-seitige Oszillation
innerhalb der Zone mehr (eine frühere Version tat das versehentlich und
ließ die Fixture dadurch ständig zwischen ihren 5 eingebauten
Geschwindigkeiten hin- und herspringen).

**Rotation/Scroll-Zone (100–210) als selbstgebauter, stufenloser Shake —
zwei Live-Tests, gegensätzliches Ergebnis, korrekte Technik gefunden
(2026-08-17).** Erster Test (sustained rotation, Gobo 1 fest auf einen
niedrigen CW-Wert wie `CH7=105` für 6+ Sekunden gehalten): „ist nur
durchgelaufen... ansonsten constant speed" — bei einer **gehaltenen**
Dauerrotation dreht das gesamte Rad kontinuierlich durch verschiedene
Gobo-Motive, kein Wackeln an Ort und Stelle. Das ist aber nicht die
richtige Technik — der eigentliche Vorschlag war, **kurze, abwechselnde
Pulse** zwischen CW- und CCW-Zone zu senden (nie eine Richtung lange
genug halten, um zum Nachbar-Gobo zu wandern), mit dem Index-Wert
zwischendurch erneut gesendet, um die Position zu verankern und Drift zu
verhindern (keine Rückkopplungs-/Positionsregelung in der
Rotations-Zone, nur offene Geschwindigkeitssteuerung). Zweiter Test mit
dieser korrigierten Technik (Gobo 6 fest, `CH7` alterniert zwischen
`129` [langsamste CW] und `135` [langsamste CCW], Index `60` zwischen
den Pulsen erneut gesendet): „es wackelt und pendelt overlaying minimal
links/rechts. nicht 100% smooth aber geht" — funktioniert wie erwartet.
**Jetzt fest implementiert** als „Rotation-Pulse-Shake" für CH7
(`runStep()` in `Moving_Head_Horizon.ino`, `rotationPulse=true` nur für
`sgobFX`) — echte, kontinuierlich einstellbare Speed (Hz) und Range
(Intensität) statt der 5 festen Fixture-Stufen. Zonen-Referenz: CW
100(langsam)–129(schnell), Stop 130–134, CCW 135(langsam)–210(schnell).
CH8 (rotierendes Gobo) hat keine dokumentierte Gegenrichtung auf sich
selbst und bleibt daher beim fixture-nativen 5-Stufen-Shake (siehe
`StepFX::scratchSpeed`-Kommentar in `FX_Engine.h` für die genaue
Begründung inkl. der CH9/Rotation-FX-Kollision, die eine CH9-basierte
Alternative verhindert).

### CH8 — Gobo (rotierend, Rad 2, 6 Gobos + Open)
| DMX | Funktion |
|---|---|
| 0–9 | White |
| 10–19 | Gobo 1 |
| 20–29 | Gobo 2 |
| 30–39 | Gobo 3 |
| 40–49 | Gobo 4 |
| 50–59 | Gobo 5 |
| 60–69 | Gobo 6 |
| 70–129 | Clockwise Rotation |
| 130–225 | White (zweite, sehr breite Open-Zone) |
| 226–230 | Gobo 1 Shake |
| 231–235 | Gobo 2 Shake |
| 236–240 | Gobo 3 Shake |
| 241–245 | Gobo 4 Shake |
| 246–250 | Gobo 5 Shake |
| 251–255 | Gobo 6 Shake |

**Abgleich mit Code:** `rGoboMap[7]` (`{0,10,20,30,40,50,60}`) — korrekt,
deckt sich 1:1 mit dem Handbuch.

**Shake-Zonen-Formel:** `226 + (gobo_nr - 1) × 5` für `gobo_nr` 1–6.

### CH9 — Gobo Rotation / Index (Rad 2)
| DMX | Funktion |
|---|---|
| 0–63 | Rotation Angle (fester Index/Winkel, keine kontinuierliche Drehung) |
| 64–192 | Clockwise Rotation (Geschwindigkeit steigt mit dem Wert) |
| 193–255 | Anti-clockwise Rotation (Geschwindigkeit steigt mit dem Wert) |

**Wichtig für `gRotFX` (Modulator, moduliert CH9):** Die Zonen 64–192 (CW)
und 193–255 (CCW) sind **physisch entgegengesetzte Drehrichtungen** — ein
Modulator-Bereich, der diese Grenze überschreitet, lässt das Gobo-Rad
beim Durchlaufen der Kurve sichtbar die Richtung wechseln statt gleichmäßig
zu drehen. Aktuell genutzter Bereich im Frontend-Default (`grSt:135,
grEn:190`) liegt komplett innerhalb der CW-Zone (64–192) — **das ist
sicher, keine Änderung nötig**, aber wichtig für künftige
Preset-/Default-Änderungen: `startVal`/`endVal` von `gRotFX` sollten immer
innerhalb *einer* der beiden Zonen bleiben (`64–192` **oder** `193–255`),
nie darüber hinweg.

### CH10 — Prism
| DMX | Funktion |
|---|---|
| 0–127 | No Prism |
| 128–255 | Prism on |

**Abgleich:** Frontend sendet `s.prism ? 200 : 0` — korrekt.

### CH11 — Prism Rotation
`0–255`, „Prism rotation" — Handbuch spezifiziert **keine** Unterzonen
(anders als CH9). Vermutlich ebenfalls CW/CCW-gesplittet, aber nicht
dokumentiert. `pRotFX`-Default (`prSt:193, prEn:255`) bleibt sicherheitshalber
in der oberen Hälfte — unauffällig, aber unverifiziert.

### CH12 — Frost
| DMX | Funktion |
|---|---|
| 0–127 | No Function |
| 128–255 | Frost simple |

**Abgleich:** Frontend sendet `s.frost ? 200 : 0` — korrekt.

### CH13 — Focus, CH14 — Zoom

**Reihenfolge korrigiert 2026-09-04.** Diese Überschrift lautete „CH13/CH14 — Zoom / Focus",
also in der Reihenfolge Zoom-zuerst. Alle anderen Paarüberschriften in diesem Dokument sind
positional („CH3/CH4 — Pan/Tilt", „CH15/CH16 — Pan Fine / Tilt Fine"), weshalb sie sich als
CH13 = Zoom lesen ließ — und um ein Haar dazu geführt hätte, die korrekte Belegung im Code
(`CH_FOCUS 13`, `CH_ZOOM 14`) zu „reparieren". Am Gerät bestätigt: **CH13 ist Focus, CH14 ist
Zoom**, so wie es im Code und in der Oberfläche steht.

**`0–255 linear` stimmt für beide.** Am 2026-09-04 kam die Meldung, CH14 zeige zwischen 0 und
130 keine Wirkung auf die Bildgröße. Ein Sweep am Gerät (Fokus auf CH13=128 festgehalten, CH14
in 17 Stufen von 0 auf 255) widerlegte das: der Zoom arbeitet über den ganzen Bereich.

Was den Eindruck erzeugt hat, ist Optik, nicht DMX: **mit einem rotierenden Gobo im Strahlengang
ist die Größenänderung kaum zu sehen.** Zoom verschiebt Linsen und verändert damit zugleich die
Schärfeebene; das Gobo sitzt in der Brennebene, also wird sein Bild beim Zoomen unscharf, statt
sichtbar größer zu werden. Man sieht „weicher", nicht „größer". Ohne Gobo, im offenen Beam, ist
der Effekt eindeutig.

Praktische Folge für die Bedienung: **Zoom und Fokus gehören zusammen nachgeführt.** Wer zoomt,
muss den Fokus mitziehen, sonst wirkt der Zoom wie eine Fehlfunktion. Größere Pulte machen das
automatisch („Zoom/Focus Tracking"); hier ist es Handarbeit.

**Kein Fix nötig, weder im Code noch in der Oberfläche.** Diese Passage stand einen Tag lang
falsch hier — sie behauptete auf Verdacht eine tote Zone unterhalb von 130 und leitete daraus
ab, der Regler verschenke seinen halben Weg. Der Sweep am Gerät sagt das Gegenteil. Notiert als
Warnung: eine Fehlermeldung ist eine Beobachtung, keine Diagnose, und gehört nicht als Befund
dokumentiert, bevor sie gemessen ist.

### CH15/CH16 — Pan Fine / Tilt Fine
Beide `0–255`, 8-Bit-Feinanteil zu CH3/CH4 (ergibt 16-Bit-Auflösung
zusammen). Deckt sich mit `CH_PAN_FINE`/`CH_TILT_FINE`.

### CH17 — Macro
| DMX | Funktion |
|---|---|
| 10–120 | Auto mode |
| 121–150 | Auto mode |
| 151–255 | Sound mode |

**Achtung — Diskrepanz gefunden:** Das Handbuch kennt nur diese drei
groben Zonen (zwei davon beide „Auto mode", ohne weitere Erklärung, welche
Makros/Effekte konkret dahinterstecken). Unser Frontend-Dropdown
(`data/index.html`, `Optics · prism · macros`-Panel) bietet aktuell 13
benannte Einzelwerte an (`Lamp On`, `Lamp Off`, `Reset fixture`, `Fan
speed auto/high`, `Pan/Tilt speed fast/slow`, `Blackout on move`, `Sound
auto`, `Demo mode 1/2/3` bei u. a. 8/16/24/32/40/80/120/168/200/230/240/255)
— das sind **generische Platzhalterwerte aus einer anderen
Fixture-Vorlage**, keine für dieses Gerät verifizierten Werte. Das
Handbuch selbst ist zu ungenau, um daraus die echten Einzelwerte
abzuleiten (es nennt keine Makro-Namen, nur "Auto mode"/"Sound mode" als
Sammelbegriff). **Nicht blind gefixt** — entweder das Handbuch-Kapitel 10
(Menü „Set → Run Mode") am Gerät selbst durchklicken, um die echten Makro-
Grenzen zu ermitteln, oder die Werte so lassen (sie fallen zumindest alle
in eine plausible Zone: alle Werte ≥10 landen in „Auto mode" oder „Sound
mode", verursachen also vermutlich *irgendein* Auto-/Sound-Verhalten, nur
nicht zwingend das im Dropdown-Label versprochene).

### CH18 — Reset
| DMX | Funktion |
|---|---|
| 0–127 | No Function |
| 128–255 | Reset after 5 seconds |

Deckt sich mit `README.md`s Beschreibung „CH18 System Reset".

## Bordmenü-Referenz (Abschnitt 9/10 des Handbuchs, zur Vollständigkeit)

Direkt am Gerät (Display + [MENU]/[UP]/[DOWN]/[ENTER]) erreichbar:

- **Set:** Run Mode (DMX/Auto/Random/Sound), DMX Address (001–512),
  Channel Mode (Std. 18ch), Top LED Temp, Sound Regulator, Invert Pan/
  Tilt, Pan-Tilt Swap, Pan-Tilt Encoder, No DMX Signal (Clear/Keep),
  Display On/Off, Load Default.
- **Manu:** DMX channel, DMX Monitor.
- **Sys:** System Errors, LED Temp, Partial Fixture/LED Hours,
  Sensor Monitor (Hall 1–6, HallXY, Pan/Tilt Codewheel State/Step).
- **Advan** (Passwort-geschützt, ↑↑↑↓↓↓): Pan/Tilt/Colour/Gobo/Focus/
  Zoom/Prism/Rt.Gobo-Kalibrierung, Zero Point Correction.

Relevant z. B. falls „No DMX Signal" auf „Clear" statt „Keep" steht — dann
blackt das Gerät bei WLAN-Aussetzern selbstständig, unabhängig von unserer
Firmware-Logik.

## Offene Punkte / nächste Schritte

- **Gobo-6-static-Problem** ist laut diesem Abgleich kein Code-Bug
  (siehe CH7 oben) — braucht Sichtprüfung am physischen Rad, kein
  weiterer Software-Fix.
- **CH17-Macro-Dropdown** nutzt vermutlich falsche/generische Werte (siehe
  CH17 oben) — nicht kritisch (keine Nutzerbeschwerde bisher), aber
  bekannt-unverifiziert. Bei Bedarf am Gerät durchklicken.
- **Shake-Offset-Formel im Code war falsch** (nutzte einen einzigen
  geratenen Konstanten-Offset `+183` für alle Wheel-Typen) — mit den
  jetzt bekannten echten Zonen (`211 + (n-1)×5` für CH7, `226 + (n-1)×5`
  für CH8) korrigiert, danach per interaktiver Hardware-Kalibrierung
  (manueller DMX-Sweep, User beobachtete live) bestätigt: die 5 Werte pro
  Zone sind 5 diskrete, aufsteigende Shake-Geschwindigkeiten (kein
  kontinuierlicher Bereich) — Software wählt jetzt direkt eine Stufe
  1–5 statt zu oszillieren. **Gelöst und live verifiziert**, kein offener
  Punkt mehr. Siehe `history.md` (2026-08-17) für die volle
  Fund-/Fix-Geschichte.
