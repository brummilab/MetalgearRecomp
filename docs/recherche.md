# Recherche: Metal Gear Solid (PS1) – Projekte, Werkzeuge, Grundlagen

*Stand: 23.10.2026*

Diese Seite ergänzt die bereits dokumentierten Quellen: RE:INTEGRAL/PsyZ, Midnight Ghost Stone, alexbeavs `metal-gear-solid-recomp`, die Upstream-PRs #2434/#2435/#2437 und das decomp.me-Preset 19. Die Daten zur letzten Aktivität stammen aus der GitHub-API bzw. von den Projektseiten. Wo die Lizenz „–“ steht, hat GitHub keine Lizenz erkannt. Unsichere Angaben sind mit *(unsicher)* markiert.

---

## 1. Andere MGS1-Projekte

### Ports, Forks und Recomps

- **mgs_reversing, ESP32-S3-Port** (David Montero Crespo): [Branch `esp32-port`](https://github.com/davidmonterocrespo24/mgs_reversing/tree/esp32-port), [Write-up](https://velxio.dev/blog/posts/metal-gear-solid-on-esp32-s3/) (27.09.2026), [Hackaday](https://hackaday.com/2026/09/29/metal-gear-solid-moves-from-playstation-to-esp32/).
  - Der Decomp-C-Code wird nativ für Xtensa übersetzt, auf einer eigenen PsyZ-Variante ([davidmonterocrespo24/psyz](https://github.com/davidmonterocrespo24/psyz)).
  - Software-Rasterizer, mts-Scheduler auf FreeRTOS, virtuelles CD-Laufwerk auf microSD.
  - Noch nicht umgesetzt: Streaming, MDEC, Audio und Memory Card.
  - **Für uns relevant:** Es ist die ausführlichste öffentliche Fehlerliste für einen nativen MGS1-Port (Details unter Abschnitt 3 und 5).
  - Lizenz: –. Das Write-up betont, dass keine Spieldaten enthalten sind.
- **MGS1: Reintegrated** ([ImperishableNEET/mgs1-reintegrated](https://github.com/ImperishableNEET/mgs1-reintegrated)):
  - Experimenteller Fork von RE:INTEGRAL mit Fokus auf Portabilität und optionale Verbesserungen.
  - Aktuell läuft nur Windows x86 (32 Bit). x86-64 und ARM64 für Windows/Linux sind als Ziel genannt.
  - Wählbares Renderer-Modul (`-RendererModule renderers/modern`).
  - Prerelease `v0.2.0-reintegrated.5`, letzter Commit 14.10.2026.
  - **Für uns relevant:** Zeigt, wie weit die 32-Bit-Annahmen in RE:INTEGRAL reichen. Wir bleiben vorerst ebenfalls bei 32 Bit (PS1-RAM fest bei 0x80000000).
  - Lizenz: übernimmt die Lizenzhinweise von RE:INTEGRAL.
- **mgs1-browser-codec** ([ImperishableNEET/mgs1-browser-codec](https://github.com/ImperishableNEET/mgs1-browser-codec)): Codec-Bildschirm im Browser, aus dem Decomp-C nach WebAssembly übersetzt, mit lokalem Importer für Disc-Daten. Aktualisiert 12.10.2026.
  - **Für uns relevant:** Ein isolierter Testfall für RADIO.DAT und den Codec.
- **mgs1_voice_player** ([teopiaz/mgs1_voice_player](https://github.com/teopiaz/mgs1_voice_player)): Spielt Codec-Gespräche und FMVs der PSX-Fassung ab, in C. Aktualisiert 23.10.2026.
  - **Für uns relevant:** Referenz zum Abgleich unserer VOX-, XA- und STR-Dekodierung.
- **esp-cpp/tab5-emu** ([Repo](https://github.com/esp-cpp/tab5-emu)): Lizenz MIT. Führt MGS auf dem ESP32-P4 (M5Stack Tab5) als unterstützt. Ob nativ oder emuliert, ist *(unsicher)*.
- **silver-gear** ([daetrox/silver-gear](https://github.com/daetrox/silver-gear)): Launcher auf Basis von SwanStation für Linux und Windows, mit 60 Hz, 16:9 und Achievements. Lizenz GPL-3.0. Das ist ein Emulator und nur Kontext, kein nativer Port.

### Übersetzungen und Mods

Alle diese Projekte sind Patch-Skripte ohne Spieldaten. Sie sind nützlich, um die Text- und Font-Pfade zu verstehen.

- [DoktorDeSparkle/mgs1-translation-toolkit](https://github.com/DoktorDeSparkle/mgs1-translation-toolkit)
- [soaresden/MGS1-VOSTFR](https://github.com/soaresden/MGS1-VOSTFR) (Französisch)
- [solidcrash/MGS1-ENG-ITA](https://github.com/solidcrash/MGS1-ENG-ITA) (Italienisch)
- [Hieronymouss/mgs1-ptbr-patcher](https://github.com/Hieronymouss/mgs1-ptbr-patcher) (Portugiesisch, BPS-Patcher)

### Kontext: PC-Port von 2000 und Master Collection

- **MGSM2Fix** ([nuggslet/MGSM2Fix](https://github.com/nuggslet/MGSM2Fix), [Nexus](https://www.nexusmods.com/metalgearsolidmc/mods/5)): Widescreen, interne Auflösung und Fenstermodus für MGS1 in der Master Collection.
- **MGSHDFix** ([ShizCalev/MGSHDFix](https://github.com/ShizCalev/MGSHDFix)): das Gegenstück für MGS2/3.
- **[PCGamingWiki](https://www.pcgamingwiki.com/wiki/Metal_Gear_Solid_-_Master_Collection_Version)**: Die Master Collection ist auf 30 FPS (NTSC) bzw. 25 FPS (PAL) begrenzt. Das bestätigt, dass die Spiellogik an die Bildrate gebunden ist.
- **[Hacker News](https://news.ycombinator.com/item?id=9739731)**: Ein ehemaliger Entwickler des PC-Ports von 1999/2000 berichtet aus der Portierung, z. B. dass die Modelle kein Skinning verwenden. Das ist eine Anekdote, keine verifizierte Quelle.

### Community

- **FoxdieTeam-Discord:** [discord.gg/tTvhQ8w](https://discord.gg/tTvhQ8w), verlinkt aus dem README von mgs_reversing.
- **Stand von mgs_reversing:** Die Hauptprogramme SLPM_862.47/48/49 sind zu 100 % dekompiliert, die Overlays sind noch in Arbeit. Letzter Push 22.10.2026. Das Repo hat keine Lizenzdatei.
- **R.A.I.D.-Discord** (psxrecomp-Umfeld): [discord.gg/Ad9BwSzctP](https://discord.gg/Ad9BwSzctP).

---

## 2. Werkzeuge und Formatdokumentation

- **psx-spx: STAGE.DIR und *.DAT (Metal Gear Solid)** ([Seite](https://problemkaputt.de/psxspx-cdrom-file-archive-stage-dir-and-dat-metal-gear-solid.htm)):
  - Dateiliste mit Größen je Release (Retail-STAGE.DIR: 42AE000h).
  - Liste der Typkürzel: `.sb` steht für MIPS-Code, `.nd`/`.cp` für PCX, `.cg` für GCX, `.ck` für KMD, `.co` für OAR, `.cz` für ZMD, `.cl` für Licht, `.sw` für Wave usw.
  - Ausrichtung auf 800h, angepasstes PCX-Format, Hinweise zu FACE.DAT, DEMO.DAT und VOX.DAT.
  - **Für uns relevant:** Die beste kompakte Referenz für Loader und Asset-Prüfung.
- **Rex** ([Jayveer/Rex](https://github.com/Jayveer/Rex)): Entpackt STAGE.DIR und DAR. Lizenz –, letzter Push 01.09.2021.
- **MGS-KMD-Noesis** ([Jayveer/MGS-KMD-Noesis](https://github.com/Jayveer/MGS-KMD-Noesis)): Noesis-Plugin für KMD-Modelle und OAR-Animationen. Lizenz –, letzter Push 27.03.2021.
  - Laut [ResHax](https://reshax.com/topic/19040-ps1-metal-gear-solid-kmd/) gibt es Probleme mit der Polygon-Orientierung (Face Winding).
  - **Für uns relevant:** Zweitmeinung beim Debuggen von Modellen, Skeletten und Animationen.
- **Gcx** ([Jayveer/Gcx](https://github.com/Jayveer/Gcx)): Dekompiliert GCX-Bytecode zurück in GCL-Skript. Ausgelegt auf MGS3/4, für MGS1 nur eingeschränkt nutzbar *(unsicher)*. Lizenz –, letzter Push 24.02.2021.
- **MINT** ([virusek20/mint](https://github.com/virusek20/mint)): CLI für DAR, PCX, KMD, DIR, STG und VOX (VOX nur PC-Fassung, nur Export). Importiert glTF, PNG und JPG. Lizenz MIT, letzter Commit 29.06.2026.
  - **Für uns relevant:** Die einzige Lösung mit Import-Richtung, also Grundlage für Mod-Support.
- **mgs1-scripts** ([DoktorDeSparkle/mgs1-scripts](https://github.com/DoktorDeSparkle/mgs1-scripts)): Python-Skripte zum Reverse Engineering von MGS1. Dazu gehören ein Editor für Stages und einer für Dialoge (demo, zmovie, vox als JSON). Aktualisiert 07.09.2026, Lizenz nicht angegeben.
- **[TCRF: Metal Gear Solid (PlayStation)](https://tcrf.net/Metal_Gear_Solid_(PlayStation))**: Unbenutzte Inhalte und Hinweise zu den Dateidaten. Nützlich für Testfälle wie Debug-Menü und Demo-Auswahl.
- **[Joy-Division/KCEJ-Wiki](https://github.com/Joy-Division/KCEJ-Wiki)**: Technisches Wiki zu KCEJ-Spielen, u. a. MGS1 und MGS HD (z. B. `radio.dat` und `codec.dat`). Archiviert seit 24.07.2024.
- **[jPSXdec, Dokumentation des STR-Formats](https://github.com/m35/jpsxdec/blob/readme/jpsxdec/PlayStation1_STR_format.txt)**: Referenz für ZMOVIE.STR und MDEC.

---

## 3. Bausteine für native PS1-Ports

### PsyZ

[Xeeynamo/psyz](https://github.com/Xeeynamo/psyz) ist ein Drop-in-Ersatz für PSY-Q mit GPU- und SPU-Schicht.

- **Plattformen:** Linux, Windows (MSVC, Clang, MinGW), macOS/iOS, WebAssembly, PSP.
- **Nutzer:** SotN, FF7, Rayman, MMX4, Rage Racer und MGS1.
- **Stand:** Lizenz „Other“, letzter Push 22.10.2026.
- **Zum OT-Layout:** Laut dem ESP32-Write-up nutzt PsyZ für den OT-Tag zwei Wörter (`tag` und `len`). Jedes Primitiv wird dadurch 4 Byte größer.

### Statische Recompiler

- **psxrecomp** ([RetroPortingToolKit/psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp)): übersetzt MIPS nach C.
  - Overlays werden vorab in Shards übersetzt und per Byte-Guard ausgewählt. Lücken schließt ein Capture-and-compile-Weg (gcc/tcc), dazwischen läuft ein Interpreter als Fallback.
  - Die Hardware wird über MMIO-Handler nachgebildet. Renderer: OpenGL, Vulkan oder Software.
  - Laut README wird die genaue Zeitsteuerung der SPU-IRQs noch nachgeschärft.
  - Lizenz: PolyForm Noncommercial 1.0.0. Letzter Push 23.10.2026.
  - **Für uns relevant:** Vorlage für den Umgang mit Overlays in unserem Recompiler.
- **RecompOne** ([BlackLabelHQ/RecompOne](https://github.com/BlackLabelHQ/RecompOne)): Recompiler nach C# mit eigener Runtime, Lizenz MIT. Darauf basiert die native SotN-Fassung („SymphonyRecomp“).
- **ps1-recomp** ([PS1Recomp/ps1-recomp](https://github.com/PS1Recomp/ps1-recomp)): übersetzt nach C++, Lizenz „Other“, letzter Push 12.10.2026.

### Decomp-zu-PC-Ports mit Vergleichswert

- **sotn-decomp** ([Xeeynamo/sotn-decomp](https://github.com/Xeeynamo/sotn-decomp)): AGPL-3.0, Basis von PsyZ.
- **Silent-Hill-PC-Port** ([SlickAmogus/silent-hill-decomp](https://github.com/SlickAmogus/silent-hill-decomp)): baut auf „Psycross“ auf. Laut Suchtreffer steht der PC-Teil unter GPL-3.0 *(Lizenzumfang unsicher)*.

### Spezifikationen und SDKs

- **psx-spx / nocash:** [problemkaputt.de/psx-spx.htm](https://problemkaputt.de/psx-spx.htm) und die gepflegte Fassung [psx-spx.consoledev.net](https://psx-spx.consoledev.net/) (Repo zuletzt 08.10.2026). Referenz für GTE, GPU, SPU-ADPCM/Reverb, CD-XA und MDEC.
- **[Rodrigo Copetti: PlayStation Architecture](https://www.copetti.org/writings/consoles/playstation/)**: Überblick mit vielen Quellenverweisen.
- **[PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK)**: Offenes PS1-SDK (Lizenz „Other“, letzter Push 28.01.2025). Gut lesbarer Referenzcode für libgpu, libcd und libspu.
- **PSY-Q:**
  - [FoxdieTeam/psyq_sdk](https://github.com/FoxdieTeam/psyq_sdk) ist die Build-Abhängigkeit des Decomps.
  - Für die Einrichtung: [psx.dev Getting started](https://www.psx.dev/getting-started) und [Retro Reversing: PSYQ setup](https://www.retroreversing.com/psyq-sdk-setup).
  - Die Original-SDKs sind urheberrechtlich geschützt. Wir verweisen nur auf sie und liefern sie nicht mit.

---

## 4. Rechtliche und ethische Hinweise anderer Projekte

Fast alle genannten Projekte formulieren dieselben Regeln:

- **Eigene Disc:** Nutzer liefern ihre eigene Disc bzw. ihr eigenes Image. Beispiele:
  - ESP32-Port: `port/extract_disc.py` liest die Dateien aus dem eigenen Image.
  - Reintegrated: „supply your own Integral CUE/BIN“.
- **Keine Spieldaten:** keine Disc-Images, keine extrahierten Dateien, kein BIOS, kein kommerzielles SDK, keine Spielstände (so wörtlich bei Reintegrated).
- **Keine Zugehörigkeit:** ausdrücklich keine Verbindung zu Konami bzw. Sony (Reintegrated, RecompOne).
- **Maschinell übersetzter Code:** psxrecomp weist darauf hin, dass Release-Builds maschinell übersetzten Originalcode enthalten, und wählt deshalb eine nicht-kommerzielle Lizenz. Dieser Punkt ist für unsere Recompiler-Ausgabe relevant.
- **Lizenzlage beim Decomp:** mgs_reversing hat keine Lizenzdatei. Bei Übernahmen aus dem Decomp sollten wir das offen benennen.

---

## 5. Was wir daraus mitnehmen

1. **OT-Layout zentral prüfen.** Bei einem OT-Tag aus zwei Wörtern verschieben sich alle Felder nach dem Tag. Beim ESP32-Port betroffen:
   - `SetDrawStp`: Der Befehl wurde in das Wort geschrieben, das jetzt `len` enthält, statt nach `code[0]`.
   - `DG_MakePrim` hat `len` nicht gesetzt (Formel dort: `psize/4 - 2`).
   - `DG_PrimInfos`: Der erste Vertex liegt bei +12 statt +8.
   - `LinkModelToParent`: Offsets müssen `*56 + 12` statt `*52 + 8` lauten.
   - Die Radix-Sortierung in `divide.c`/`sort.c` legt das High-Byte der Tiefe im Längenbyte ab.

   Wir sollten alle Stellen finden, die das gepackte Layout stillschweigend voraussetzen.
2. **Zeigertests über das Vorzeichenbit.** GCL unterscheidet Block-Zeiger von Proc-IDs über `proc_id < 0`. Auf PSX-RAM (0x8xxxxxxx) funktioniert das, mit Host-Zeigern nicht. Alle Stellen dieses Musters sollten wir prüfen, nicht nur die erste, die auffällt.
3. **Compiler-Absicherung.** Strict Aliasing hat in `CompMatrix` die Rotation wegoptimiert, ein zweiter Fall steckt in `libdg/display.c`. Deshalb global `-fno-strict-aliasing` setzen. Divisionen durch berechnete Werte absichern, denn MIPS läuft bei Division durch null weiter.
4. **GTE und libgte gezielt testen.**
   - `Square0` verschiebt nicht, nur `Square12` verschiebt um 12 Bit.
   - Die MVMVA-Opcodes für RTV0–2 und LL/LLV0 stammen aus der richtigen Zeile von `inline_n.h`.
   - Rotationen in zwei verschiedenen Frames vergleichen. In der Ruhepose sind alle Rotationen Einheitsmatrizen, ein Test dort fällt also nie durch.
5. **GPU-Regel übernehmen.** Die PSX-GPU verwirft Polygone, die größer als 1023×511 sind, und MGS verlässt sich darauf. Der Test gehört vor das Clipping, sonst verschwindet genau die geprüfte Eigenschaft.
6. **Zeitbasis.** `VSync(-1)` muss den vollen 32-Bit-Zähler liefern. Bei 16 Bit hängt das Spiel nach 17,6 Minuten. Tasten-Halte und Frame-Dropper gehören zum Spiel-Frame, nicht zum VBlank. Langzeittests über 20 Minuten hinaus einplanen.
7. **CD-Semantik.** `CDBIOS_ReadRequest` liefert sektorweise und asynchron, und der Callback darf die Restgröße umschreiben (`FS_CdStageFileInit` braucht das). Für DEMO.DAT, VOX.DAT und XA muss echtes Ring-Buffer-Streaming laufen, sonst hängt der Stage-Wechsel.
8. **Skript und Codec sind verknüpft.** Das Dock (s00a) wird erst im zweiten Durchlauf des Skripts aufgebaut, und den löst das Annehmen des Codec-Anrufs aus. Ein Stub für Codec bzw. RADIO.DAT legt also die Story lahm. Jeder Stub braucht eine Markierung, die uns zwingt, ihn später wieder anzufassen.
9. **Scratchpad und Puffer.** 346 feste Adressen der Form `0x1f8000xx` lagen in 16 Dateien verteilt. Der Scratch-Puffer von `DG_PcxRead4Bpp` überlappte im ESP32-Port mit Paketen, die noch in Gebrauch waren. Die Überlappung sollten wir mit Watchpoints bzw. ASan prüfen.
10. **Overlays.** Das ESP32-Projekt nimmt notgedrungen nur Stages mit symbolischen Actor-Tabellen auf (56 Stages, 149 Klassen). Für die übrigen Overlays ist psxrecomp mit Byte-Guards und Interpreter-Fallback die erprobte Vorlage für unseren Recompiler.
11. **Werkzeuge für Tests nutzen.** Mit Rex und MINT können wir Asset-Prüfsummen und Golden-Tests für unseren Loader erzeugen. teopiaz' Voice-Player taugt als Referenz für VOX/XA und STR. Im Repo landen weiterhin nur Werkzeuge, keine Daten.

---

## 6. Abgleich mit unserem Stand (23.10.2026)

| Punkt | Stand bei uns |
| --- | --- |
| OT-Layout (Punkt 1) | Wir behalten das Original-Layout mit einem Wort (`tag`, 24-Bit-Verweis + Länge). Die Verschiebungen aus dem ESP32-Port betreffen uns nicht. Volle Zeiger dürfen aber nie in die OT zurückgeschrieben werden (siehe Verlauf, 13. Test). |
| Vorzeichenbit-Tests (Punkt 2) | Teilweise erledigt: HZD-Bodenzeiger mit Bit 31 als Markierung (`MGS_HZD`) und die rote Lebensleiste (Markierung jetzt in Bit 0) werden nach RE:INTEGRAL korrigiert. Ein vollständiger Scan steht noch aus. |
| `-fno-strict-aliasing` (Punkt 3) | Ist für Spielcode, Laufzeitschicht und recompilierte Overlays gesetzt, `-fwrapv` ebenfalls. |
| Division durch null (Punkt 3) | Offen, noch nicht geprüft. |
| GTE (Punkt 4) | Offen: Gezielte Tests für `Square0`/`Square12` und MVMVA ergänzen. |
| Große Polygone verwerfen (Punkt 5) | Offen, in der Software-GPU prüfen. |
| `VSync(-1)` (Punkt 6) | Unser Zähler hat 32 Bit. |
| CD-Streaming (Punkt 7) | Asynchron, sektorweise, mit Nachholen von bis zu 1 s. |
| Codec/RADIO.DAT (Punkt 8) | Nicht als Stub, sondern als Originalcode übernommen. |
| Overlays (Punkt 10) | Eigener statischer Recompiler (`port/tools/recomp.py`). Nach dem Merge der Upstream-PRs #2434/#2437 wird er überflüssig. |

