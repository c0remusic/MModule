# BR et SRR du DT2 en drives, son base-width en BAND

Type: AFK
Status: done
Assignee:
Blocked by: 01

## Quoi

BR et SRR du DT2 deviennent des types de drive, son base-width un type de BAND, tous après les types actuels (le
BaseWidth existant est celui du Syntakt). Le type BR garde la loi du DT2. Relevés à l'oracle s'il manque des
vecteurs.

## Critère de fin

- Chaque type jugé contre ses vecteurs par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Fait (2026-11-20)

**Oracle.** `tools\dt2\dt2_stage_oracle.py` (elektron-work) fait tourner la chaîne de piste du DT2 (OS 1.16) jusqu'à
chaque étage et capture sa sortie. 96 vecteurs dans `vectors-dt2` : `br_*`, `srr_*`, `bw_*`. Lois dans
`vectors-dt2\STAGES.md`, chacune vérifiée par un modèle float32 exact au bit près. Le modèle et les tables relevées
sont dans `tools\dt2\stages\`, hors du dépôt.

**BR** (type de drive « Bit reduce », 0x1cdd8b). Fondu entre un quantificateur à n bits et un à n + 1 bits de
(x + 1) / 2. Les bits viennent d'une table : 31 à 0, puis une cubique en BR, juste à 7,9e-6 près. BR 0 laisse passer.
Sans état, identique à toutes les fréquences.

**SRR** (type de drive « SR reduce », 0x1cdecb). Deux échantillonneurs-bloqueurs, de n + 1 et n + 2 échantillons à
48 kHz, mélangés par la fraction de t = 100 (k / 127)². À une autre fréquence, le maintien mélangé t + 1 garde sa durée.
Un Hit remet les compteurs à zéro, et son bloc passe sans effet, comme sur le DT2.

**Base-width** (type de BAND « Fine cuts », 0x1ccbd8). C'est la structure du BaseWidth Syntakt : un passe-bas à un pôle,
puis un passe-haut à un pôle sur sa sortie. La table de pôles est celle du firmware Syntakt. Elle a pris ici la forme
exp(-w g(w)), g un polynôme de degré 8 juste à 1,7e-6 près. Le BaseWidth Syntakt existant garde sa propre formule,
juste à 2,1e-3 près : aucun set existant ne change de son. `syntakt::fast::BaseWidth` reçoit sa fonction de pôles en
paramètre.

**Arrondis du DSP reproduits.** Le DSP lit le bouton comme demi-mot × 1/32512 en float. Pour les valeurs 9, 13, 18,
26, 36, 52, 72 et 104, p × 127 tombe juste sous l'entier : SRR lit alors sa table un cran plus bas, et le base-width
reçoit 256k − 1. Sous `/fp:fast`, le compilateur fusionnait les deux produits ; un `volatile` garde l'arrondi.
Toujours sous `/fp:fast`, la division du maintien SRR devenait un produit par la réciproque : 127 donnait n = 99,
g = 1 au lieu de n = 100, g = 0. Une marge de 1e-9 avant la troncature le corrige.

**Test** `edDrumsDt2StagesTest`, inscrit au gate (DT2_VECTORS) :
- Q3 contre l'OS : BR 111,2 dB, pire fenêtre 105,9 dB ; SRR 108,2 et 108,4 dB ; base-width 86,0 et 67,0 dB.
  Seuils : 70 et 60 dB.
- BR et SRR : 16 voies jouent, bit pour bit, ce que joue chaque voie seule, sans toucher les autres.
- À 44,1 kHz, le maintien mélangé du SRR dure ce qu'il dure à 48 kHz, à un échantillon près, de 1 à 127.
- Coût sur 16 voies à 44,1 kHz : BR 0,47 à 0,64 % d'un cœur, SRR 0,17 à 0,28 %.

## Références

Spécification v2, §5.2 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
