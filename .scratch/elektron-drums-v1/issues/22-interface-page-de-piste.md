# Interface : page de piste et knobs en JUCE

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

La page de piste du mockup v12 en JUCE seul : barre du haut (onglets Kit editor et Sound browser, CPU), onglets de
piste avec play et LED, toutes les boxes de la piste à disposition fixe, couleurs de section de `v12-notes.md`.
Knobs dessinés dans `paint` à proportions fixes (52 et 64 px), valeur dans l'anneau, arc de modulation, lock en
accent, valeur jouée en repère ; menu Vélocité / Lock par clic sur le nom ou clic droit ; ASSIGN des LFO. Repeinte
limitée aux zones qui changent. Les boxes suivent les paramètres à mesure qu'ils existent.

## Critère de fin

- Capture de la page comparée boîte par boîte aux positions du mockup (sur le modèle de `mdDrumsSkinTest`).
- Une trame sans changement ne repeint rien ; coût de l'interface sur le fil audio : nul.

## Références

Spécification §10. Ticket « Interface façon Overbridge », `../elektron-drums/mockup/v12*`.

## État (2026-11-08)

L'éditeur JUCE suit la maquette Lockstep (spec section 10 amendée), pas le mockup v12 que ce ticket nomme encore :
`edDrumsPlugin/edDrumsEditor*`, plaques en ressources. Pages Sound, Seq Detail, Master, FX, Menu ; Kits et Browse inertes
là où rien n'existe. Écarts restants : valeurs en 0-127 et non en unités, ASSIGN, COLOR et CPU absents, police
Bahnschrift à la place de Barlow.

## État (2026-11-11)

Bloc Amp de la page Sound : les cellules de l'enveloppe (MODE, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE) et sa courbe sont
grisées tant que AMP est sur OFF, ce qui est le cas des pistes Machinedrum, OPEN DRUM et SOPHIE dont la voix garde son
enveloppe ; grisées, elles ne se tirent plus, comme les drives éteints. Bloc Machine sur SOPHIE : 20 noms sur les trois
pages (MODEL à TAIL, BODY en 20), et l'image du bloc montre le son d'usine que tiennent Machine 1-20, ou EDITED ; un
clic ouvre la liste des 16 sons d'usine, dont le choix écrit Machine 1-20 comme des gestes. Rendus `sound-sophie.png`
et `sound-sophie-p3.png` de `edDrumsEditorTest`. Les valeurs de SOPHIE restent en 0-127, sans unités.
