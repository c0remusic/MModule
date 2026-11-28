# Compagnon Push : lock par pas tenu

Type: AFK puis HITL
Status: done (essai sur Push à venir)
Assignee: Claude (session 2026-11-28)
Blocked by: 23

## Quoi

Un pas tenu arme un lock que les encodeurs règlent ; le pas lâché, les encodeurs redeviennent les relais.

## Critère de fin

- Test UDP : lock posé sur le pas tenu, joué au pas suivant à l'échantillon près.
- Essai de l'utilisateur sur Push.
- Gate vert.

## Décisions (2026-11-28, avec l'utilisateur)

- **Aucune prise d'encodeur.** Le compagnon dit le pas tenu (`/lockstep/hold pattern track step 1`) et lâché (`0`).
  Pendant la tenue, un relais que Push tourne devient un lock des pas tenus au lieu de régler le Kit, par le chemin
  de LIVE. Toute banque marche, l'écran de Push nomme ce qu'on règle, pas de latence de prise. Remplace l'idée du
  ticket 23 (encodeurs pris le temps d'un pas tenu) ; spec v2 §8 amendée.
- **Plusieurs pas tenus** : un relais tourné locke chacun, sur la piste du paramètre.
- **Pas vide tenu** : l'appui pose son trig (ticket 22), le lock s'y ajoute ; un appui long sur un trig le garde.
- **Valeur de départ** : au début d'une tenue, les relais prennent le lock du pas s'il en a, sinon la valeur du Kit ;
  au lâcher, les valeurs du Kit. Ces deux changements vont à l'hôte comme des edits, pour que l'écran de Push les
  montre (comme au changement de piste affichée) : Live les enregistre s'il enregistre l'automation.

## Avancement (2026-11-28)

- Lockstep : `takeRelays` (fil audio) envoie un tour de relais pendant une tenue dans la file de LIVE, marqué
  `HeldSteps`, sans toucher la cible ; `addLiveLock` (minuteur) en fait un lock de chaque pas tenu de la piste du
  paramètre, s'il prend un lock, et note le pas ; `serviceCompanion` tient les pas tenus, les vide quand le port du
  compagnon change (rechargé), et envoie `/lockstep/locked pattern track step`. `syncRelays` montre le lock ou le Kit
  au début d'une tenue, laisse les relais aux tours pendant, et les rend au Kit au lâcher.
- Compagnon : hold à l'appui d'un pas, lâché au relâchement et au désarmement ; un appui bref ne retire pas un trig
  qui a reçu un lock pendant l'appui.
- Test : `edDrumsProcessorTest` `companionLocks` : pas 4 tenu, relais Cutoff à la valeur du Kit, tourné à 33 : lock
  du pas 4, Kit intact, compagnon prévenu ; lâché, relais au Kit ; retenu, relais à 33 ; joué identique au même lock
  posé par l'éditeur, et entendu dès le pas 4.
- Revue, corrigé : une tenue dont le lâcher se perd (paquet perdu, device fermé brutalement) tombe après 5 s sans
  hello (le compagnon en envoie toutes les 2 s) ; seule une tenue sur la piste affichée détourne les relais du Kit ; un
  paramètre sans lock se règle comme d'habitude pendant une tenue.
- Laissé : un tour qui arrive au minuteur après le lâcher du pas est perdu.
- Reste l'essai sur Push : pas tenu et encodeur tourné, écran, lâcher, plusieurs pas tenus, appui bref après un lock.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
