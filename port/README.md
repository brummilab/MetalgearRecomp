# Nativer PC-Port (Windows / Linux)

Dieser Ordner baut aus dem dekompilierten Quellcode von
[mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing) ein **natives
PC-Programm**. Es gibt keinen Emulator: Der C-Code des Spiels wird direkt für
x86 kompiliert. Nur die PlayStation-Hardware und Sonys Bibliotheken (PsyQ)
werden durch eine eigene Laufzeitschicht ersetzt.

> **Spieldaten:** Das Programm enthält keine Spieldaten. Zum Spielen braucht man
> ein Abbild der eigenen Disc 1 von *Metal Gear Solid Integral* (JP,
> **SLPM-86247**), denn genau diese Version ist dekompiliert. Die US- und
> PAL-Version passen **nicht**.

## Stand

| Bereich | Stand |
| --- | --- |
| Build Linux (x86, 32 Bit) | ✔️ baut, linkt, Selbsttests grün |
| Build Windows (MinGW, 32 Bit) | ✔️ baut, linkt, Selbsttests grün (getestet unter Wine) |
| Boot | ✔️ Taskscheduler, alle Systemtasks, Memory Card, Pad, Dateisystem starten |
| Spielablauf mit Disc-Image | ⚠️ noch nicht getestet (kein Disc-Image verfügbar) |
| Titelbildschirm | ✔️ Overlay `title` recompiliert (mit PS1-Build); ohne ihn Start im Debug-Stage-Select (`select1`) |
| Stages | 62 Overlays nativ, 16 weitere recompiliert (u. a. `brf`, `s07b`, `s08b`, `s11d`, `s14e`, `s19b`); es fehlen `rank` (Hardware-Zugriffe) und `s07br` (kein PS1-Build-Ausgang) |
| Grafik | ✔️ Software-GPU (alle GP0-Befehle, Halbtransparenz, Dithering, Maskenbit), interne Auflösung 1×/2×/4×, Breitbild 16:9 |
| 3D-Mathematik | ✔️ GTE-Emulation nach Hardware-Spezifikation |
| Sound | ✔️ SPU mit 24 ADPCM-Stimmen, ADSR, Hall (Reverb nach Hardware-Formel, alle PsyQ-Presets), XA-Audio (Filmton), SPU-Interrupts samplegenau (Takt für gestreamte Sprache) |
| Filmsequenzen (STR/MDEC) | ✔️ CD-Streaming in Echtzeit, VLC-Dekoder (STR v2/v3), MDEC, XA-Ton; mit Testdaten geprüft, mit echten Filmen noch nicht |
| Speicherstände | ✔️ Memory-Card-Image `memcard1.mcd` (Standardformat, emulatorkompatibel) |
| Eingabe | ✔️ Tastatur und Gamepads (SDL GameController, analog), Belegung siehe unten |

### Tastenbelegung

In der japanischen Fassung bestätigt **○ (Kreis)**, **× (Kreuz)** bricht ab.

| PlayStation | Tastatur | Xbox-Controller |
| --- | --- | --- |
| ○ Kreis (bestätigen) | C | B |
| × Kreuz (zurück) | X | A |
| □ Quadrat | Z | X |
| △ Dreieck | V | Y |
| Start / Select | Enter / Backspace | Start / Back |
| Steuerkreuz | Pfeiltasten | Steuerkreuz |
| L1 / R1 / L2 / R2 | A / S / Q / W | LB / RB / – / – |

### Auflösung und Breitbild

**Höhere Auflösung** (`--render-scale 2|4`): Neben dem originalen VRAM gibt es
eine hochaufgelöste Kopie. Jedes Primitiv wird in beide gezeichnet. Der
originale VRAM bleibt maßgeblich für CPU-Zugriffe, Paletten und indizierte
Texturen, die Kopie wird angezeigt. 15-Bit-Texturen werden aus der Kopie
gelesen, so bleiben Render-to-Texture-Effekte scharf. Das Verfahren ähnelt dem
guter PS1-Emulatoren. Grenzen: Die 3D-Koordinaten bleiben ganzzahlig (kein
Subpixel-„PGXP“), Linien werden als Blöcke vergrößert.

