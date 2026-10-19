# Le set marqué modifié quand l'état change hors des paramètres

Type: prototype
Status: construit, attend le jugement de l'utilisateur dans Live
Assignee: c0remusic (session du 2026-10-19)
Blocked by:

## Question

Un Sample lâché, une Machine utilisateur installée, un Link, un Choke ou le nom du Kit changent l'état du set sans
toucher un paramètre de l'hôte. Live n'en sait rien : le set n'apparaît pas modifié, et le fermer sans l'enregistrer
perd le changement sans question. Que dire à l'hôte, et Live l'entend-il ?

## Faits

- JUCE sait le dire : `updateHostDisplay` avec `withNonParameterStateChanged(true)` ; son wrapper VST3 appelle alors
  `IComponentHandler2::setDirty(true)`, puis `restartComponent` sans drapeau (`juce_audio_plugin_client_VST3.cpp`,
  `restartComponentOnMessageThread`). L'hôte VST3 de JUCE le rend comme `nonParameterStateChanged`.
- Les notes de version de Live (10 à 12.3) ne parlent pas de `setDirty`. Que Live 12.2.1 marque le set n'est pas établi.
- L'undo de Live ne voit que les paramètres : rien de ce qui précède n'entre dans son historique, et ce ticket ne
  cherche pas à l'y mettre (un paramètre caché enregistrerait de l'automation pendant l'enregistrement, ticket 34 de la
  carte de l'éditeur).

## Prototype

Construit le 2026-10-19 : `Processor::markStateChanged`, appelé quand l'utilisateur charge ou retire un Sample, installe
une Machine utilisateur que le Device n'avait pas, change un Link, un Choke ou le nom du Kit ; jamais au chargement,
qui ne dit rien à l'hôte (ticket 34). `mdDrumsProcessorTest` (« the set marked modified ») vérifie chaque cas, et
qu'un paramètre automatisé, un état restauré ou la même Machine installée deux fois ne le demandent pas.

## À juger dans Live (l'utilisateur)

Avec la build de ce commit copiée dans `Common Files\VST3`, Live fermé avant la copie :

1. Ouvrir un set enregistré avec MoDule, sans rien toucher : le titre de la fenêtre de Live ne porte pas d'astérisque.
2. Changer un Link dans MoDule (ligne Link de la bande du haut). Le titre porte-t-il l'astérisque ?
3. Enregistrer, puis lâcher un WAV sur le nom de la Machine. Astérisque ? Fermer le set : Live demande-t-il
   d'enregistrer ?
4. Arrangement Record allumé, transport en marche : changer un Link. Live écrit-il de l'automation sur les paramètres
   de MoDule ? (Le wrapper de JUCE suit `setDirty` d'un `restartComponent` sans drapeau ; un `restartComponent` avec
   `kParamValuesChanged` faisait écrire à Live l'automation de tout le panneau, ticket 34 de la carte de l'éditeur.)

Si Live ne marque rien, `setDirty` ne sert à rien chez lui et la question reste ouverte (le seul autre moyen connu,
un paramètre bougé, a les défauts dits plus haut).
