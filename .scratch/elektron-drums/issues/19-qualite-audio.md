# Qualité audio : pratiques et coûts pour l'instrument

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Quelles pratiques de qualité audio adopter pour l'instrument, avec leur coût CPU, sachant que la performance passe
avant la fidélité (budget 15 % d'un cœur à 16 pistes) et que le caractère des machines Elektron (repliement voulu
de SY BITS, SRR, bit reduction) ne doit pas être « corrigé » ? Couvrir : repliement des oscillateurs et de la FM
(PolyBLEP, minBLEP, tables limitées en bande), drives et saturation (antialiasing par antidérivée ADAA face au
suréchantillonnage, HIIR), filtres à fréquence de coupure haute (ZDF/TPT, déformation), interpolation des samples
joués à d'autres hauteurs (linéaire, Hermite, sinc), lissage des paramètres et des locks (zipper, clics), attaques et
coupures de voix sans clic (choke, retrigger, vol de voix), dénormaux, marge et gain staging sur 16 pistes et le
master, indépendance à la fréquence de l'hôte. Et comment mesurer la qualité dans nos tests : null test contre
l'oracle, THD, mesure du repliement (balayage sinusoïdal, spectrogramme), métriques de tolérance pour les étages qui
ne sont plus exacts.

## Answer

Ne corriger que les ajouts de l'instrument, jamais un étage porté (repliement de SRR, BITS, FM, DIST dans l'oracle,
vérifié par null test). Adopter : PolyBLEP pour les oscillateurs à saut des modèles analogiques (+15 dB, ~0,002 %) ;
drives maison suréchantillonnés HIIR en SIMD sur les voix (2x HIIR 8 coef. : -58 contre -24,5 dB d'ASR à 2,5 kHz,
0,012 %/voix en AVX2 contre 0,19 % scalaire ; deux drives 2x à 16 pistes ~0,4 % [INFER], donc 2x envisageable par
défaut), ADAA1 sur les écrêtages durs ; SVF TPT (Chamberlin diverge dès 12 kHz, même coût) ; Hermite pour le layer,
sinc 8 en option HQ ; rampes de 64 échantillons sur les coupures ajoutées, choke et retrigger portés comme sur la
machine ; FTZ/DAZ ; bus flottant sans écrêtage caché. Trouvé : le polyphasé du ticket 01 a sa phase inversée (THD+N
-22 dB à 1 kHz ; corrigé -73 dB, mais 9,6 dB de rejet seulement au-dessus de 22 kHz). Seize mesures de test et
seuils proposés (Q1-Q16). Détails : [research/19-qualite-audio.md](../research/19-qualite-audio.md).