Rechenzeit (Software-Rendering, pessimistische Testszene, 1 CPU-Kern):
Faktor 1 ≈ 3 ms, Faktor 2 ≈ 16 ms, Faktor 4 ≈ 33 ms pro Frame. MGS läuft
meist mit 30 fps (33 ms Budget). Faktor 4 braucht deshalb eine schnelle CPU.

**Breitbild** (`--widescreen`): Der GTE-Breitbild-Hack staucht die
3D-Projektion horizontal auf 3/4, und das Bild wird auf 16:9 gestreckt. Die
3D-Welt erscheint unverzerrt mit etwa 33 % mehr Sichtfeld.

**HUD unverzerrt** (Standard im Breitbild, abschaltbar mit `--stretch-hud`):
2D-Elemente wie HUD, Menüs, Texte und Funk-Bildschirm werden nicht über den GTE
projiziert und kämen gestreckt heraus. Der Grafikchip staucht sie deshalb
ebenfalls um die Bildmitte auf 3/4, und zwar im Zielraster, damit
hochaufgelöste Schrift sauber bleibt. Die Unterscheidung kommt aus einem Cache
aller Bildschirmpositionen, die der GTE erzeugt (die Idee hinter PGXP,
`src/psx/ws.c`):
- Polygone und Linien mit mindestens 2 Ecken aus dem Cache sind 3D und bleiben unverändert.
- Alle anderen Polygone und Linien sowie alle Sprites/Rechtecke gelten als 2D.

Filme werden im Breitbild mit schwarzen Rändern in 4:3 gezeigt; ihre
Untertitel bleiben dann ungestaucht.

Bekannte Grenzen:
- Das HUD liegt im mittleren 4:3-Bereich, nicht an den Rändern des 16:9-Bilds.
- 2D-Elemente, die an 3D-Positionen hängen (z. B. Markierungen über Gegnern),
  rutschen etwas zur Mitte.
- Sprites, die das Spiel als 3D-Effekt nutzt, würden wie HUD gestaucht. MGS
  zeichnet 3D-Effekte nach bisherigem Stand als Polygone; das ist mit echten
  Szenen noch zu prüfen.
- Objekte am Bildrand können später auftauchen, weil das Spiel nach seinem
  4:3-Sichtfeld aussortiert.

### 60 fps (experimentell, `--60fps`)

Die Spiellogik läuft fest mit 30 Ticks pro Sekunde (`DG_FrameRate = 2`) und
zählt in Frames. Doppelt so oft laufen lassen würde das Spiel doppelt so
schnell machen. Deshalb wird zwischen zwei Spielframes ein **interpoliertes
Zwischenbild** gezeichnet; die Logik bleibt unverändert (`src/psx/interp.c`):

- Jede vom GTE projizierte Bildschirmposition bekommt einen Schlüssel:
  Adresse der geladenen Rotationsmatrix (pro Objekt, z. B. `&obj->screen`),
  wie oft diese Matrix im Frame geladen wurde, Nummer des Vertex danach.
  Den Hinweis auf die Matrixadresse fügt `asm2c.py` beim Übersetzen von
  `gte_SetRotMatrix` ein.
- Das Spiel kopiert die Positionen mit normalem C-Code in seine Primitive.
  Deshalb wird über den **Wert** zugeordnet (wie bei PGXP). Im Zwischenbild
  liegt jeder wiedererkannte Vertex auf halbem Weg zwischen alter und neuer
  Position.
- Sicherungen: Polygone werden nur interpoliert, wenn **alle** Ecken eindeutig
  wiedererkannt werden und sich höchstens 64 Pixel bewegt haben, also keine
  Überblendung bei Kameraschnitten. Sprites/HUD bleiben unverändert.
  Interpoliert wird nur, wenn das Spiel gerade mit 30 fps läuft; Filme
  (15 fps über eigenen Takt) nicht.
- Beide Pufferarten werden unterstützt: Zeichnet das Spiel in den gerade
  sichtbaren Puffer, kommt das Zwischenbild zuerst (+1 Frame Latenz).
  Zeichnet es in einen hinteren Puffer, wird das Zwischenbild in den
  sichtbaren Puffer gezeichnet (keine zusätzliche Latenz).

