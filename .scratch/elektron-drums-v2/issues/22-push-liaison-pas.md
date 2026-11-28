# Compagnon Push : liaison et pads de pas

Type: AFK puis HITL (Push en main)
Status: done (essai sur Push à venir)
Assignee: Claude (session 2026-11-28)
Blocked by: 01

## Quoi

Un device Max for Live devant Lockstep sur sa piste, pour Push 2 et Push 3 en mode contrôleur. Il se présente à
Lockstep par des notes MIDI, comme MoDule Seq ; Lockstep écoute alors sur un port UDP local propre à l'instance, par
lequel le compagnon lit et écrit les patterns. Pads : les 16 pas de la piste affichée sur deux rangées de 8, trig posé
ou retiré à la vélocité du pad, couleur des pads pour lire les pas. Armé, il prend `Button_Matrix`, `Track_Controls`
et `Track_Select_Buttons`, et les lâche désarmé ; il suit `pad_layout` et repeint. Un seul compagnon arme Push à la
fois. Le compagnon ne joue pas les notes : le séquenceur interne joue à l'échantillon près. Rien sur l'écran de Push.

## Critère de fin

- Test sans Push : un client UDP de test lit et écrit un pattern ; le séquenceur joue ce qui est écrit.
- Deux instances : chacune son port, aucune ne lit l'autre.
- Essai de l'utilisateur sur Push : pas posés et retirés, armement et désarmement.
- Gate vert, nouveau test inscrit dans sa liste.

## Décisions (2026-11-28, avec l'utilisateur)

- **Liaison : OSC sur UDP, 127.0.0.1 seulement**, ce que parlent `udpsend` et `udpreceive` de Max. Le compagnon
  ouvre son port P et le donne par les notes 124-127 : 124 à la vélocité 76, puis 125-127 à 1 + six bits de P chacune
  (une vélocité 0 serait un note-off). Il les répète chaque seconde tant que Lockstep ne répond pas, toutes les deux
  ensuite. Lockstep ouvre un port que le système choisit et répond `/lockstep/hello port` à P. Deux instances : deux
  paires de ports.
- **Pads : la piste affichée** dans l'éditeur (`shownTrack`, celle des relais), le pattern qui joue. Pas 1-16
  seulement ; la pagination plus tard.
- **Geste** : un pas vide reçoit un trig à la vélocité du pad ; un trig appuyé moins de 400 ms est retiré au
  relâchement (tenir reste au lock du ticket 24).
- **Couleurs**, palette par défaut de Push, sans SysEx : au-delà de la longueur éteint, pas vide gris sombre, trig
  coloré, tête de lecture blanche. Lockstep envoie le pas joué quand il change.
- **Seule `Button_Matrix`** est prise ici : les encodeurs et `Track_Select_Buttons` pris sans relais ne feraient plus
  rien ; ils viennent au ticket 23.
- **Armement** : touche PUSH du device, paramètre caché, éteinte au chargement. Armé, le compagnon garde Push quelle
  que soit la piste sélectionnée ; en armer un désarme les autres (`send` global de Max) ; supprimé, il rend Push.
- **Le device vit dans le dépôt** en source (`source/elektron/edDrumsPush/` : `make_device.py`, `lockstep_push.js`) ;
  l'`.amxd` se construit hors du dépôt.

## Avancement (2026-11-28)

- Lockstep (`edDrumsPushLink.*`) : un codec OSC à entiers (un `f` arrondi, tout autre type refusé) plutôt que
  `juce_osc`, qui aurait grossi la bibliothèque JUCE commune à tous les plug-ins ; `PushHello` lit le hello sur le fil
  audio ; `PushLink` lit son port sur un fil à lui. Le minuteur du Processor (`serviceCompanion`) ouvre le lien au
  premier hello, écrit les trigs reçus par `editSequencer` (`/lockstep/trig pattern track step velocity`, 0 retire,
  hors bornes ignoré), et envoie `/lockstep/view` (pattern, piste, longueur, 16 vélocités) et `/lockstep/play` quand ils
  changent, et à chaque hello.
- Compagnon (`lockstep_push.js`, ES5 d'un `[js]`) : hello, port changé après cinq hellos sans réponse (port pris),
  prise de `Button_Matrix` sur la deuxième surface qui la liste (règle mesurée par md-drums-seq, ticket 33), matrice
  repeinte six fois à 150 ms après la prise et quand `pad_layout` change, pads des deux rangées du haut.
- Couleurs : la palette par défaut n'a pas de nuances d'une teinte ; trig vert à partir de la vélocité 64, bleu
  en dessous. À juger sur Push.
- Tests : `edDrumsPushLinkTest` (codec, hello pour tout port, deux liens en local, au garde-fou) ;
  `edDrumsProcessorTest` `companion` (deux instances, deux ports, trigs écrits par UDP joués à leurs échantillons,
  l'autre instance ni écrite ni informée).
- Revue, corrigé : armé sans Push trouvé, la touche s'éteint (la réarmer réessaie) ; lignes au-delà de 120 colonnes.
  Laissé : `udpreceive` écoute sur toutes les interfaces (sa référence, Max de Live 12, n'a que `port`, `quiet`,
  `defer`, `outputformat`, `maxqueuesize`) ; Windows peut demander l'accès réseau pour Live au premier chargement, et
  un hôte du réseau local pourrait repeindre les pads. Les écritures vont toujours à 127.0.0.1, et Lockstep n'écoute
  qu'en local.
- Reste l'essai sur Push : construire le device (`python -I source/elektron/edDrumsPush/make_device.py <dossier>`),
  le poser devant Lockstep, PUSH allumé : pads des pas, pas posés et retirés, tête de lecture, désarmement, deux
  compagnons armés l'un après l'autre.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
