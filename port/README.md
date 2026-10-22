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
| Sound | ✔️ SPU mit 24 ADPCM-Stimmen und ADSR; ⏳ Reverb, XA-Audio |
| Filmsequenzen (STR/MDEC) | ⏳ noch nicht: Filme brechen nach dem eingebauten Timeout ab |
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
│       ├── spu.c           Soundchip
│       ├── libcd.c         CD-Laufwerk aus Disc-Image
│       ├── libpad.c        Controller
│       ├── card.c          Memory Card + „bu00:“-Dateisystem
│       ├── libpress.c      Film-Streaming/MDEC (Platzhalter)
│       └── os.c            Windows/POSIX-Spezifisches
└── tests/test_runtime.c    Selbsttests GTE/GPU (ohne Spieldaten)
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
2. Filmsequenzen: CD-Streaming (`St*`) und MDEC-Dekoder (`DecDCT*`).
3. XA-Audio (Musik/Funksprüche) und SPU-Reverb.
4. Hochauflösendes Rendering / Breitbild als Option.
5. Neue upstream dekompilierte Overlays erscheinen automatisch: Submodul
   aktualisieren und neu bauen.
