# Lire les conversions Pitch et Decay dans l'OS X.20

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
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

## Answer

Recherche du 2026-10-17, dans l'OS X.20 décodé, sans le booter : [research/10-pitch-and-decay.md](../research/10-pitch-and-decay.md).

- **Une routine**, `$21aeba`, convertit les huit paramètres de toute Machine utilisateur : le mot 14 bits de la Track
  (réglage << 7, modulation comprise), borné à [min << 7, max << 7], puis la conversion du quartet bas de l'octet.
- **Pitch, DEFAULT** : P = L[(v >> 4) & 127] << (v >> 11), L[k] = 2048 · 2^(k/127.5) (au TUNING 0) : environ 16
  réglages par octave, de 43 Hz à 10,6 kHz par la formule de MDS. Les modes TONAL (2 réglages par demi-ton, 64 = la
  note 69) suivent le mode d'accord de la Track, hors de cette carte.
- **Decay** : D = T[v >> 5], la table de décroissance d'OS 1.63 elle-même (512 mots à `$24bf94`), dans le flash image
  de l'utilisateur.
- **Les autres** égalent `md::mds::convert` aux réglages entiers, sauf Unipolar à 127 ($7FFFFF, pas $7F0000) ; entre
  deux réglages, X.20 tronque Index, compte tout mot non nul comme Boolean 1, et son Unipolar donne 0 sous un réglage.
- Construire : [Les conversions d'X.20 dans MoDule](15-conversions-x20.md).
