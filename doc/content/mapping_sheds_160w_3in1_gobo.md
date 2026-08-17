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

### CH5 — Speed
`0–255`, „Pan/Tilt speed, Pan/Tilt time" — das Handbuch spezifiziert
**keinen** exakten Split-Punkt zwischen den beiden Modi (typisch bei
Billig-Movern: oft eine Hälfte „Zeit bis Ziel" (Fade), die andere
„Geschwindigkeit"/Road-Runner). Unser Projekt behandelt CH5
(`state.motorSpeed`) aktuell als einfachen linearen 0–255-Wert — laut
Handbuch nicht widerlegt, aber auch nicht vollständig bestätigt. Kein
Fix nötig, nur zur Kenntnis: falls sich CH5 am echten Gerät „springend"
statt linear verhält, ist das der wahrscheinliche Grund.

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

### CH13/CH14 — Zoom / Focus
Beide `0–255` linear. Kein Fix nötig.

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