Grenzen: Ohne echtes Disc-Image ist ungeprüft, wie gut die Zuordnung im Spiel
greift. Mögliche Artefakte sind springende Vertices bei Objekten, deren
Vertex-Reihenfolge sich von Frame zu Frame ändert, und Teile, die nicht
interpoliert werden, z. B. 2D-Effekte und Objekte ohne eigene Matrix. Die Animation
der Spielfiguren selbst bleibt bei 30 Schritten pro Sekunde, nur die
Bildschirmbewegung wird geglättet.

### Sprache

*MGS Integral* hat englische Sprachausgabe und eine Spracheinstellung
(`GM_CONFIG_ENGLISH`) für Texte und Filmuntertitel. Der Port setzt sie
standardmäßig **bei jedem Frame auf Englisch**, weil Spielstände und das
Optionsmenü sie sonst zurücksetzen würden (`src/game/native_options.c`).
Mit `--japanese` bleibt die Einstellung dem Spiel überlassen. Welche Texte
dabei trotzdem japanisch bleiben, zeigt sich erst mit einem echten Disc-Image.

## Bauen

Voraussetzungen: CMake ≥ 3.16, Ninja, Python 3 mit `jinja2`, GCC oder Clang
mit 32-Bit-Unterstützung, SDL2 (32 Bit). Die Submodule müssen ausgecheckt sein
(`git submodule update --init`).

### Linux (Ubuntu/Debian)

```bash
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install gcc-multilib cmake ninja-build python3-venv libsdl2-dev:i386
python3 -m venv .venv && .venv/bin/pip install jinja2
scripts/build-port.sh            # Ergebnis: build/linux/mgs
```

### Windows

**Variante A – MSYS2 (direkt unter Windows):** Im Startmenü „MSYS2 MINGW32“
öffnen, dann:

```bash
pacman -S git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja \
          mingw-w64-i686-SDL2 mingw-w64-i686-python mingw-w64-i686-python-jinja2
git clone --recursive https://github.com/brummilab/MetalgearRecomp.git
cd MetalgearRecomp
cmake -S port -B build/windows -G Ninja
ninja -C build/windows
```

`build/windows/mgs.exe` zusammen mit `SDL2.dll` (aus `/mingw32/bin`) weitergeben.

**Variante B – Cross-Build unter Linux:**
`sudo apt install gcc-mingw-w64-i686`, dann `scripts/build-port.sh windows`.

## Starten

```bash
build/linux/mgs --iso "MGS Integral Disc 1.bin"
build\windows\mgs.exe --iso "MGS Integral Disc 1.bin"
```

| Option | Bedeutung |
| --- | --- |
| `--iso PFAD` | Disc-Image: `.bin` (2352 Byte/Sektor, aus `.cue/.bin`) oder `.iso` (2048) |
| `--save-dir ORDNER` | Ort von `memcard1.mcd` (Standard: aktueller Ordner) |
| `--scale N` | Fenstergröße (Standard 3 = 960×720) |
| `--fullscreen` | Vollbild (F11 schaltet um) |
| `--japanese` | Spracheinstellung des Spiels unverändert lassen (Standard: Englisch erzwungen) |
| `--render-scale N` | interne Auflösung: 1 = Original 320×240, 2 = 640×480 (Standard), 4 = 1280×960 |
| `--widescreen` | 16:9 mit breiterem 3D-Sichtfeld; HUD/Menüs bleiben unverzerrt, Filme in 4:3 |
| `--stretch-hud` | im Breitbild HUD/Menüs strecken statt unverzerrt lassen |
| `--60fps` | **experimentell:** interpolierte Zwischenbilder, 60 statt 30 Bilder/s |

**Tastatur:** Pfeiltasten = Steuerkreuz, X = ✕, C = ○, Z = □, V = △,
A/S = L1/R1, Q/W = L2/R2, Enter = Start, Backspace = Select, Esc = Beenden.
Gamepads (Xbox/PlayStation) werden automatisch erkannt.

## Architektur

