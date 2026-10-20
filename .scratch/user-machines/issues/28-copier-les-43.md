# Copier les 43 du zip X.20 dans la Library de l'utilisateur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by: 24

## Question

L'utilisateur l'a demandé le 2026-10-20 (« on peut pas integrer directement les machines dans le dossier du vst ? »).
Pas dans le VST ni dans une release : aucune licence ne permet de redistribuer les 43, et une mise à jour du VST
effacerait son dossier. Dans la Library de son dossier de données (`Documents\c0remusic\MoDule\UserMachines`), sur son
seul PC, depuis son zip : les 43, une fois la Library sans limite construite. Fini quand MoDule les liste toutes.

## Answer

Fait le 2026-10-20 : les 43 `.syx` du zip X.20 B, tels quels, dans `Documents\c0remusic\MoDule\UserMachines` (la
Library les lit depuis d5900fb). `mdDrumsBankTest` met les mêmes 43 dans une Library et les y retrouve toutes ;
l'utilisateur les verra dans le menu secondaire avec un VST construit après lui.
