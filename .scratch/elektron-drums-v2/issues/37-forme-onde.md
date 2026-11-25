# Voir la forme d'onde

Type: HITL
Status: in-progress
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : on n'a pas le visuel de la forme d'onde. Le cadre VOICE de la page Sound est
vide (« the hit's preview has no engine yet ») ; la maquette de référence (`concepts/u_assets/u.js`,
`drawMachScopeGraphic`) y dessine le Hit, le sample par-dessus, et l'écoute au clic.

## Décidé (2026-11-25, recommandations acceptées par l'utilisateur)

- Ce qu'elle montre : le Hit de la piste affichée rendu à part de ses réglages (machine, chaîne, enveloppe, couche
  sample), vélocité 100, au tempo de l'hôte, sans Lock, sans accent, sans sends ni master, sans les LFO. Le dessin suit
  les réglages même quand rien ne joue.
- Où : le cadre VOICE de la page Sound. Le nom du sample du Sampler passe sur la ligne de titre.
- Sample : sur le Sampler et sur la couche SAMPLE LAYER, le fichier en vert sous le Hit, deux traits pour START et la fin
  de LENGTH. Pas de poignée à tirer dans ce ticket.
- Clic sur le cadre : écoute du Hit sur Main.
- Temps : ajusté à la longueur du Hit (jusqu'à -60 dB), entre 50 ms et 2 s, la durée écrite dans un coin.
- Vertical : vraie échelle, le haut du cadre à 0 dBFS ; ce qui dépasse 0 dBFS en rouge.
- Quand : à chaque changement du son de la piste affichée, au plus une fois toutes les 50 ms en glissant, la dernière
  demande seule.
- Où calculer : un fil à part, un par processus, partagé par les éditeurs ; le moteur suivant construit à l'avance,
  libéré après 30 s sans demande. L'ancien Hit reste dessiné, grisé, jusqu'au nouveau.

## Écarts de mise en œuvre (2026-11-25)

- Une prévisualisation par instance, pas par processus : chaque instance a sa propre `Library` (son image flash), dont
  le moteur de la prévisualisation dépend. Le fil et son moteur n'existent qu'après la première demande de l'éditeur.
- Le nom du sample du Sampler va dans le coin haut gauche du cadre, pas sur la ligne de titre : celle-ci porte déjà
  VOICE, GRAIN et ses trois segments, les pages et le nom de la machine.

## Avancement

- Tranche 1 (commit à venir) : le Hit dessiné, sa durée, le rouge au-delà de 0 dBFS, l'écoute au clic, le fil à part.
  Mesuré (`edDrumsHitPreviewTest`) : un moteur construit en 4,1 ms, un Hit de 2 s rendu en 9,7 ms.
- Tranche 2 (à faire) : le sample en vert et les traits START et fin de LENGTH, sur le Sampler (12-BIT, VINTAGE,
  CLEAN) et sur la couche SAMPLE LAYER.
- Non testé : l'absence des sends et des LFO dans le Hit (la préparation est dans le Processor) ; le rouge, aucun son
  du test ne dépassant 0 dBFS.

## Critère de fin

- [ ] Test du rendu : le Hit rendu à part est celui que le moteur joue seul (mêmes échantillons, Hit pris à son
  `HitDelay`) ; sends, LFO et Locks n'y sont pas ; deux demandes rapprochées n'en rendent que la dernière ; durée
  ajustée et bornée ; dépassement de 0 dBFS signalé. Coût mesuré : construction du moteur, rendu.
- [ ] `edDrumsEditorTest` : le cadre VOICE dessine le Hit (snapshot), le sample et ses deux traits sur le Sampler, la
  durée ; un clic fait écouter ; un réglage changé redemande le Hit.
- [ ] Essai dans Live.
