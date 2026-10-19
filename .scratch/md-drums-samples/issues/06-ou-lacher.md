# Où lâcher, et ce que montre l'éditeur

Type: prototype
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 01, 02

## Question

Où l'utilisateur lâche un fichier et ce que l'éditeur lui montre : sur le nom de la Machine d'une Track (la Track
passe à une Machine ROM), sur l'écran Hit, sur une cellule ROM du navigateur de machines ; ce qui s'allume pendant le
survol ; le nom du fichier affiché sur la Machine ROM ; l'erreur d'un fichier refusé ou trop long ; et comment retirer
un Sample ou remettre celui d'usine. Un prototype grossier dans le skin, à juger dans Live.

## Answer (prototype)

Construit le 2026-10-17, à juger dans Live :

- Où : le nom de la Machine dans la bande de tête et l'écran Hit (`DropView`). Ils prennent un fichier à la fois, WAV,
  AIFF, FLAC ou une Machine utilisateur (`.syx`, `.mds`).
- Ce qui s'allume pendant le survol : le nom de la Machine et le titre de l'écran Hit passent en accent. Sans la
  bordure verte de la pile, qui sortirait ces boîtes de la grille.
- Ce que fait le lâcher (`Processor::importFile`) : un fichier audio devient le Sample de la Machine ROM que joue la
  Track montrée. Sinon il va sur la première de ROM-33 à ROM-48 sans Sample de l'utilisateur, et la Track y passe.
- Le nom du fichier sur la Machine ROM : la ligne d'état le dit 8 s (« kick.wav on ROM-33, Track 1 ») ; la Machine
  garde son nom ROM-33. Le Sample porte le nom du fichier (`Sample::name`), pour une suite qui l'afficherait.
- Un fichier refusé ou trop long : la ligne d'état dit pourquoi (le rate, les secondes qui restent, les formats pris).
- Retirer un Sample : `Processor::removeSample` existe, sans geste dans l'éditeur pour l'instant.
- À juger : ces deux cibles suffisent-elles, ou faut-il les cellules ROM du navigateur (avec « USR-01 à USR-16 dans
  le navigateur ») ; un geste pour retirer ; le nom du fichier visible ailleurs que dans la ligne d'état.

## Décision

Le 2026-10-19, l'utilisateur accepte le prototype tel quel (« ok go pour tout ») : les deux cibles, la ROM de la Track ou
la première ROM-33..48 libre, la ligne d'état.
