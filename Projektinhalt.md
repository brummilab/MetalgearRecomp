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
| 2026-10-22 | Englisch erzwingen (Standard), `--japanese` als Schalter | Integral hat englische Sprachausgabe; Spracheinstellung wird jedes Frame gesetzt, weil Spielstände/Optionsmenü sie überschreiben |
| 2026-10-22 | Höhere Auflösung über zweiten, hochaufgelösten VRAM; Standard Faktor 2 | Original-VRAM bleibt maßgeblich (CPU-Zugriffe, Paletten); Faktor 2 passt sicher ins 30-fps-Budget |
| 2026-10-22 | Breitbild per GTE-Projektion (×3/4) | Ohne Eingriff in Spielcode; HUD-Verzerrung als bekannte Einschränkung |
| 2026-10-22 | HUD-Erkennung über Cache der GTE-Bildschirmpositionen (≥ 2 Treffer = 3D), Sprites immer 2D | Keine Änderung am Spielcode; Schwelle 2 verhindert HUD-Flackern durch Zufallstreffer |
| 2026-10-22 | 60 fps als experimentelle Option über Zwischenbilder (Vertex-Zuordnung per Wert, Schlüssel aus Matrixadresse + Reihenfolge) | Spiellogik zählt in Frames und darf nicht schneller laufen; Projektion wird in Scratchpad-Tabellen kopiert, daher Zuordnung über Wert statt Adresse |
| 2026-10-22 | Overlays fest eingebunden, je Overlay vorgelinkt mit lokalisierten Symbolen | Eine einzige Programmdatei, keine DLL-/dlopen-Probleme, keine Symbolkonflikte zwischen Overlays |
| 2026-10-22 | Unfertige Overlays (brf u. a.) statisch aus dem PS1-Build recompilieren statt von Hand fertig zu dekompilieren | Ein Werkzeug für alle 18 Overlays statt ca. 9000 Befehle Handarbeit; Maschinencode aus eigenem Build, keine Spieldaten im Repo; dekompilierter Code hat Vorrang |
| 2026-10-22 | x86-Sprünge (Thunks) an den Original-Einsprungadressen, Overlay bleibt an 0x800C3208 | Funktionszeiger in Tabellen/Actors funktionieren ohne Zeigerübersetzung; Overlay-Daten bleiben unverändert adressierbar |
| 2026-10-22 | Statisch eingelinkte PsyQ-Funktionen in Overlays → Laufzeitschicht | Die Kopien würden Hardware-Register ansprechen (MDEC-DMA, CD) |
| 2026-10-22 | Overlay `rank` vorerst nicht recompiliert | Greift direkt auf Timer- und Pad-Register zu |
| 2026-10-23 | Eigenständig weiterführen, bei jedem Problem zuerst RE:INTEGRAL/PsyZ abgleichen (Option 3) | Kein Doppelaufwand bei bereits gelösten Problemen; eigene Nische: schlanker SDL-Port für Windows und Linux |
| 2026-10-23 | MIT-Lizenz für eigenen Code (ohne Submodule, generierten Code, Spiel) | Repo ist öffentlich; gleiche Lizenz wie RE:INTEGRAL erleichtert Austausch |
| 2026-10-23 | Große Laufzeitpuffer auf dem Heap, Startprüfung der 16-MB-Grenze | Spieldaten müssen für 24-Bit-GPU-Verweise unter 16 MB liegen; recompilierte Overlays haben das Programm darüber wachsen lassen |

## Technik

- **PS1-Build:** PsyQ 4.4/4.3 (`cc1psx.exe`, GCC 2.8) unter Wine, Ninja, SHA256-Prüfung
- **PC-Port:** CMake + Ninja, GCC/Clang (Linux, `-m32`) bzw. MinGW-w64 i686 (Windows), SDL2
- **Laufzeitschicht** (`port/src/psx`): Kernel/Threads (Fibers), GTE, GPU (Software), SPU, CD (.bin/.iso), Pad, Memory Card (.mcd)
- **Werkzeuge** (`port/tools`): `gen_sources.py`, `asm2c.py`, `link_overlay.py`, `gen_overlay_table.py`, `recomp.py` (MIPS→C-Recompiler), `native_syms.py`
- **Tests:** `port/tests/test_runtime.c` (GTE-Projektion, NCLIP, Trigonometrie, GPU-Rasterung, Filmkette Streaming→VLC→MDEC + XA), `port/tests/test_recomp.c` (recompilierte vs. native Funktionen, Overlay-Installation; nur mit PS1-Build)