```
port/
├── CMakeLists.txt          Build (übersetzter Spielcode + Laufzeit + SDL2)
├── cmake/                  Toolchains: linux-i686, mingw-i686
├── include/
│   ├── mgs_native.h        wird in jede Spieldatei eingebunden
│   └── stdarg.h            Compiler-Builtins statt PsyQ-Stack-Hack
├── tools/
│   ├── gen_sources.py      Quellenlisten aus upstreams Linker-Template
│   ├── asm2c.py            GTE-Inline-Assembler -> C, wenige Quell-Fixups
│   ├── link_overlay.py     Overlays vorlinken, Symbole lokalisieren
│   ├── gen_overlay_table.py
│   ├── recomp.py           statischer MIPS→C-Recompiler für Overlays
│   ├── unecm.c             ECM-Disc-Abbilder (*.bin.ecm) entpacken
│   └── native_syms.py      Symbollisten des nativen Programms für recomp.py
├── src/
│   ├── main.c              Einstieg, Kommandozeile
│   ├── platform_sdl.c      Fenster, Eingabe, Audio
│   ├── game/               Overlay-Anbindung (als Spielcode kompiliert)
│   └── psx/                Ersatz für PS1-Hardware und PsyQ-Bibliotheken
│       ├── kernel.c        Threads (Fibers), Interrupts, Events, BIOS
│       ├── gte.c libgte.c  Geometrie-Coprozessor
│       ├── gpu.c libgpu.c  Grafikchip (Software-Rasterizer), Ordering Tables
│       ├── spu.c           Soundchip inkl. Hall (Reverb)
│       ├── libcd.c         CD-Laufwerk aus Disc-Image, Streaming (St*)
│       ├── libpad.c        Controller
│       ├── card.c          Memory Card + „bu00:“-Dateisystem
│       ├── mdec.c          Filme: VLC-Dekoder + MDEC (IDCT, YUV→RGB)
│       ├── xa.c            XA-ADPCM-Audio (Filmton)
│       ├── recomp_rt.c     Laufzeit recompilierter Overlays (Thunks, Relokation)
│       └── os.c            Windows/POSIX-Spezifisches
├── tests/test_runtime.c    Selbsttests GTE/GPU/Filmkette (ohne Spieldaten)
└── tests/test_recomp.c     Recompiler gegen Compiler (nur mit PS1-Build)
```

**Disc-Abbilder im ECM-Format** (`*.bin.ecm`) wandelt `unecm` (wird mitgebaut,
`build/<plattform>/unecm[.exe]`) zurück in ein normales Abbild. Es prüft die
Prüfsumme und meldet, ob es die richtige Disc ist (`SLPM_862.47`):
`unecm "<abbild>.bin.ecm" mgs1.bin`

**Das Upstream-Repo bleibt unverändert.** Der Build legt eine übersetzte Kopie
der Quellen in `<build>/gen/` an:

- **Inline-Assembler:** Die GTE-Makros (`lwc2`, `mtc2`, `cop2 …`) werden zu
  Aufrufen des GTE-Emulators. Stack-Tricks (`SetSpadStack`) entfallen.
- **Fixups** (`FIXUPS` in `tools/asm2c.py`): eine Handvoll gezielter Änderungen,
  z. B. Lvalue-Casts und andere GCC-2.x-Eigenheiten, Kernel-Tabelle statt Adresse
  0x100, der Leerlauf-Task gibt CPU-Zeit ab, der Overlay-Hook. Greift ein Fixup
  nach einem Upstream-Update nicht mehr, bricht der Build mit einer klaren
  Meldung ab.

**Speicherlayout:** Der Spielcode rechnet mit festen Adressen: Heaps bei
0x801xxxxx, Scratchpad bei 0x1F800000. Außerdem speichert der Grafikchip
Verweise mit nur 24 Bit. Der Port ist deshalb bewusst ein 32-Bit-Programm:

- PS1-RAM (2 MB) liegt bei `0x80000000`, das Scratchpad bei `0x1F800000`,
  beide werden beim Start fest eingeblendet.
