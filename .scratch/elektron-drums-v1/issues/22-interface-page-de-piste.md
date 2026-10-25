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