## Struktur

- `upstream/mgs_reversing` – Submodul, Decomp-Code
- `upstream/psyq_sdk` – Submodul, Toolchain und Header
- `port/` – nativer PC-Port, siehe `port/README.md`
- `scripts/setup.sh`, `scripts/build-port.sh`, `scripts/build.sh`
- `docs/workflow.md` – Anleitung zum Dekompilieren einer Funktion
- `docs/decompme-status.md` – täglicher Überblick über https://decomp.me/preset/19 (`scripts/decompme_status.py`)
- `docs/verwandte-projekte.md` – Vergleich mit anderen MGS1-PC-Projekten (RE:INTEGRAL u. a.)
- `docs/recherche.md` – Internetrecherche: weitere Projekte, Werkzeuge, Formate, Lehren aus anderen nativen Ports

## Stand (22.10.2026)

- Upstream-Stand: Commit `7bbcb7102`
- PS1-Matching-Build verifiziert: alle 80 Ziele hash-identisch
- PC-Port Linux: baut, Selbsttests grün, bootet bis zum CD-Zugriff
- PC-Port Windows: baut (MinGW-Cross), Selbsttests grün, bootet unter Wine gleich weit
- 168 Quelldateien Hauptprogramm + 62 Overlays nativ eingebunden + 16 Overlays recompiliert (brf, camera, change, d18a, d18ar, s03d, s07b, s08b, s08br, s11d, s11g, s14e, s15c, s19b, s19br, title)
- Mit echtem Disc-Image (Windows): bootet bis ins Titelmenü (23.10.2026)

## Offene Punkte

- [x] Overlay **brf** (Briefing) eingebaut – über den statischen Recompiler
- [ ] brf ist auf decomp.me komplett gematcht (44/44) – sobald upstream gemergt: Submodul aktualisieren, Port nimmt dann automatisch die dekompilierte Fassung
- [ ] Recompilierte Overlays mit echtem Disc-Image prüfen (Briefing, Titel, Stages)
- [ ] Overlay `rank`: Timer-/Pad-Registerzugriffe abfangen, dann ebenfalls recompilieren

- [ ] Mit Disc-Image prüfen, welche Texte trotz Englisch-Einstellung japanisch bleiben; ggf. übersetzen
- [ ] Option: US-Version dekompilieren (SLUS-00594) – nur gemeinsam mit upstream sinnvoll

- [ ] Test mit eigenem Disc-Image (SLPM-86247), Fehler beheben
- [x] Filmsequenzen: CD-Streaming (`St*`), VLC, MDEC, XA-Audio
- [ ] Filmwiedergabe mit echten Filmen prüfen
- [x] SPU-Reverb (Hall)
- [x] Titelbildschirm (Overlay `title`) – recompiliert
- [x] Fehlende Stages (brf, camera, change, d18a(r), s03d, s07b, s08b(r), s11d, s11g, s14e, s15c, s19b(r), title) – recompiliert; s07br hat keinen PS1-Build-Ausgang
- [x] Hochauflösendes Rendering (2×/4×) und Breitbild (GTE-Hack)
- [x] HUD im Breitbild unverzerrt (GTE-Positions-Cache), Filme im Breitbild als 4:3
- [x] 60 fps (experimentell) über interpolierte Zwischenbilder
- [ ] 60 fps mit echtem Spiel prüfen und Zuordnung verfeinern
- [ ] Optional: Subpixel-Präzision (PGXP), Multithread-Rasterizer
- [ ] Mit echten Szenen prüfen: HUD-Erkennung, 3D-Sprites, Markierungen über Gegnern
- [ ] Optional: Windows-Build nativ unter Windows (MSYS2) einmal real testen
- [ ] Fork von `mgs_reversing` für eigene Pull Requests anlegen
- [ ] Aus `docs/recherche.md`: alle Vorzeichenbit-Zeigertests suchen, Division durch null absichern, GTE-Tests (`Square0`/`Square12`, MVMVA), übergroße Polygone verwerfen, Langzeittest über 20 min

## Verlauf