- Das Programmabbild liegt bei `0x00400000` und muss unter 16 MB bleiben, damit
  24-Bit-Verweise eindeutig dekodierbar sind (`psx_decode_addr24`). Große
  Puffer der Laufzeitschicht (VRAM, Sound-RAM, Film- und Memory-Card-Puffer)
  liegen deshalb auf dem Heap (`PSX_HEAP_BUFFER`), und `psx_mem_init()` bricht
  mit einer klaren Meldung ab, wenn die Programmdaten die Grenze überschreiten.
- Der Kopierschutz der japanischen Disc (`SafetyCheck` in `title`/`change`,
  prüft auf eine gepresste Disc in einem echten Laufwerk) meldet im Port
  „bestanden“ (`OVERRIDES` in `recomp.py`). BIOS-Aufrufe aus Overlays
  (Sprung nach 0xA0/0xB0/0xC0, Funktionsnummer in `$t1`) gehen an `rc_bios`.
- Funktionen, die mit `j`/`b` in eine andere springen (gemeinsamer Epilog,
  der auf Stapelrahmen und Register des Springers angewiesen ist), werden zu
  einer C-Funktion zusammengefasst; die Funktionsanfänge darin werden zu
  Einstiegen (s08b: 2, d18a: 1).
- Die Einstiegs-Hüllen recompilierter Funktionen deklarieren keine Parameter
  und lesen ihre Argumente nur (`RC_ARGS8`): Native Aufrufer übergeben über
  Funktionszeiger so viele Argumente wie das Original, und ein Compiler darf
  Parameter-Slots als Zwischenspeicher nutzen – mit acht deklarierten
  Parametern überschrieb das den Stapel des Aufrufers.
- Recompilierter Code bildet Zugriffe auf die ersten 64 KB (Null-Zeiger; auf
  der PS1 der Anfang des RAMs) wie die Konsole auf das PS1-RAM ab, ohne
  Verzweigung (`rc_ea`); Stapel- und bekannte Adressen gehen direkt. Er wird
  mit `-Os` übersetzt, damit die Programmdaten unter 16 MB bleiben (derzeit
  ~14,1 MB). Wird es knapp, gehört der recompilierte Code in ein eigenes Modul
  oberhalb von 16 MB (Code muss nicht unter der Grenze liegen, nur Daten).
- **F12** speichert das angezeigte Bild (`screenshot-NNN.bmp`) und den
  PS1-Bildspeicher (`vram-NNN.bmp`, alle Texturen und Bildpuffer) im
  Arbeitsverzeichnis; `MGS_SCREENSHOT_FRAME=n` löst das beim n-ten Bild aus.
- Fehlersuche: `--watch-stack` (Windows) überwacht per Hardware-Watchpoint
  die Rücksprungadresse der Actor-Schleife und meldet mit `WATCH:`, welche
  Anweisung sie überschreibt; `--guard-mem` sperrt den alten Programmbereich.
- Wo der Spielcode 24-Bit-Verweise selbst in Zeiger umwandelt (`DG_SortChanl`,
  `libdg2.c`, `divide.c` über `getaddr`, PsyQ-Makro `nextPrim`), löst
  `mgs_ptr24()` sie wie die GPU-Emulation auf; auf der Konsole sind sie als
  KUSEG-Adressen gültig. Zurück in Ordering Tables kommen sie wieder als
  24-Bit-Verweise (sonst landet das obere Byte im Längenfeld des Pakets).
