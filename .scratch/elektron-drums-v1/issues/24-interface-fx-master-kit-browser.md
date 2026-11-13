# Interface : pages FX, Master, Kit et Sound browser

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 14, 15, 19, 20, 22

## Quoi

Les autres pages du mockup v12 : FX (retours d'envoi), Master (compresseur, vumètre de réduction, overdrive), Kit
(banque de 64), Sound browser en trois colonnes (arbre par source ou par type, liste avec recherche et filtres,
aperçu ; clic écoute, double-clic charge, glisser sur un onglet de piste ou un emplacement de kit). La box SAMPLE
reste grisée jusqu'à J2.

## Critère de fin

- Captures comparées au mockup comme au ticket 22 ; chaque paramètre a son contrôle (test qui parcourt la liste).

## Références

Spécification §8, §10.

## État (2026-11-13)

La référence est la maquette Lockstep (`concepts/t.html`, `t-ui-spec.md`), pas le mockup v12. L'audit, fait sur le code
et les tests, a trouvé l'essentiel déjà construit aux tickets 19, 20 et 22 ; ce ticket a ajouté ce qui manquait et un
test pour chaque geste, relu dans le Processor (`edDrumsEditorTest`).

| Page | Ce que demande le ticket | Avant | Maintenant |
|---|---|---|---|
| FX | quatre blocs, retours DLY REV CHO PHA en mètres | fait, couverture testée | fait et testé : glisser sur DEPTH en un geste, SYNC qui montre TIME ou RATE |
| Master | compresseur, ses six cellules et sa courbe | fait, couverture testée | fait et testé |
| Master | seuil THR à glisser sur la courbe (`gmk`) | manquait | fait et testé : glisser de 20 mm, un seul geste |
| Master | vumètre de réduction GR, barre et trace | fait, non testé | testé : −46 dB au plus profond sur des Hits forts, barre et trace repeintes |
| Master | overdrive (DRIVE), SC FILTER, MASTER | fait, couverture testée | fait et testé |
| Master | M et S par piste | grisés, « sans moteur » | faits sur le mute et le solo du séquenceur, comme Overview, testés |
| Kits | banque de 64, LOAD, SAVE HERE, LOAD AT ITS START | fait et testé | inchangé |
| Kits | RENAME | boîte modale, non testé | en place sur la ligne de titre du Slot comme RENAME de Browse, 16 caractères, testé |
| Kits | DELETE en deux clics | premier clic seul testé | testé jusqu'au Slot vidé |
| Kits | IMPORT, EXPORT `.edkit` | fait, non testé à la souris | testé : EXPORT puis IMPORT dans le Slot 10 (sélecteur de fichier remplacé par le test) |
| Browse | arbre par source (LOCKER) et par type, liste, recherche, segments | fait et testé | inchangé |
| Browse | aperçu, clic écoute, double-clic charge, glisser sur une piste (samples, sons de piste, kits) | fait et testé | inchangé |
| Browse | MACHINES : double-clic, PUT ON TRACK, glisser sur une piste | double-clic et PUT sans test, pas de glisser | glisser ajouté (nom sous la souris, cadre sur la piste), les trois testés |
| Menu | TEMPO (paramètre hôte sans contrôle) | manquait | cellule TEMPO sous WITHOUT HOST, en BPM, testée |

Couverture : un test parcourt tous les états (16 pistes sur la machine qui a le plus de Machine 1-24 et ses pages, FX
dans les 32 états de ses SOURCE et SYNC, Master, Seq Detail, Menu) ; 1528 des 1594 paramètres d'hôte ont un contrôle
atteignable à la souris. Restent sans contrôle : les 64 relais pour Push, qui visent la piste montrée, et `echo_time`,
`delay_time` (TIME en 128es), que le ticket 14 laisse à l'hôte : ils ne jouent que RATE à OFF et la maquette n'en a pas.
Leur donner une cellule est à décider.

Écarts assumés : pas de glisser sur un emplacement de kit, que v12 nommait mais que la maquette Lockstep n'a pas ; un
clic sur MACHINES n'écoute rien, aucun moteur ne rend d'aperçu d'une machine ; THR prend la pression sur tout le carré
de la courbe, pas sur la seule ligne (la disposition n'a pas à suivre la valeur). Rendus `master.png`, `fx.png`,
`kits*.png`, `browse*.png`, `menu.png` regardés. Une trame sans rien de neuf ne repeint rien sur les six pages ; rien de
neuf ne tourne sur le fil audio (M et S passent par `editSequencer`, comme Overview). `edDrumsEditorTest`,
`edDrumsProcessorTest` et `edDrumsVst3HostTest` passent. Statut laissé ouvert à cause des deux TIME.