- **2026-10-22:** Repo angelegt, Submodule eingebunden, Setup-/Build-Skripte, README und Workflow-Doku erstellt. PS1-Matching-Build verifiziert.
- **2026-10-22:** Repo umbenannt in MetalgearRecomp. Nativer PC-Port begonnen und lauffähig bis zum Boot: Build-System, Assembler-Übersetzer, komplette Laufzeitschicht (Kernel, GTE, GPU, SPU, CD, Pad, Memory Card), Overlay-Einbindung, Linux- und Windows-Build, Selbsttests.
- **2026-10-22:** Filmwiedergabe eingebaut: CD-Streaming mit echter Laufwerksrate, STR-Frame-Zusammensetzung, VLC-Dekoder (STR v2/v3), MDEC (IDCT, YUV→RGB), XA-ADPCM-Filmton im SPU-Mixer. Ende-zu-Ende-Test mit selbst erzeugtem Disc-Image.
- **2026-10-22:** Hall (SPU-Reverb) eingebaut: Hardware-Formel mit 22050 Hz, alle PsyQ-Presets, Arbeitsbereich im Sound-RAM. Test: Nachhall klingt über ca. 2 s ab, ohne Hall endet der Impuls sofort.
- **2026-10-22:** Englisch erzwingen eingebaut (`GM_CONFIG_ENGLISH` jedes Frame, Schalter `--japanese`), im laufenden Spiel per Debugger geprüft. Compiler-Warnungen bereinigt.
- **2026-10-22:** Höhere Auflösung (1×/2×/4×) und Breitbild eingebaut; Rasterizer auf Spannen mit inkrementellen Gradienten umgestellt. Tests: Hochauflösung stimmt mit Originalpixeln überein, Breitbild-Projektion, Leistungsmessung.
- **2026-10-22:** HUD im Breitbild unverzerrt: 2D-Elemente werden um die Mitte gestaucht, 3D über GTE-Positions-Cache erkannt; Filme im Breitbild als 4:3 mit Rändern. Tests bei 1× und 2×, 16:9-Vorschau geprüft.
- **2026-10-22:** 60 fps (experimentell) eingebaut: interpolierte Zwischenbilder ohne Änderung der Spiellogik, für beide Doppelpuffer-Varianten, mit Schutz vor Kameraschnitten. Tests mit bewegtem Objekt grün.
- **2026-10-22:** Überblick über decomp.me/preset/19 eingerichtet: `docs/decompme-status.md` (Fortschritt pro Overlay, Mitwirkende, neueste Scratches), tägliche Aktualisierung als geplante Aufgabe. Erster Stand: brf 44/44 gematcht, s14e 13/42.
- **2026-10-22:** Overlay brf (Briefing) eingebaut, und zwar über einen neuen statischen MIPS→C-Recompiler (`port/tools/recomp.py`), der alle unfertigen Overlays aus dem eigenen PS1-Build übersetzt: 16 Overlays inkl. Titelbildschirm, Thunks an den Original-Adressen, Umrechnung auf native Symbole, PsyQ-Kopien → Laufzeitschicht. Test `test_recomp` (Linux + Wine) grün, erkennt absichtliche Fehler. Mit echtem Disc-Image noch nicht getestet.
- **2026-10-23:** `unecm` (ECM-Abbilder entpacken, mit Prüfsumme und Disc-Erkennung) ergänzt, gegen das Referenzwerkzeug getestet. Windows-Testpaket (mgs.exe, unecm.exe, SDL2.dll) für den ersten Test mit eigenem Disc-Abbild bereitgestellt.
- **2026-10-23:** Erster Test mit echtem Disc-Abbild (Windows): Absturz beim Einlesen des Disc-Verzeichnisses (NULL-Zugriff in `srchfile.c`, auf der PS1 harmlos) behoben. Kernel-Bereich 0x80000000–0x8000FFFF wieder lesbar, nur das alte Hauptprogramm bleibt gesperrt; OT-Verweis auf 0 beendet die Liste. Absturz-Handler schreibt Absturzadresse und Aufrufkette ins Log. Mit nachgebauter Mini-Disc geprüft.
- **2026-10-23:** Zweiter Absturz mit echtem Abbild gefunden: Durch die recompilierten Overlays lag das Programm über 16 MB, Spielvariablen wie die Zeichenlisten (`DG_Chanls`) wurden bei 24-Bit-Verweisen abgeschnitten. Große Laufzeitpuffer auf den Heap verlegt (Daten enden jetzt bei ~13 MB), Prüfung beim Start ergänzt; Speichersperre nur noch mit `--guard-mem`.
- **2026-10-23:** Dritter Absturz mit echtem Abbild (Menü-Init lädt Textur von Adresse 0): DMA-artige Übergaben (LoadImage/StoreImage, SpuWrite, CdGetSector, DecDCTin/out) bilden niedrige Adressen wie die Konsole auf den PS1-RAM ab. Erster Abschnitt `init` lädt jetzt vollständig.
- **2026-10-23:** Vierter Test: Menü-Init und Laden von `title` klappen. Absturz beim Start des Titels: Stage-Binaries der Streaming-Region (`s`) lädt das Spiel direkt an die Overlay-Adresse, ohne `GM_LoadInitBin` – der Overlay-Hook lief nie, das Spiel sprang in MIPS-Code. Hook jetzt zusätzlich am Ende jedes Stage-Ladevorgangs (betrifft native und recompilierte Overlays).
- **2026-10-23:** Fünfter Test: Overlay `title` wird installiert und läuft recompiliert. Absturz bei der Memory-Card-Prüfung: `memcard_check()` endet im Original ohne Rückgabewert (auf der PS1 liefert der Tail-Call `memcard_dummy()` z. B. 0xC0000003 für einen leeren Slot), nativ kam „Karte OK“ heraus. Per Fixup explizit gemacht; Compiler-Scan bestätigt: keine weiteren Funktionen ohne Rückgabewert im Spielcode.
- **2026-10-23:** Build-Fehler behoben: Geänderte Fixups wurden erst beim zweiten Build-Lauf kompiliert (Ninja prüfte die übersetzten Quellen vor dem Übersetzen). Übersetzte Quellen sind jetzt Byproducts von `mgs_translate`; mit einem Testlauf geprüft. Dadurch fehlte die Memory-Card-Korrektur im ausgelieferten Testpaket v7.
- **2026-10-23:** Sechster Test: Memory-Card-Prüfung im Titel läuft. Absturz in `DG_DivideChanl`: asm2c übersetzte das Sichern von `$sp` in `SetSpadStack` als Schreibzugriff auf Adresse 0 – behoben. Maschinencode-Scan nach Zugriffen auf Adresse 0 gefunden: weitere in `dummy_fl.c` (Fixup) und im recompilierten Code (Original-MIPS schreibt dorthin) → Zugriffe auf die ersten 64 KB werden im recompilierten Code wie auf der PS1 abgebildet; Scan jetzt 0 Treffer. Recompilierter Code mit `-Os` (Datenende 14,1 MB). Header jetzt ebenfalls Byproducts der Übersetzung. Startprüfung erkennt Debug-Abschnitte unter Windows korrekt.
- **2026-10-23:** Siebter Test: Absturz mit EIP 0 im Spiel-Task – die Rücksprungadresse von `GV_ExecActorSystem` wird mit 0 überschrieben. Statische Prüfungen (Querverweise zwischen Funktionen, $sp-Zugriffe im recompilierten Titel) ohne Befund; Diagnose-Option `--watch-stack` (Hardware-Watchpoint, unter Wine geprüft) eingebaut, um den Täter zu finden. Nebenbefund: Sprünge in fremde Funktionen (s08b: 5, d18a: 1) müssen noch zusammengeführt werden.
- **2026-10-23:** Achter Test mit `--watch-stack`: Täter gefunden – die Einstiegs-Hülle `rc_title_800D37F4` schrieb in den Slot ihres 8. Parameters, den der native Aufrufer (Funktionszeiger mit 1–2 Argumenten) nie übergeben hatte, und traf damit die Rücksprungadresse von `GV_ExecActorSystem`. Hüllen haben jetzt keine Parameter mehr und lesen die Argumente nur; Maschinencode-Scan: vorher 1962 von 5756 Hüllen betroffen, jetzt 0.
- **2026-10-23:** 🎮 **Meilenstein: Mit echter Disc läuft das Spiel unter Windows bis ins Titelmenü.** Offen: Meldung „SOUND WORK IS BROKEN“ nach Liedwechsel (Reihenfolge Sound-/Spiel-Task prüfen). Danach Sprünge zwischen Funktionen im Recompiler aufgelöst (Funktionen zusammengeführt; s08b, d18a), damit diese Stages nicht abstürzen.
- **2026-10-23:** Neunter Test: Musik im Menü läuft, „Select Game Level“ funktioniert mit ○ (Taste C). Absturz danach im Kopierschutz `SafetyCheck` (ruft BIOS-`exit` auf, weil keine gepresste Disc im Laufwerk) → im Port als bestanden gesetzt; allgemeine BIOS-Aufrufe aus recompilierten Overlays über `rc_bios`. Tastenbelegung in der Port-README dokumentiert.
- **2026-10-23:** Zehnter Test: Kopierschutz durch, Spiel lädt `d00a` (natives Overlay, 27 Charas). Absturz in `DG_SortChanl`: Der Spielcode macht aus 24-Bit-Verweisen direkt Zeiger (auf der PS1 gültige KUSEG-Adressen). `mgs_ptr24()` eingeführt und in `sort.c`, `libdg2.c` und dem PsyQ-Makro `nextPrim` eingesetzt. Offen: Ton stockt kurz zu Beginn.
- **2026-10-23:** Elfter Test: derselbe Absturz – auch die Listenköpfe in `DG_DivideBuffer` (von `divide.c` maskiert gespeichert) werden in `sort.c`/`libdg2.c` als Zeiger gelesen; jetzt ebenfalls über `mgs_ptr24()`.
- **2026-10-23:** Zwölfter Test: Zeichnen von `d00a` läuft, Stream startet. Absturz im GPU-Befehl A0 (Bild laden): Die Emulation las Breite×Höhe Pixel ohne Rücksicht auf die Paketlänge und lief über das RAM-Ende. Jetzt auf die vorhandenen Datenwörter begrenzt, Größen wie die Hardware (0 = 1024/512), erster Fall wird protokolliert.
- **2026-10-23:** 🎮 Dreizehnter Test: **Das Spiel startet und läuft ohne Absturz** (Intro d00a, Lippensynchronisation), aber mit vielen Grafikfehlern; GPU erhält Pakete, die keine Grafikbefehle sind (Füllmuster 0xBBBBBBBB). Screenshot-Funktion (F12, inkl. VRAM-Abbild) zur Analyse eingebaut.
- **2026-10-23:** Erste Screenshots: Konami-Logo korrekt (Texturen, Paletten, Rasterung funktionieren). Schwarze Leiste unten behoben: Das Spiel zeigt nur `screen.h` = 224 von 240 Zeilen des Anzeigebereichs. Intro-Text erscheint als gelbe Blöcke – Analyse über VRAM-Abbild läuft.
- **2026-10-23:** Ursache der Grafikfehler gefunden (Diagnose): Mein `mgs_ptr24`-Fix ließ `DG_SortChanl` volle Zeiger zurück in die Ordering Table schreiben; deren oberes Byte landete im Längenfeld des nächsten Pakets (0x8C statt 0x0C = 140 statt 12 Wörter), der GPU führte Fremddaten aus und überschrieb den Bildspeicher. Verweise werden wieder als 24 Bit gespeichert. Filmton stotterte: XA-Vorpuffer (100 ms) und Nachholen verpasster CD-Sektoren bis 1 s.
- **2026-10-23:** Grafik nach dem Fix deutlich besser. Absturz in `DG_AddSubdividedPrim`: `getaddr()` kürzt einen Zeiger auf 24 Bit (letzte solche Stelle, gezielt gesucht). Intro-Ton stotterte: Die Sprache im Intro ist ein SPU-Stream, dessen Nachladen der Treiber über SPU-Interrupts einer Taktstimme taktet; meine SPU fasste mehrere Interrupts pro Bild zu einem zusammen (Test: 55 statt 98 pro Sekunde). Jetzt hält das Mischen am Interrupt an, bis der Handler gelaufen ist; neuer Test `test_spu_irq`.
- **2026-10-23:** Abgleich mit anderen Projekten (`docs/verwandte-projekte.md`): Upstream unverändert (7bbcb7102), aber offene PRs #2434/#2437 vervollständigen alle Overlays. **RE:INTEGRAL** (Drommedhar) verfolgt dasselbe Ziel auf derselben Basis und ist weiter (v0.3, Kampagne durchgespielt, Windows, Godot). Zwei weitere Projekte recompilieren die US-Fassung. Richtungsentscheidung offen. Repo ist öffentlich; geprüft: keine Spieldaten, keine persönlichen Angaben, noch keine LICENSE-Datei.
- **2026-10-23:** Entscheidung: Option 3 (eigenständig, mit Abgleich gegen RE:INTEGRAL/PsyZ). MIT-Lizenz eingetragen (`LICENSE`, Geltungsbereich ohne Submodule/Spiel). Nachschlagequellen (PsyZ, RE:INTEGRAL-Patches) in `docs/verwandte-projekte.md`.
- **2026-10-23:** Abgleich mit RE:INTEGRAL, erster Fund: Reihenfolge der Argumentauswertung (PS1 links→rechts, x86 unbestimmt). Systematischer Scan: 28 Aufrufe (Funkrufe/Codec `MENU_RadioCall`, Funk-Nachrichten, Soundeffekte `GM_SeSet2`, 25× `GM_ConfigControlString` mit `GCL_GetOption`) auf feste Reihenfolge umgeschrieben; Test `evalorder` verhindert Rückfälle. Auswertung des großen RE:INTEGRAL-Patches (79 Dateien) läuft.
- **2026-10-23:** Auswertung des RE:INTEGRAL-Patches umgesetzt: HZD-Bodenzeiger mit Markierung im Bit 31 (`MGS_HZD`), Markierung der roten Lebensleiste in Bit 0, NULL-Zugriffe (Munition, `GM_PlayerControl`) und nicht initialisierte Richtung in `demoexec.c`. Windows-Testpaket v25.
- **2026-10-23:** Internetrecherche zum Thema in `docs/recherche.md`: ESP32-Port des Decomps (ausführliche Fehlerliste), RE:INTEGRAL-Fork „Reintegrated“, Werkzeuge (Rex, MINT, KMD-Noesis), Formatdoku (psx-spx STAGE.DIR), psxrecomp/RecompOne, rechtliche Regeln anderer Projekte, Abgleich mit unserem Stand.
- **2026-10-23:** Test v25: Ton im Intro passt. Spiel lädt `s00a` (Dock), erstes Codec-Gespräch mit Campbell läuft; Absturz beim Wechsel zurück ins Spiel. Ursache: Ohne Waffe ist `GM_Weapon` = -1, und `weapon_actions_8009ED8C[-1]` ist auf der PS1 die direkt davor liegende Tabelle `actions_no_weapon_8009ED70` (Linker-Anordnung); nativ war das fremder Speicher. Per Fixup explizit gemacht (v26).
- **2026-10-23:** 🎮 Test v26: **Spiel läuft nach dem ersten Codec-Gespräch im Dock (s00a) weiter**, Snake ist steuerbar.
- **2026-10-23:** Rückmeldung: Figuren pixeln stark, starkes Rauschen im Codec-Gespräch (Analyse läuft, Log/Screenshots angefragt). Nebenbefund behoben: `SpuGetVoiceAttr` lieferte die ADSR-Einzelwerte nicht (Log-Meldung „SOUND ERROR:SPU OFF ENV ON“) (v27).
- **2026-10-23:** Screenshots aus dem Dock: Figuren nah an der Kamera mit Zacken bzw. Splittern (falsche Vertices), Ursache noch offen. Differenztest der GTE gegen PsyZ (Zufallsregister, alle Befehle, libgte-Funktionen): Sättigungs-Flags von SX/SY und Farben falsch (ein Bit verschoben, Y setzte sogar das Z-Flag) und AVSZ-Sättigung korrigiert; Regressionstest ergänzt. Diagnose-Schalter `--no-divide` (Polygon-Unterteilung aus) zum Eingrenzen (v28).
- **2026-10-23:** Test v28: Zacken an Figuren unverändert mit `--no-divide` und `--render-scale 1`, Polygon-Unterteilung und Hochskalierung sind damit ausgeschlossen. Stand der Suche: GTE-Befehle stimmen mit PsyZ überein; Puffer-Index der Eltern-Vertices (`GV_Clock`) korrekt. Nächste Spur: Vertices, die Kind-Objekte vom Eltern-Objekt übernehmen (`DG_WriteObjVerticesIndirect`, Gelenke), und die Matrix-Verkettung in `libdg/screen.c`. Codec-Rauschen: Angaben vom Test noch offen. Kleinigkeit offen: IRGB-Lesen ohne Vorzeichen-Erweiterung von IR1–3 (`gte.c`).

