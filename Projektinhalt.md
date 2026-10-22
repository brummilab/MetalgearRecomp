# Projektinhalt: Metal Gear Solid Decomp

## Ziel

Decomp von *Metal Gear Solid* (PS1) – C-Quellcode, der bitgenau die
Original-Executables ergibt.

## Entscheidungen

| Datum | Entscheidung | Begründung |
| --- | --- | --- |
| 2026-10-22 | Aufbau auf [FoxdieTeam/mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing) statt Neustart | Hauptprogramme von MGS Integral (JP) dort bereits 100 % matching; Neustart würde Jahre doppelter Arbeit bedeuten |
| 2026-10-22 | Upstream und PsyQ-SDK als Git-Submodule | Eigenes Repo bleibt schlank, Upstream lässt sich jederzeit aktualisieren |
| 2026-10-22 | Zielversion: MGS Integral JP (SLPM-86247/48/49) | Vorgabe des Upstream-Projekts |

## Technik

- **Zielplattform:** Sony PlayStation (MIPS R3000A)
- **Compiler:** PsyQ 4.4 / 4.3 (`cc1psx.exe`, GCC 2.8-basiert) unter Wine
- **Build:** Python (`build.py`) erzeugt `build.ninja`, Ninja baut, `compare.py` prüft SHA256
- **Matching-Hilfe:** [decomp.me](https://decomp.me/preset/19), Preset „Metal Gear Solid (overlays)“
- **Emulator zum Testen:** PCSX-Redux

## Struktur

- `upstream/mgs_reversing` – Submodul, Decomp-Code
- `upstream/psyq_sdk` – Submodul, Toolchain
- `scripts/setup.sh`, `scripts/build.sh` – Einrichtung und Build unter Linux/WSL
- `docs/workflow.md` – Anleitung zum Dekompilieren einer Funktion

## Stand

- Upstream-Stand eingebunden: Commit `7bbcb7102` (22.10.2026)
- Build in Linux-Container getestet (Ubuntu 24.04, Wine 9.0)

## Offene Punkte / Ideen

- [ ] Fork von `mgs_reversing` für eigene Pull Requests anlegen
- [ ] Erstes Overlay als Arbeitsschwerpunkt wählen (Kandidaten: `s10a` Underground Passage – noch nicht begonnen)
- [ ] Optional: Decomp der US-Version (SLUS-00594) als Ableitung prüfen

## Verlauf

- **2026-10-22:** Repo angelegt, Submodule eingebunden, Setup-/Build-Skripte, README und Workflow-Doku erstellt.
