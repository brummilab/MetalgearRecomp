# Verwandte Projekte (Stand 23.10.2026)

Überblick, damit nicht doppelt gearbeitet wird. Alle Angaben laut den
jeweiligen READMEs bzw. Projektseiten zum genannten Datum.

| Projekt | Ansatz | Version / Spielfassung | Plattform | Stand |
| --- | --- | --- | --- | --- |
| **MetalgearRecomp** (dieses Repo) | Dekompilierter C-Code von FoxdieTeam, eigene Laufzeitschicht (Software-GPU, SPU, CD …), fehlende Overlays per eigenem statischem Recompiler | Integral JP (SLPM-86247) | Windows + Linux, 32 Bit, SDL2 | Erster Test mit echter Disc: Titel, Menü, Intro laufen |
| [**RE:INTEGRAL**](https://github.com/Drommedhar/mgs-reintegral) (Drommedhar) | Dekompilierter C-Code von FoxdieTeam + Plattformbibliothek **PsyZ**; Godot-Renderer und Modding-SDK, alternativ Legacy-Renderer (D3D12) | Integral JP, beide Discs | Windows (x86-Spielhost); Linux geplant | v0.3.0; Hauptkampagne am 10.10.2026 bis zum Abspann durchgespielt; MIT-Lizenz für eigenen Code |
| [**Midnight Ghost Stone**](https://github.com/coccofresco/mgsone-recomp) (coccofresco) | Statische Recompilierung (PS1Recomp), einzelne Routinen durch dekompilierten C-Code ersetzt; Vulkan-Darstellung mit Original-Assets | USA v1.0 (SLUS-00594/00776) | Linux, Windows | alpha.1 „UNTESTED“; GPL-3.0 |
| [**metal-gear-solid-recomp**](https://github.com/alexbeavs-ps1-ports/metal-gear-solid-recomp) (Alexbeav) | Statische Recompilierung mit PSXRecomp | laut Projektseiten MGS1 | Windows, Linux, macOS | v0.1.1, experimentell |
| [**FoxdieTeam/mgs_reversing**](https://github.com/FoxdieTeam/mgs_reversing) (Upstream) | Matching-Dekompilierung | Integral JP | PS1 | Offene PRs #2434 (alle übrigen Nicht-VR-Overlays inkl. s19b), #2435, #2437 (VR-Overlays) – nach dem Merge sind die Overlays, die wir recompilieren, als C-Code verfügbar |

## Folgerungen

- **RE:INTEGRAL verfolgt dasselbe Ziel auf derselben Grundlage und ist deutlich
  weiter** (komplette Kampagne spielbar). Unterschiede zu uns: Godot statt
  schlanker SDL-Port, derzeit nur Windows, eigene Plattformbibliothek PsyZ statt
  unserer Laufzeitschicht.
- **Unser Recompiler für unfertige Overlays wird überflüssig**, sobald die
  Upstream-PRs #2434/#2437 gemergt sind; der Port nimmt dekompilierten Code dann
  automatisch (Submodul aktualisieren).
- Die beiden Recompilierungs-Projekte zielen auf die **US-Fassung** und eine
  andere Technik (Recompilierung des ganzen Spiels statt dekompiliertem C).

## Entscheidung (23.10.2026): weitermachen und abgleichen

Dieses Repo wird eigenständig weitergeführt (schlanker SDL-Port, Windows und
Linux). Bei jedem neuen Problem wird **zuerst nachgesehen, wie RE:INTEGRAL bzw.
PsyZ es gelöst hat**, bevor eine eigene Lösung entsteht:

- **PsyZ** (PsyQ-Ersatz für native Builds, von Xeeynamo):
  https://github.com/Xeeynamo/psyz – RE:INTEGRAL pinnt Commit `6d9c5730`.
- **RE:INTEGRAL-Patches** auf mgs_reversing und PsyZ (Verzeichnis `patches/`,
  Liste in `sources.lock.json`): Die Dateinamen benennen das Thema, z. B.
  `mgs-title`, `mgs-intro`, `mgs-intro-playback`, `mgs-movies`,
  `mgs-widescreen`, `mgs-loading-pacing`, `mgs-mantis`, `mgs-disc-change`,
  `psyz-cd`, `psyz-audio`, `psyz-movies`, `psyz-xa-predictor-rounding`,
  `psyz-loading-audio`, `psyz-spu-sweeps`, `psyz-live-adsr`,
  `psyz-polygon-rasterization`, `psyz-masking`.
- RE:INTEGRAL nutzt ein neueres mgs_reversing (Commit `f54dbb2a`).
- Lizenzen beachten: Übernahmen nur mit passender Lizenz und Quellenangabe;
  RE:INTEGRALs eigener Code steht unter MIT, die Patches auf fremden Code und
  PsyZ haben eigene Bedingungen (siehe deren THIRD_PARTY-Dokumentation).

Erwogene Richtungen:

1. **Eigenständig mit klarer Nische**: schlanker, quelloffener SDL-Port ohne
   Godot, **Linux und Windows** aus einer Codebasis (RE:INTEGRAL: Linux erst
   geplant), Englisch erzwungen, 60 fps, Breitbild.
2. **Bei RE:INTEGRAL mitarbeiten**, z. B. Linux-Unterstützung beisteuern, und
   dieses Repo als Experiment abschließen.
3. **Teile übernehmen bzw. abgleichen**: Laufzeit-Fixes (z. B. KUSEG-Zeiger,
   SPU-Interrupt-Takt) mit PsyZ/RE:INTEGRAL vergleichen, statt jedes Verhalten
   selbst neu zu finden.
