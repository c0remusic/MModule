# Layering d'un sample dans la piste

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 02

## Question

Quel lecteur d'échantillons pour la
couche sample d'une piste (les deux lecteurs de SP TWINSHOT, portés et vérifiés, puis les moteurs du DT2), quels
réglages (choix, niveau, accord, début, fin, sens, enveloppe propre ?), et où la couche entre dans la chaîne (avant
les drives, sommée à la machine) ?

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q6).

- Lecteur : le moteur sampler du Digitakt II porté (voix `0x1c4ecf`, STRETCH `0x1c5576`, recherche « Faisabilité du
  Digitakt II »). Le layering attend donc ce port : il sort de la v1 et rejoint le jalon du Digitakt II (choix de
  l'utilisateur, plutôt qu'un lecteur maison en attendant). Son coût CPU (rendu interne à 96 kHz puis décimation) est
  à mesurer au port.
- Entrée dans la chaîne : sommé à la machine avant les drives (squelette de « Chaîne de piste »), mono.
- Réglages : sample, niveau propre (pas de knob unique machine/sample), accord en demi-tons et fin, début, longueur,
  boucle, lecture à l'envers, enveloppe propre (attaque, maintien, déclin), plus ceux du lecteur DT2 qui s'y ajoutent ;
  tous verrouillables par pas et modulables.
- Décalage du sample de 0 à 100 ms après le trig (pas de négatif : il ajouterait de la latence).
- L'accord suit la note du trig, interrupteur « suivi de note » pour le figer.
- Layer éteint par défaut, sans coût éteint ; un retrig relance le sample avec la rampe anti-clic de la voix.
