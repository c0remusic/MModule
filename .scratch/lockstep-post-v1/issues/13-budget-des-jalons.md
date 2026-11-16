# Répartition du budget CPU entre les jalons

Type: grilling
Status: resolved
Assignee:
Blocked by: 12

## Question

Le budget est de 15 % d'un cœur au plus à 16 pistes pour l'instrument complet. Les coûts sont maintenant mesurés en
instructions : voix DT2 ONESHOT ~3 600 et STRETCH ~1 800 instructions SHARC par voix et par bloc (« Oracle dynamique
de la voix sampler du Digitakt II »), machines du Cycles 5 300 à 8 300 instructions ColdFire, DRIVE analogique
~0,1 % par piste. Les ramener en temps sur ce PC (port flottant SIMD), et répartir le budget entre lecteur DT2, couche
sample, machines du Cycles, DRIVE analogique et effets du DT2 : ce qui tient, ce qui doit céder, et la mesure qui le
dira au banc.

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling).

- **Point de départ mesuré en v1** (Ryzen 7 3700X, 16 pistes à 44,1 kHz, pas sur machine calme) : moteurs 8,7 % au pire
  cas (16 MD), 7,4 % en mixte ; envois 0,96 % au pire ; master DT2 0,2 à 0,3 % ; la chaîne avait dépassé son guide de
  2 % d'environ 2 points au pire cas (ticket 11 de `../elektron-drums-v1/`). Le coût suit les machines utilisées, pas
  les pistes.
- **Règle :** les 15 % valent pour tout kit, donc se jugent sur le **pire kit** : 16 pistes sur les machines les plus
  chères, en maximisant les machines distinctes ; les 16 couches sample allumées ; chaque emplacement de chaîne au
  type le plus cher (ANALOG compris) ; envois et master au plus cher. Machine calme, 44,1 kHz, blocs de 128.
- **Guides par bloc** (somme 15 %) : moteurs ≤ 10 % (famille Cycles et lecteur DT2 du Sampler compris) ; couche
  sample ≤ 1,5 % pour 16 pistes (lecteur DT2 en ONESHOT et BR du Rytm ; estimé 0,5 à 1 % [INFER]) ; chaîne ≤ 2,5 %
  (ANALOG ≈, Cycles dist, BR, SRR et base-width du DT2), relevé de 2 % au prix des envois ; envois ≤ 0,7 % (effets du
  DT2 compris) ; master ≤ 0,3 % (ANALOG ≈ compris).
- **Dépassement :** un guide est un repère, le plafond une règle. Un ticket qui dépasse son guide le dit avec sa mesure
  et passe si le pire kit tient sous 15 % ; un ticket qui fait passer le pire kit au-dessus s'optimise avant son
  commit. Pas de dégradation automatique de qualité.
- **Mesure :** un banc `edDrumsWorstKitCost` joue le pire kit (minimum de 5 passes de 4 s), donne le pourcentage par
  bloc et au total, et entre au garde-fou comme test de temps (rejoué une fois s'il échoue seul).
- **Corrigé le 2026-11-16** (« Les effets du Digitakt II en envois ») : le guide des envois était sous leur coût mesuré
  en v1 (0,96 %). Envois ≤ 1,2 %, chaîne ≤ 2,0 % ; les autres guides ne changent pas, la somme reste 15 %.
