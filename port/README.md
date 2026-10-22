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
| Titelbildschirm | ⏳ Overlay `title` upstream noch nicht dekompiliert → Start im Debug-Stage-Select (`select1`), wie upstreams `dev_exe` |
| Stages | 62 Overlays nativ; 18 (z. B. `s07b`, `s08b`, `s11d`, `s11g`, `s14e`, `s19b`) fehlen noch upstream |
| Grafik | ✔️ Software-GPU (alle GP0-Befehle, Halbtransparenz, Dithering, Maskenbit) |
| 3D-Mathematik | ✔️ GTE-Emulation nach Hardware-Spezifikation |
| Sound | ✔️ SPU mit 24 ADPCM-Stimmen, ADSR, Hall (Reverb nach Hardware-Formel, alle PsyQ-Presets), XA-Audio (Filmton) |
| Filmsequenzen (STR/MDEC) | ✔️ CD-Streaming in Echtzeit, VLC-Dekoder (STR v2/v3), MDEC, XA-Ton; mit Testdaten geprüft, mit echten Filmen noch nicht |
| Speicherstände | ✔️ Memory-Card-Image `memcard1.mcd` (Standardformat, emulatorkompatibel) |
| Eingabe | ✔️ Tastatur und Gamepads (SDL GameController, analog) |

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
│   └── gen_overlay_table.py
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
│       └── os.c            Windows/POSIX-Spezifisches
└── tests/test_runtime.c    Selbsttests GTE/GPU/Filmkette (ohne Spieldaten)
```

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
  24-Bit-Verweise eindeutig dekodierbar sind (`psx_decode_addr24`).
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
