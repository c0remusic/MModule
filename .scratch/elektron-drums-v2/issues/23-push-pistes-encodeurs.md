# Compagnon Push : pads des pistes et relais des encodeurs

Type: AFK puis HITL
Status: done (essai sur Push à venir)
Assignee: Claude (session 2026-11-28)
Blocked by: 22

## Quoi

Une rangée de pads montre les 16 pistes : sélection de la piste affichée, mute. Sans pas tenu, les encodeurs
restent les relais, que le compagnon relaie lui-même.

## Critère de fin

- Test UDP : sélection et mute reçus et rendus dans l'état.
- Essai de l'utilisateur sur Push.
- Gate vert.

## Décisions (2026-11-28, avec l'utilisateur)

- **Pads des pistes** : troisième et quatrième rangées du haut, pistes 1-8 et 9-16. Un appui montre la piste, comme
  un clic de l'éditeur (`setShownTrack`) : relais et pas du haut la suivent. Rangées 5-8 éteintes.
- **Mute** : touche Mute de Push prise (`Global_Mute_Button`, nom validé dans Live par md-drums-seq). Tenue + pad de
  piste : mute ou démute ; appui bref sans pad : mode Mute verrouillé, la touche allumée, un second appui en sort.
- **Couleurs** : piste affichée blanche, muette gris sombre, avec trigs dans le pattern verte, sans trig bleue. Pas
  d'éclair au Hit.
- **Encodeurs, révision de la spec** : pris, ils laisseraient l'écran de Push sur la banque de Live, aux noms faux.
  Ils restent donc à Live sur les relais ; le compagnon les prendra le temps d'un pas tenu (ticket 24).
  `Track_Select_Buttons` ne sont pas pris. Spec v2 §8 amendée.

## Avancement (2026-11-28)

- Lockstep (`serviceCompanion`) : `/lockstep/track t` montre la piste, `/lockstep/mute t 0|1` la coupe par
  `editSequencer` (gardé dans l'état, `mutes`), hors bornes ignoré ; les écritures dans leur ordre. Il envoie
  `/lockstep/tracks` (16 états : 2 muette, 1 avec trigs, 0 sans) quand ils changent et à chaque hello.
- Compagnon (`lockstep_push.js`) : pads des pistes, touche Mute tenue ou verrouillée, couleurs ; la touche rendue au
  désarmement.
- Test : `edDrumsProcessorTest` `companion` : piste 4 montrée, piste 1 muette, piste 2 muette puis rétablie, valeurs
  hors bornes ignorées, l'autre instance intacte, le mute dans l'état restauré.
- Reste l'essai sur Push : pads des pistes, mute tenu et verrouillé, touche allumée, encodeurs toujours sur les relais.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
