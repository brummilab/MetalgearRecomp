# MetalgearRecomp

Arbeitsrepo für den Decomp von **Metal Gear Solid** (PlayStation, 1998).

Statt bei null anzufangen, baut dieses Repo auf dem Community-Projekt
[FoxdieTeam/mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing) auf.
Dort ist *Metal Gear Solid Integral* (JP) bereits weit dekompiliert:

| Ziel | Stand (Upstream, Okt. 2026) |
| --- | --- |
| `SLPM_862.47` / `SLPM_862.48` – Hauptprogramm Disc 1/2 | ✔️ 100 % dekompiliert, matching |
| `SLPM_862.49` – Hauptprogramm VR-Disc | ✔️ 100 % dekompiliert, matching |
| Stage-Overlays (`s00a` … `s20a`) | 🚧 teilweise |
| VR-Overlays (`vr_*`, `vab_*`, …) | ❌ größtenteils offen |

Hier liegt die eigene Arbeit (Skripte, Doku, Notizen); der eigentliche
Quellcode und die Toolchain sind als Git-Submodule eingebunden.

## Aufbau

```
MetalgearRecomp/
├── upstream/
│   ├── mgs_reversing/   # Submodul: Decomp-Quellcode, Assembler, Build-System
│   └── psyq_sdk/        # Submodul: PsyQ-Compiler/-Linker (Sony PS1 SDK)
├── scripts/
│   ├── setup.sh         # Umgebung einrichten (Wine, venv, Submodule)
│   └── build.sh         # Bauen + Hash-Check gegen das Original
├── docs/
│   └── workflow.md      # Anleitung: eine Funktion dekompilieren
└── Projektinhalt.md     # Projektübersicht und Verlauf
```

## Schnellstart (Linux / WSL)

```bash
git clone --recursive https://github.com/brummilab/MetalgearRecomp.git
cd MetalgearRecomp
scripts/setup.sh
scripts/build.sh              # SLPM_862.47, Disc 1/2
scripts/build.sh vr_exe       # SLPM_862.49, VR-Disc
scripts/build.sh dev_exe      # Dev-Build zum Experimentieren
```

Ein erfolgreicher Build endet mit einem bestandenen Hash-Check: Die gebaute
Datei ist bitgenau identisch mit der Originaldatei. Die Ausgabe liegt in
`upstream/mgs_reversing/obj/`.

Zum Bauen ist **keine** Spiel-ISO nötig. Zum Spielen bzw. Testen eines
Dev-Builds braucht man eine eigene Kopie von *MGS Integral* (SLPM-86247) und den
Emulator [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux):

```bash
cd upstream/mgs_reversing/build
../../../.venv/bin/python run.py --iso <pfad/zur/iso> --pcsx-redux <pcsx-redux-ordner>
```

### Hinweise

- Die PsyQ-Compiler sind 32-Bit-Windows-Programme und laufen unter Linux über
  Wine. Wine stürzt bei vielen parallelen Compiler-Prozessen gelegentlich ab;
  `scripts/build.sh` baut deshalb mit `JOBS=2` weiter (anpassbar, z. B.
  `JOBS=4 scripts/build.sh`).
- Unter Windows kann direkt nach der Upstream-Anleitung gebaut werden
  (`python build.py` in `upstream/mgs_reversing/build`).

## Mitarbeiten

Siehe [docs/workflow.md](docs/workflow.md). Kurzfassung: Funktion aus
`asm/overlays/` wählen, auf [decomp.me](https://decomp.me/preset/19) matchen,
C-Code übernehmen, bauen, Pull Request an upstream.

## Upstream aktualisieren

```bash
git submodule update --remote upstream/mgs_reversing
git add upstream/mgs_reversing && git commit -m "Upstream aktualisiert"
```

## Rechtliches

Dieses Repo enthält **keine** Spieldaten (ISO, Executables, Assets) – diese sind
urheberrechtlich geschützt (© Konami) und dürfen nicht eingecheckt werden
(`.gitignore` blockiert die üblichen Formate). Für eigene Tests braucht man eine
legal erworbene Kopie des Spiels.

Das Upstream-Projekt `mgs_reversing` hat derzeit keine ausgewiesene Lizenz.
