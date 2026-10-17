# USR-01 à USR-16 dans le navigateur

Type: prototype
Status: construit, attend le jugement de l'utilisateur
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 

## Question

Où le navigateur de machines montre USR-01 à USR-16 (leur nom MDM1, les vides grisés), et ROM-33 à ROM-48 que la
carte des Samples rend jouables : la grille v22 n'a plus de place sous ROM (la prévisualisation commence à y 528). Un
prototype dans la maquette et le skin, avec les boîtes que `mdDrumsSkinTest` compare, à juger par l'utilisateur.

## Answer (prototype)

Construit le 2026-10-17, à juger : [capture du navigateur](../18-navigateur.png), celle que `mdDrumsSkinTest` écrit avec
`MD_DRUMS_SKIN_PNG`.

- Où : sous les familles, colonnes 4 à 11, là où la grille v22 n'a rien. Un bloc « USR 16 / user machines », en-tête à
  y 592, deux rangées de huit cases à y 632 et 664 ; un bloc « ROM 33-48 / your samples », en-tête à y 704, cases à
  y 744 et 776. Ce sont l'en-tête, la ligne et les cases des familles, descendus de 344 et 456 px (des multiples de
  4) : les 881 boîtes de v22 ne bougent pas, et les lignes de base restent sur les pas de 4 px.
- Une case vide est grisée (`#6e7177`, le gris des pistes) : un USR que la Library ne tient pas, une ROM-33..48 sans
  Sample de l'utilisateur, ni joué par l'instance ni rangé dans la bibliothèque. L'état se relit quatre fois par
  seconde tant que le navigateur est ouvert (`TrackView::followUserCells`), ce qui garde une frame sans nouveauté
  sous 2 µs.
- Cliquer une case USR prend la copie de la Library (`Processor::takeUserMachine`), un choix explicite comme
  l'utilisateur l'a décidé ; une case ROM prend le Sample rangé dans la bibliothèque s'il y en a un
  (`Processor::takeSample`). Les flèches et la molette parcourent aussi ces cases (l'ordre des numéros).
- Pas fait : les noms MDM1 dans les cases (elles disent le numéro, comme les ROM), le sprite 32x24.
- À juger : la place (sous les familles, ou ailleurs : un onglet, une colonne) ; numéros ou noms dans les cases ;
  le gris des cases vides.
