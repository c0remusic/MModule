# Lire les conversions Pitch et Decay dans l'OS X.20

Type: research
Status:
Blocked by:

## Question

Comment X.20 convertit-il un réglage 0-127 en mot DSP pour les deux conversions que la spécification MDS ne donne
pas : Pitch (octet `$54`, « oscillator increment using active tuning », P avec f = P × 44 100 / 2^21) et Decay
(`$07`, « per-sample envelope multiplier », Q23) ? Dans l'OS 68k d'X.20 décodé par « Décoder l'OS X.20 »
([research/01](../research/01-decoding-x20.md)), sans le booter : trouver la routine qui convertit les paramètres
d'une Machine utilisateur, ses tables, et en déduire les formules (avec l'accord A=440 ou 433, et ce que changent les
modes TONAL, TONAL-1 et TONAL+1 et le drapeau TONAL de MDM1). Rendre les formules et, si possible, les 128 valeurs
de chaque conversion à l'accord par défaut, pour remplacer les devinettes de `md::mds::convert`. Rien d'X.20 dans le
dépôt : les tables se recalculent, ou se lisent du fichier de l'utilisateur.
