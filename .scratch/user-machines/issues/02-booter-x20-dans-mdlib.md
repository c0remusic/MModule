# Booter X.20 dans mdLib

Type: task
Status: resolved (on sait pourquoi pas)
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

X.20 peut-il servir de référence comme OS 1.63 aujourd'hui (`mdEngineFirmwareTest`) ? Poser l'OS X.20 de
l'utilisateur sur son flash image 1.63, à la manière de `mdosvariant` dans la pull request 100 de
joelanders/gearmulator-md-mm (OS décodé des messages `$7E`, posé sous `$200000`), le booter dans mdLib sous garde
d'horloge, y installer une Machine utilisateur par le SysEx `$74` du zip (TX8-BD), l'assigner à une Track par `$5B`,
la jouer et enregistrer sa sortie. Résolu quand X.20 boote et qu'un Hit de TX8-BD sort, ou quand on sait pourquoi
pas. Rien de l'OS X.20 ni du zip n'entre dans le dépôt : les fichiers sont lus par variable d'environnement. Limité à
une session.

## Answer

Fait le 2026-10-17 : [research/02-booting-x20.md](../research/02-booting-x20.md).

- **X.20 ne boote pas encore.** Son chargeur va au bout (les deux DSP chargés, l'OS dépaqueté vers 21,7 s d'audio),
  puis l'OS s'arrête à son étape de démarrage 7, vers 23,5 s, sur une exception à `$2A6DE4` : une instruction de
  l'unité MAC du ColdFire MCF5206e, que Musashi n'émule pas. L'OS s'en sert à environ 290 endroits.
- **Trois manques de nos émulateurs passaient avant, corrigés sur la branche** : CMPU et EOR #xxxx que le JIT du
  DSP56300 compilait en rien, et le push d'un octet sur la pile du ColdFire, que Musashi décalait de deux comme un
  68000. Ce dernier touche aussi le moteur de MoDule ; les 9 tests firmware et moteur du garde-fou passent avec, les
  tests du plug-in n'ont pas tourné.
- **Rien du son n'a donc été atteint** : ni TX8-BD, ni la conversion Pitch. L'outil (`mdX20BootTest`, `--play`,
  `--pitch`, OS lu par `MD_X20_OS_SYX`) les fera dès qu'X.20 prendra le MIDI.
- **Où est le code** : worktree `C:\dev\MModule-x20`, branche `x20-boot` (f783147 sur cb44f07), sous-modules dsp56300
  0e7b9234 et mc68k 69939c8, en branches `x20-boot` locales ; rien de poussé.

La suite (émuler l'unité MAC, puis ce qu'X.20 rencontre ensuite, inconnu) est une décision :
[La référence du son après « Booter X.20 »](09-reference-du-son.md).
