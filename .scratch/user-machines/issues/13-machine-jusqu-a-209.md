# Machine jusqu'à 209, et USR dans le Kit

Type: task
Status:
Blocked by: 12

## Question

Deuxième tranche, comme « Le paramètre Machine et USR-01 à USR-16 » l'a décidé : le paramètre Machine (et
FocusMachine) va jusqu'à 209, USR-01 à USR-16 aux numéros d'X.20 (194 à 209, 192 et 193 sans Machine) ; la table des
Machines (`mdProtocol/mdmachines.*`) les nomme, par leur nom de MDM1 une fois installées ; le Device lit et écrit leurs
numéros dans le dump de Kit et dans `$5B` (le bit c, modèle + 128) ; USR vide joue muet. Tests : un Kit qui porte
USR-05 passe le dump aller-retour, `$5B` l'assigne, `mdDrumsProcessorTest` règle Machine à 209 comme un hôte et le
relit, `mdDrumsVst3HostTest` voit le nouveau maximum. Les notes de 0.1.2 diront le décalage de l'automation de
Machine des sets 0.1.x.
