# Décoder l'OS X.20 et y trouver ses Machines intégrées

Type: research
Status:
Blocked by:

## Question

Que coûte d'extraire de l'OS X.20 les 8 Machines intégrées qu'il ajoute (GND-SN-PRO, GND-SW, GND-PU, NFX-EV, NFX-CO,
NFX-UC, INP-CA, INP-CB) pour que MoDule les joue ? À établir, sur le `.syx` de l'utilisateur et sans rien commiter
de lui : comment se décompresse la section 0 (le dépaqueteur de 1.63 s'arrête à l'octet 7 090) et ce que sont les
blocs « SFS1 » jusqu'à `$F0000` ; où sont les programmes du DSP voix et du DSP mixeur, et le code DSP de ces 8
Machines ; où sont la table des Machines et leurs routines 68k (coefficients) ; où X.20 répartit les Machines
utilisateur, et sur quel DSP. Rendre une estimation (jours, semaines ou mois) et ce qui resterait inconnu. Limité à
une session : ce qui n'est pas trouvé dedans est rapporté comme inconnu, pas poursuivi.
