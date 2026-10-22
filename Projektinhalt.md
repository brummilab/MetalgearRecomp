# Projektinhalt: MetalgearRecomp

## Ziel

*Metal Gear Solid* (PS1) als **natives PC-Programm unter Windows und Linux**,
auf Basis des dekompilierten C-Quellcodes und ohne Emulator.

## Entscheidungen

| Datum | Entscheidung | Begründung |
| --- | --- | --- |
| 2026-10-22 | Aufbau auf [FoxdieTeam/mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing) statt Neustart | Hauptprogramme von MGS Integral (JP) dort bereits 100 % matching; ein Neustart würde Jahre doppelter Arbeit bedeuten |
| 2026-10-22 | Upstream und PsyQ-SDK als Git-Submodule | Eigenes Repo bleibt schlank, Upstream lässt sich jederzeit aktualisieren |
| 2026-10-22 | Zielversion: MGS Integral JP (SLPM-86247/48/49) | Vorgabe des Upstream-Projekts; nur diese Version ist dekompiliert |
| 2026-10-22 | Repo umbenannt: `metalgeardecomp` → `MetalgearRecomp` | Vorgabe David |
| 2026-10-22 | Ziel PC-Port (Windows/Linux nativ) | Vorgabe David |
| 2026-10-22 | Port als 32-Bit-Programm | Spielcode speichert Zeiger in 32-Bit-Feldern und patcht Zeiger in Disc-Daten; 64 Bit hieße massiver Umbau |
| 2026-10-22 | PS1-RAM bei 0x80000000, Scratchpad bei 0x1F800000 fest eingeblendet; Programm unter 16 MB | Feste Adressen im Spielcode funktionieren unverändert; 24-Bit-Verweise der GPU bleiben dekodierbar; funktioniert unter Linux und Windows |
| 2026-10-22 | Upstream-Code wird nicht verändert; Build erzeugt übersetzte Kopie (`asm2c.py`) | Upstream bleibt aktualisierbar; Assembler → C und wenige Fixups automatisch |
| 2026-10-22 | Software-Rasterizer statt OpenGL | Exaktes PS1-Verhalten (VRAM, Halbtransparenz, Render-to-Texture) ohne Sonderfälle; Hochauflösung später als Option |
| 2026-10-22 | Interrupts an sicheren Punkten statt echter Unterbrechung | Nativer Code ist nicht unterbrechbar; Task-Wechsel im Interrupt über nachgebildete Kernel-Tabellen |
| 2026-10-22 | MDEC-Ausgabe synchron statt per DMA-Interrupt | Spiel-Callback wird sofort bedient; Folgeanforderungen aus dem Callback laufen in einer Schleife statt rekursiv |
| 2026-10-22 | Overlays fest eingebunden, je Overlay vorgelinkt mit lokalisierten Symbolen | Eine einzige Programmdatei, keine DLL-/dlopen-Probleme, keine Symbolkonflikte zwischen Overlays |

## Technik

- **PS1-Build:** PsyQ 4.4/4.3 (`cc1psx.exe`, GCC 2.8) unter Wine, Ninja, SHA256-Prüfung
- **PC-Port:** CMake + Ninja, GCC/Clang (Linux, `-m32`) bzw. MinGW-w64 i686 (Windows), SDL2
- **Laufzeitschicht** (`port/src/psx`): Kernel/Threads (Fibers), GTE, GPU (Software), SPU, CD (.bin/.iso), Pad, Memory Card (.mcd)
- **Werkzeuge** (`port/tools`): `gen_sources.py`, `asm2c.py`, `link_overlay.py`, `gen_overlay_table.py`
- **Tests:** `port/tests/test_runtime.c` (GTE-Projektion, NCLIP, Trigonometrie, GPU-Rasterung, Filmkette Streaming→VLC→MDEC + XA)

## Struktur

- `upstream/mgs_reversing` – Submodul, Decomp-Code
- `upstream/psyq_sdk` – Submodul, Toolchain und Header
- `port/` – nativer PC-Port, siehe `port/README.md`
- `scripts/setup.sh`, `scripts/build-port.sh`, `scripts/build.sh`
- `docs/workflow.md` – Anleitung zum Dekompilieren einer Funktion

## Stand (22.10.2026)

- Upstream-Stand: Commit `7bbcb7102`
- PS1-Matching-Build verifiziert: alle 80 Ziele hash-identisch
- PC-Port Linux: baut, Selbsttests grün, bootet bis zum CD-Zugriff
- PC-Port Windows: baut (MinGW-Cross), Selbsttests grün, bootet unter Wine gleich weit
- 168 Quelldateien Hauptprogramm + 62 Overlays nativ eingebunden
- Mit echtem Disc-Image noch nicht getestet

## Offene Punkte

- [ ] Test mit eigenem Disc-Image (SLPM-86247), Fehler beheben
- [x] Filmsequenzen: CD-Streaming (`St*`), VLC, MDEC, XA-Audio
- [ ] Filmwiedergabe mit echten Filmen prüfen
- [x] SPU-Reverb (Hall)
- [ ] Titelbildschirm (Overlay `title`) – wartet auf Upstream; bis dahin Start im Stage-Select
- [ ] Fehlende Stages upstream: brf, camera, change, d18a(r), rank, s03d, s07b(r), s08b(r), s11d, s11g, s14e, s15c, s19b(r), title
- [ ] Optional: Hochauflösendes Rendering, Breitbild, 60-fps-Optionen
- [ ] Optional: Windows-Build nativ unter Windows (MSYS2) einmal real testen
- [ ] Fork von `mgs_reversing` für eigene Pull Requests anlegen

## Verlauf

- **2026-10-22:** Repo angelegt, Submodule eingebunden, Setup-/Build-Skripte, README und Workflow-Doku erstellt. PS1-Matching-Build verifiziert.
- **2026-10-22:** Repo umbenannt in MetalgearRecomp. Nativer PC-Port begonnen und lauffähig bis zum Boot: Build-System, Assembler-Übersetzer, komplette Laufzeitschicht (Kernel, GTE, GPU, SPU, CD, Pad, Memory Card), Overlay-Einbindung, Linux- und Windows-Build, Selbsttests.
- **2026-10-22:** Filmwiedergabe eingebaut: CD-Streaming mit echter Laufwerksrate, STR-Frame-Zusammensetzung, VLC-Dekoder (STR v2/v3), MDEC (IDCT, YUV→RGB), XA-ADPCM-Filmton im SPU-Mixer. Ende-zu-Ende-Test mit selbst erzeugtem Disc-Image.
- **2026-10-22:** Hall (SPU-Reverb) eingebaut: Hardware-Formel mit 22050 Hz, alle PsyQ-Presets, Arbeitsbereich im Sound-RAM. Test: Nachhall klingt über ca. 2 s ab, ohne Hall endet der Impuls sofort.