- **Reihenfolge der Argumente:** Der PS1-Compiler wertet Funktionsargumente
  von links nach rechts aus, GCC auf x86 nicht zwingend. Aufrufe, deren
  Argumente nacheinander das Skript lesen (`GCL_GetNextInt()`,
  `GCL_GetOption()` …), schreibt `asm2c.py` in feste Reihenfolge um
  (`GLOBAL_FIXUPS`); der Test `evalorder` (`tools/check_evalorder.py`) meldet
  neue Fälle. Gefunden über einen Patch von
  [RE:INTEGRAL](https://github.com/Drommedhar/mgs-reintegral) (`alert.c`).
- **Markierungen im Bit 31:** Einige Zeiger tragen auf der Konsole eine
  Markierung im obersten Bit (HZD-Bodenzeiger in `CONTROL`, Kugeln, Claymore,
  Reflexkugel). Vor dem Zugriff löscht `MGS_HZD()` (= `mgs_kuseg()`) sie.
  Die rote Lebensleiste markiert ihre Konfiguration nativ in Bit 0 statt
  über die Adresse (`menu/life.c`). Dazu kommen abgesicherte NULL-Zugriffe
  (Munition in `sna_init.c`, `GM_PlayerControl` in `script.c`) und eine
  nicht initialisierte Richtung in `kojo/demoexec.c`. Alles nach den
  RE:INTEGRAL-Patches.
- **Negative Tabellenindizes:** Ohne Waffe ist `GM_Weapon` = -1. Auf der
  Konsole trifft `weapon_actions_8009ED8C[-1]` die direkt davor gelinkte
  Tabelle `actions_no_weapon_8009ED70`; `asm2c.py` macht das explizit.
- Die SPU mischt bis zu einem ausgelösten Interrupt und erst nach dessen
  Handler weiter: Der Sound-Treiber nutzt den Interrupt einer stummen
  Taktstimme als Uhr für gestreamte Sprache (`test_spu_irq`).
- Bei einem Absturz schreibt das Programm `CRASH:`-Zeilen mit Adresse und
  Aufrufkette ins Log (zuordnen mit `addr2line`/`nm` auf dem Build mit
  Debug-Infos).
- Unter Windows braucht das ein 64-Bit-Windows. Das Programm ist
  „large address aware“ gelinkt.

**Interrupts:** Nativer Code lässt sich nicht unterbrechen. VSync-, CD- und
SPU-Interrupts werden deshalb an sicheren Punkten zugestellt: beim Warten auf
den nächsten Frame, im Leerlauf-Task und bei Aufrufen in die Laufzeitschicht.
Ein Interrupt-Handler kann wie auf der Konsole über die Kernel-Tabellen den
aktiven Task wechseln.

**Overlays:** Auf der PS1 lädt jeder Spielabschnitt sein eigenes Codemodul
nach. Nativ werden alle vollständig dekompilierten Overlays fest eingebunden.
Jedes wird vorher einzeln vorgelinkt, und seine Symbole werden lokalisiert.
Lädt das Spiel ein Overlay von der Disc, ersetzt `mgs_native_overlay_loaded()`
die MIPS-Charaktertabelle durch die native.

### Recompilierte Overlays (z. B. `brf`, das Briefing)

18 Overlays sind upstream noch nicht fertig dekompiliert. Statt auf upstream zu
warten, übersetzt `tools/recomp.py` sie **statisch aus dem Original-Maschinencode
nach C**. Der Maschinencode stammt aus dem eigenen PS1-Matching-Build
(`scripts/build.sh`, legt `upstream/mgs_reversing/obj/<overlay>.bin` an), also
nicht von der Disc und nicht aus dem Netz. Ohne PS1-Build fehlen diese Overlays
einfach wie bisher.

- **Übersetzung:** Jede MIPS-Funktion wird eine C-Funktion auf nachgebildeten
  Registern. Der Stackrahmen liegt in einem lokalen Array. Übersetzt werden
  Delay-Slots, Sprungtabellen, HI/LO, unausgerichtete Zugriffe (`lwl/lwr`)
  und GTE-Befehle. Funktionen und Einsprünge findet der Recompiler über die
  Linker-Map, `jal`-Ziele, `lui/addiu`-Konstanten und Datenwörter.
- **Anbindung an den nativen Code:** Eine Datenflussanalyse verfolgt
  `%hi/%lo`-Paare und `$gp`. Adressen von Symbolen des Hauptprogramms werden auf
  die nativen Symbole umgerechnet, Aufrufe gehen direkt an die nativen
  Funktionen. Mit statisch eingelinkten PsyQ-Funktionen (MDEC, CD-Streaming, …)
  ruft das Overlay die Laufzeitschicht auf.
- **Laden:** Das Spiel lädt das Overlay wie auf der Konsole nach `0x800C3208`.
  `psx_recomp_install()` schreibt dann an jeden Einsprungpunkt einen kleinen
  x86-Sprung (`mov eax, ziel; jmp eax`) zur recompilierten Funktion. So
  funktionieren auch Funktionszeiger in Tabellen und Actor-Strukturen.
  Datenwörter, die auf das Hauptprogramm zeigen, werden reloziert. Der
  PS1-RAM ist dafür ausführbar eingeblendet. Mit `--guard-mem` wird der
  Bereich des alten Hauptprogramms (`0x80010000–0x800C3000`) gesperrt, damit
  ein nicht umgerechneter Zugriff sofort auffällt (zur Fehlersuche).
- **Prüfung:** `test_recomp` übersetzt Funktionen des Hauptprogramms
  (`GV_StrCode`, `GV_VecLen3`, `GV_VecDir2`, …) und vergleicht sie auf
  Zufallseingaben mit den nativ kompilierten. Außerdem installiert es alle
  recompilierten Overlays und ruft über einen Thunk auf. Ein absichtlich
  eingebauter Fehler im recompilierten Code fällt dem Test auf.
- **Ausnahme `rank`:** Dieses Overlay steuert Timer und Pad-Schnittstelle direkt
  über Hardware-Register und bleibt deshalb vorerst aus
  (`-DMGS_RECOMP_SKIP=rank`). `-DMGS_RECOMP_OVERLAYS=OFF` schaltet den
  Recompiler ganz ab.

Wird ein Overlay upstream fertig dekompiliert, übernimmt der Port automatisch
die native Fassung. Dekompilierter Code hat immer Vorrang.

## Nächste Schritte

1. Mit einem echten Disc-Image testen und Abstürze bzw. Grafikfehler beheben.
2. Hochauflösendes Rendering / Breitbild als Option.
3. Neue upstream dekompilierte Overlays erscheinen automatisch: Submodul
   aktualisieren und neu bauen.

### Hall (Reverb)

MGS schaltet den Modus *Studio Large* mit halber Tiefe für die Kanäle 0–12
ein. `spu.c` rechnet den Hall wie die Hardware (psx-spx „SPU Reverb Formula“):

- 22050 Hz, Arbeitsbereich oben im Sound-RAM, Ringpuffer mit Wrap
- Reflexionen auf derselben und der Gegenseite, vier Kammfilter, zwei
  Allpassfilter, Sättigung auf 16 Bit
- Registerwerte aller PsyQ-Modi (Room, Studio A/B/C, Hall, Space, Echo,
  Delay, Pipe) aus den dokumentierten Presets
- Vereinfachung: Statt des 39-Tap-FIR-Filters der Hardware wird beim
  Wechsel 44,1↔22,05 kHz einfach gemittelt bzw. gehalten. Die Parameter
  *delay*/*feedback* der Modi Echo/Delay werden ignoriert; MGS nutzt sie nicht.

### Filmwiedergabe

Der Filmplayer des Spiels (`game/movie.c`, Abspann in `takabe/ending2.c`)
läuft unverändert, darunter liegt:

- **CD-Streaming** (`libcd.c`): `CdRead2` liest mit echter Laufwerksrate
  (150 Sektoren/s bei doppelter Geschwindigkeit). Damit stimmt das Tempo von
  15-fps-Filmen. STR-Sektoren (Kennung `0x0160`) werden zu Frames
  zusammengesetzt (`StGetNext`/`StFreeRing`). Ist der Ring voll, wird ein
  Frame verworfen, wie auf der Konsole.
- **VLC** (`mdec.c`): STR-Bitstrom Version 2/3 (MPEG-1-Huffman-Tabelle B.14,
  Escape-Codes, DC-Prädiktion bei v3) → MDEC-Codes.
- **MDEC**: Dequantisierung mit der PSX-Standardtabelle, 8×8-IDCT,
  YCbCr→RGB (15 Bit), Makroblöcke spaltenweise. Die Ausgabe an den
  Callback des Spiels kommt sofort statt per DMA-Interrupt.
- **XA-Audio** (`xa.c`): 4/8-Bit-ADPCM, Mono/Stereo, 37,8/18,9 kHz, auf
  44,1 kHz umgerechnet und über den CD-Eingang des SPU gemischt.
  Filmton gibt es nur mit Raw-Images (`.bin`), weil ISO-Images (2048 Byte/Sektor)
  keine XA-Audiosektoren enthalten können.
