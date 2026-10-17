# Booter X.20 dans mdLib

Type: task
Status:
Blocked by:

## Question

X.20 peut-il servir de référence comme OS 1.63 aujourd'hui (`mdEngineFirmwareTest`) ? Poser l'OS X.20 de
l'utilisateur sur son flash image 1.63, à la manière de `mdosvariant` dans la pull request 100 de
joelanders/gearmulator-md-mm (OS décodé des messages `$7E`, posé sous `$200000`), le booter dans mdLib sous garde
d'horloge, y installer une Machine utilisateur par le SysEx `$74` du zip (TX8-BD), l'assigner à une Track par `$5B`,
la jouer et enregistrer sa sortie. Résolu quand X.20 boote et qu'un Hit de TX8-BD sort, ou quand on sait pourquoi
pas. Rien de l'OS X.20 ni du zip n'entre dans le dépôt : les fichiers sont lus par variable d'environnement. Limité à
une session.
