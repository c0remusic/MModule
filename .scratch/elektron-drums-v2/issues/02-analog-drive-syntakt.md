# ANALOG ≈ du drive Syntakt, par piste

Type: AFK puis HITL (écoute)
Status: ready-for-agent
Assignee:
Blocked by: 01

## Quoi

Le type de drive « Syntakt drive » gagne un choix de modèle : DIGITAL (l'OVER exact, défaut) ou ANALOG ≈, le
DRIVE du bloc Analog FX du Syntakt approché. Mono, sur les voies SIMD, suréchantillonnage global. Modèle : saturation
douce qui dépend du niveau d'entrée, passe-haut de couplage dont la coupure monte avec DRIVE, compression, bruit qui
monte avec le gain. La page ne montre que les réglages du modèle choisi ; le nom porte la marque « ≈ ».

## Critère de fin

- Tests mesurés : sortie monotone en DRIVE ; THD croissante avec le niveau ; courbe de compression ; grave à DRIVE
  64 et 127 ; bruit selon DRIVE.
- Écoute de contrôle envoyée, notes de l'utilisateur appliquées.
- Coût visé : 0,1 % d'un cœur par piste qui l'utilise.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
