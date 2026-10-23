# MetalgearRecomp

**Metal Gear Solid** (PlayStation, 1998), dekompiliert und als **natives
PC-Programm für Windows und Linux** neu kompiliert, ohne Emulator.

Das Repo baut auf dem Community-Projekt
[FoxdieTeam/mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing) auf.
Dort wird *Metal Gear Solid Integral* (JP) Funktion für Funktion in C-Code
zurückübersetzt, der bitgenau das Original ergibt:

| Ziel | Stand (Upstream, Okt. 2026) |
| --- | --- |
| `SLPM_862.47` / `SLPM_862.48` – Hauptprogramm Disc 1/2 | ✔️ 100 % dekompiliert, matching |
| `SLPM_862.49` – Hauptprogramm VR-Disc | ✔️ 100 % dekompiliert, matching |
| Stage-Overlays | 🚧 62 vollständig, 18 noch in Arbeit (u. a. `title`); im PC-Port 16 davon recompiliert |
| VR-Overlays (`vr_*`, `vab_*`, …) | ❌ größtenteils offen |

Dieses Repo ergänzt das um:

1. **Nativer PC-Port** ([`port/`](port/README.md)): eigene Laufzeitschicht,
   die PS1-Hardware und Sonys PsyQ-Bibliotheken ersetzt (Software-GPU,
   GTE-Emulation, SPU, CD aus Disc-Image, Filmwiedergabe mit MDEC und
   XA-Audio, Memory Card, Controller über SDL2).
   Läuft unter Windows und Linux als 32-Bit-Programm.
   Overlays, die upstream noch nicht fertig sind (z. B. das Briefing `brf`
   und der Titelbildschirm), übersetzt ein **statischer Recompiler** aus dem
   eigenen PS1-Build nach C (siehe [`port/README.md`](port/README.md#recompilierte-overlays-z-b-brf-das-briefing)).
2. **Matching-Build** der PS1-Version unter Linux/WSL (Wine), inklusive
   Hash-Prüfung gegen das Original.

> 🎮 **Erster Test mit echter Disc (Windows, 23.10.2026): Das Spiel läuft bis
> ins Titelmenü** (mit recompiliertem Titelbildschirm).
>
> ⚠️ **Stand des PC-Ports:** Er baut, linkt und bootet auf Linux und Windows.
> Mit einem echten Disc-Image ist der Spielablauf noch **nicht getestet**.
> Titelbildschirm und einige Stages fehlen noch upstream, Details siehe
> [port/README.md](port/README.md).

## Aufbau

```
MetalgearRecomp/
├── upstream/
│   ├── mgs_reversing/   # Submodul: Decomp-Quellcode, Assembler, Build-System
│   └── psyq_sdk/        # Submodul: PsyQ-Compiler/-Linker/-Header (Sony PS1 SDK)
├── port/                # nativer PC-Port (CMake, Laufzeitschicht, Werkzeuge)
├── scripts/
│   ├── setup.sh         # Umgebung einrichten (Pakete, venv, Submodule)
│   ├── build-port.sh    # PC-Port bauen (linux | windows)
│   └── build.sh         # PS1-Matching-Build + Hash-Check
├── docs/
│   ├── workflow.md      # Anleitung: eine Funktion dekompilieren
│   └── decompme-status.md # täglicher Überblick über decomp.me/preset/19
└── Projektinhalt.md     # Projektübersicht und Verlauf
```

## Schnellstart: PC-Port

```bash
git clone --recursive https://github.com/brummilab/MetalgearRecomp.git
cd MetalgearRecomp
scripts/setup.sh                  # Ubuntu/Debian/WSL
scripts/build-port.sh             # -> build/linux/mgs
build/linux/mgs --iso "MGS Integral Disc 1.bin"
```

Windows-Build (MSYS2 oder Cross-Build) und Steuerung: siehe
[port/README.md](port/README.md).

Benötigt wird ein Abbild der **eigenen** Disc 1 von *Metal Gear Solid
Integral* (SLPM-86247). Andere Versionen (US/PAL) passen nicht zum Code.

## PS1-Matching-Build

```bash
scripts/build.sh              # SLPM_862.47, Disc 1/2
scripts/build.sh vr_exe       # SLPM_862.49, VR-Disc
scripts/build.sh dev_exe      # Dev-Build zum Experimentieren
```

Ein erfolgreicher Build endet mit bestandenem Hash-Check: Die gebauten Dateien
sind bitgenau identisch mit dem Original. Das ist geprüft, alle 80 Ziele sind
OK. Zum Bauen ist **keine** Spiel-ISO nötig.

Hinweis: Wine stürzt bei vielen parallelen Compiler-Prozessen gelegentlich ab.
`scripts/build.sh` baut dann mit `JOBS=2` weiter.

## Mitarbeiten

Fortschritt auf decomp.me: [docs/decompme-status.md](docs/decompme-status.md)
(täglich aktualisierte Übersicht über [decomp.me/preset/19](https://decomp.me/preset/19)).

- Dekompilieren: [docs/workflow.md](docs/workflow.md). Kurzfassung: Funktion
  aus `asm/overlays/` wählen, auf [decomp.me](https://decomp.me/preset/19)
  matchen, Pull Request an upstream.
- PC-Port: Laufzeitschicht in `port/src/psx/`, Architektur in
  [port/README.md](port/README.md).

Upstream aktualisieren (neu dekompilierte Overlays landen danach automatisch
im PC-Port):

```bash
git submodule update --remote upstream/mgs_reversing
git add upstream/mgs_reversing && git commit -m "Upstream aktualisiert"
```

## Rechtliches

Dieses Repo enthält **keine** Spieldaten (ISO, Executables, Assets). Sie sind
urheberrechtlich geschützt (© Konami) und dürfen nicht eingecheckt werden;
`.gitignore` blockiert die üblichen Formate. Zum Spielen braucht man eine
legal erworbene Kopie des Spiels.

Das Upstream-Projekt `mgs_reversing` hat derzeit keine ausgewiesene Lizenz.

## Verwandte Projekte

Es gibt weitere Projekte mit ähnlichem Ziel, unter anderem
[RE:INTEGRAL](https://github.com/Drommedhar/mgs-reintegral) (ebenfalls auf Basis von
FoxdieTeams Dekompilierung, Integral JP, Windows, Kampagne durchspielbar) sowie
statische Recompilierungen der US-Fassung. Vergleich und Folgerungen:
[`docs/verwandte-projekte.md`](docs/verwandte-projekte.md).

Eine allgemeine Recherche zu weiteren Projekten, Werkzeugen und Formaten sowie
zu Fehlerquellen nativer PS1-Ports steht in
[`docs/recherche.md`](docs/recherche.md).

## Lizenz

Der eigene Code und die Doku dieses Repos stehen unter der
[MIT-Lizenz](LICENSE). Ausgenommen sind die Submodule unter `upstream/`
(FoxdieTeam), daraus beim Build erzeugter Code und das Spiel selbst.
Spieldaten sind nicht enthalten. Inoffizielles Projekt, nicht mit Konami
verbunden.

