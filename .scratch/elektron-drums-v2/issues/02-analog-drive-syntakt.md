# ANALOG ≈ du drive Syntakt, par piste

Type: AFK puis HITL (écoute)
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-26)
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

## Décidé (2026-11-26, avec l'utilisateur)

- Forme : un type de drive de plus en fin de liste, « Saturate ≈ » (type 9), plutôt qu'une cellule MODEL sous
  « Saturate ». Aucun paramètre d'hôte en plus : mêmes AMOUNT et MIX. Une automation de TYPE enregistrée avant vise un
  type voisin, comme à l'ajout de Bit reduce et SR reduce ; les états, Kits et sons gardent des valeurs simples.

## Avancement (2026-11-26)

`AnalogDrive` (`edDrumsAnalogDrive.*`), `DriveType::SyntaktAnalog` :

    passe-haut de couplage > gain > écrêtage doux asymétrique > bloqueur de continu (10 Hz)

- Passe-haut à un pôle, sa coupure 12 + 290 (v/127)⁴ Hz : 12 Hz à 0, 30,7 à 64, 302 à 127.
- Gain 0 à +30 dB, linéaire en dB ; AMOUNT s'affiche en dB. Rampe sur le bloc de la chaîne, au taux suréchantillonné
  compris.
- Écrêtage : l'approximant de Padé de tanh, tenu à ±1 au-delà de 3, décalé de 0,2 pour des harmoniques paires. La
  sortie plafonne à +0,8 / -1,2 : ANALOG ≈ au maximum dépasse 0 dBFS (accepté).
- Souffle blanc à l'entrée, 2⁻¹⁶ de l'enveloppe de l'entrée, avant le gain. Il suit l'enveloppe (descente de 100 ms à
  -60 dB) au lieu d'être constant : constant, il tiendrait la piste au-dessus du seuil de silence du moteur (-100 dB),
  sa voix ne finirait jamais.
- Mono par voie ; suréchantillonnage global comme les autres drives (pas un étage de firmware). Lit son taux et le
  facteur (`setFactor`, appelé par `Chain::setOversampling`).

Mesuré (`edDrumsAnalogDriveTest`, dans le gate) :
- sortie monotone en DRIVE à -26 et -6 dBFS ;
- THD+N à DRIVE 64 croissante avec le niveau : -46 dB à -40 dBFS, -8,9 dB à 0 dBFS ;
- compression à DRIVE 96 : pente 0,97 au calme, 0,01 près de la pleine échelle ;
- grave : 50 Hz contre 1 kHz, -1,6 dB à DRIVE 64, -15,5 dB à 127 ;
- souffle sous -6 dBFS tenu : -107, -92, -77 dBFS à DRIVE 0, 64, 127 ; silence 0,5 s après l'entrée ;
- 16 voies égales à chaque voie seule, bit pour bit ;
- 44,1 contre 48 kHz, direct et 2x : ton à 0,007 dB, THD+N à 0,02 dB ;
- coût : 0,02 % d'un cœur par piste avant suréchantillonnage (guide 0,1 %).

`edDrumsEditorTest` : DRIVE 2 en « SATURATE ≈ » à l'écran (le glyphe est dans Barlow), AMOUNT 127 lu « +30.0 dB ».
Aucun set existant ne change : le type est ajouté en fin, le hash de `edDrumsRateTest` est inchangé.

Écoute : page locale `C:\dev\elektron\elektron-work\listen-analog\page.html` (sources synthétiques, rendu à 4 × 44,1 kHz),
ANALOG ≈ à 0, 32, 64, 96, 127 contre DIGITAL 64 et 127. Écoutée par l'utilisateur le 2026-11-26 : le modèle lui
va tel quel, sans note à appliquer.

## Références

Spécification v2, §4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
