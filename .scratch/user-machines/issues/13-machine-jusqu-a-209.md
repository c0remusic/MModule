# Machine jusqu'à 209, et USR dans le Kit

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 12

## Question

Deuxième tranche, comme « Le paramètre Machine et USR-01 à USR-16 » l'a décidé : le paramètre Machine (et
FocusMachine) va jusqu'à 209, USR-01 à USR-16 aux numéros d'X.20 (194 à 209, 192 et 193 sans Machine) ; la table des
Machines (`mdProtocol/mdmachines.*`) les nomme, par leur nom de MDM1 une fois installées ; le Device lit et écrit leurs
numéros dans le dump de Kit et dans `$5B` (le bit c, modèle + 128) ; USR vide joue muet. Tests : un Kit qui porte
USR-05 passe le dump aller-retour, `$5B` l'assigne, `mdDrumsProcessorTest` règle Machine à 209 comme un hôte et le
relit, `mdDrumsVst3HostTest` voit le nouveau maximum. Les notes de 0.1.2 diront le décalage de l'automation de
Machine des sets 0.1.x.

## Answer

Construit le 2026-10-17 :

- Machine et FocusMachine vont jusqu'à 209 (`parameterDescriptions_mddrums.json`) ; la liste de valeurs nomme 192 et
  193 « #192 », « #193 », puis USR-01 à USR-16. La table des Machines (`mdProtocol/mdmachines.cpp`) les a, famille
  USR, montrées quand un Kit les tient, pas encore proposées par l'éditeur (le nom de MDM1 viendra avec la Library).
- `KitChange::LastMachine` vaut 209 : `$5B` (numéro 66 à 81, bit c) et le dump de Kit portent les Machines
  utilisateur. Le Device prend un numéro USR installé ou non : `mdDrums::Engine::machineDefaults` donne les défauts
  du paquet, ou 0 pour un numéro vide, qui joue muet ; le Kit joué du Controller le garde.
- Vérifié : `mdProtocolKitTest` (aller-retour de toutes les Machines jusqu'à 209, 21 744 changements ; un Kit avec
  USR-05 revient de son dump ; `$5B` de USR-05 donne 198, 210 refusé), `mdDrumsDeviceTest` (USR-05 par `$5B` : SYN1-8
  à 0, la page d'effets comme OS 1.63, les 24 rapportés), `mdDrumsProcessorTest` (un hôte règle Machine à 209, le
  Device le prend).
