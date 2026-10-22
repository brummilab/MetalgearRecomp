# Workflow: eine Funktion dekompilieren

Der Decomp ist **matching**: Der C-Code muss mit dem Original-Compiler (PsyQ 4.4,
GCC 2.8-basiert) exakt dieselben Maschinenbefehle erzeugen wie das Spiel.

## 1. Funktion aussuchen

Noch nicht dekompilierte Funktionen liegen als Assembler in
`upstream/mgs_reversing/asm/overlays/<overlay>/`. Für den Einstieg eine kleine
`.s`-Datei wählen.

Vorher prüfen, ob die Funktion schon in einem anderen Overlay existiert –
viele Overlays teilen Code (z. B. gibt es `s11g_blastoff_800D4744` bereits als
`blastoff.c`).

## 2. Scratch auf decomp.me anlegen

1. Auf [decomp.me](https://decomp.me/) nach dem Funktionsnamen suchen.
2. Falls nicht vorhanden:
   ```bash
   cd upstream/mgs_reversing/build
   ../../../.venv/bin/python decompme_asm.py ../asm/overlays/<overlay>/<funktion>.s
   ```
   Das kopiert die Instruktionen in die Zwischenablage.
3. Neuen Scratch mit dem Preset **Metal Gear Solid (overlays)** anlegen und als
   Kontext den Kontext eines bestehenden Scratches aus
   [Preset 19](https://decomp.me/preset/19) übernehmen.

## 3. Matchen

decomp.me liefert einen ersten C-Entwurf. Diesen so lange anpassen, bis der
Diff 100 % zeigt (Variablenreihenfolge, Typen, Schleifenform, Temporärvariablen
spielen alle eine Rolle).

## 4. In den Code übernehmen

1. Den `INCLUDE_ASM`-Eintrag in der passenden `.c`-Datei unter
   `upstream/mgs_reversing/source/` durch den C-Code ersetzen.
2. Die `.s`-Datei löschen.
3. `scripts/build.sh` – am Ende muss der Hash-Check **OK** melden.

## 5. Beitragen

Änderungen im Submodul werden über einen Fork von `FoxdieTeam/mgs_reversing`
und einen Pull Request upstream eingereicht. Danach den Submodul-Zeiger in
diesem Repo aktualisieren:

```bash
git -C upstream/mgs_reversing pull origin master
git add upstream/mgs_reversing
git commit -m "Upstream aktualisiert"
```

## Hilfe

Discord der FoxdieTeam-Community: <https://discord.gg/tTvhQ8w>, Kanal `#metal_gear_dev`.
