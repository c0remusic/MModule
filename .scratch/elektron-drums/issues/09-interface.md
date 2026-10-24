# Interface façon Overbridge

Type: prototype
Status: open
Assignee:
Blocked by: 

## Question

Réagir au mockup v1
(`.scratch/elektron-drums/mockup/v1.html`) : pages, organisation de la piste, chaîne visible, navigateur de machines,
mix, FX, kits. Les itérations suivantes portent les décisions des autres tickets.

Exigences de l'utilisateur (2026-10-24), du mockup v2 :
- Une box par section, comme les écrans Overbridge (v1 refusé : trop proche de MoDule).
- La box AMP trop grande face aux boxes chargées : tailler chaque box à son contenu.
- Locks par pas sans tenir le pas : options sélection verrouillée (v3a) et lignes de locks (v3b), voir le ticket du séquenceur interne.
- LFO : les mapper facilement avec un ASSIGN, alors que le LFO et ses cibles sont sur des sous-pages différentes ; proposé ASSIGN qui reste armé d'une sous-page à l'autre, poignées LFO à glisser sur un knob depuis la bande de chaîne, menu de destinations groupé par box, profondeur réglée sur le knob ciblé.
- Changer d'onglet pour mapper est pénible : une seule page par piste, sans sous-pages, comme Overbridge Rytm (toutes les boxes visibles, compactes, une box qui s'agrandit au clic sur son en-tête), et assignation depuis le knob lui-même (pastille « + » au survol ou clic droit : LFO 1, LFO 2, lock sur les pas sélectionnés, profondeur sur l'anneau).
- Pas de rangée de pads ni de clavier en bas : un petit bouton play dans chaque onglet de piste (écoute sans sélectionner) et une LED qui s'allume quand la piste est déclenchée.
- Onglet SEQ (v4) : ne pas afficher toute la vue de piste en réduit, trop petite ; montrer seulement les paramètres verrouillables de la piste (liste groupée par box) pour choisir les lignes de locks.
