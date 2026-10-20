# Le menu secondaire des Machines utilisateur

Type: prototype
Status: open
Assignee: c0remusic (session du 2026-10-20)
Blocked by: 25

## Question

Dans le navigateur de Machines, un clic sur l'en-tête du bloc USR ouvre la Library entière, rangée par famille comme les
Machines intégrées : le code MDM1 avant le tiret (TX8, TX9, TXZ, NFX, P-I, SYN pour les 43 d'X.20), le nom dans chaque
case. Un clic met la Track montrée sur cette Machine (ticket « Les numéros USR donnés par le Kit ») ; « Listen while
choosing » l'entend avant, comme une intégrée. Retour aux familles intégrées par l'en-tête ou Esc. Un prototype dans le
skin, au pas de 4 et dans la grille v22, avec les boîtes que `mdDrumsSkinTest` compare, à juger par l'utilisateur : la
place, les noms entiers ou le code, ce que montre une famille plus longue qu'une rangée.

## Answer (prototype)

Construit le 2026-10-20, à juger : [capture](../26-menu-secondaire.png), celle que `mdDrumsSkinTest` écrit avec
`MD_DRUMS_SKIN_PNG` et `MD_X20_MACHINES` (les 43 d'X.20 et deux Machines de test, TST).

- Où : la ligne « user machines » du bloc USR porte « all 43 installed » ; un clic y montre la Library à la place des
  familles intégrées, du ROM et des blocs USR et ROM 33-48, la prévisualisation gardée. Le titre devient « Choose a
  user machine », « Built-in machines » s'ajoute à Cancel et Keep ; lui ou Esc reviennent aux intégrées, le navigateur
  ouvert. Fermé, le navigateur rouvre sur les intégrées.
- Rangées par famille comme les intégrées : la catégorie du paquet en tête avec son nombre (NFX 9, P-I 5, SYN 9, TX8 10,
  TX9 5, TXZ 5), le code dans la case (BD, CP…), seize rangées par colonne de 288 à 768 dp, une famille plus longue
  sur la colonne suivante, huit colonnes (c4 à c11). Ordre alphabétique des catégories, celui de la Library.
- Un clic met la Track montrée sur cette Machine (`Processor::chooseUserMachine`), entendue avec « Listen while
  choosing » ; la case de la Machine que la Track joue est allumée comme une intégrée choisie. Cancel rend la Machine
  d'avant.
- Les cases se refont quand la Library change (un lâcher, le Menu, une autre instance), quatre fois par seconde au plus,
  navigateur ouvert (`LibraryView`).
- Vérifié : `mdDrumsSkinTest` (« all 1 installed » l'ouvre, la Machine de la Track allumée, la case à 256, 288, les
  familles intégrées cachées et la prévisualisation gardée ; une deuxième installée apparaît dessous ; choisie, elle prend
  le numéro de la Track ; Esc revient aux intégrées), lignes de base au pas de 4.
- Pas fait : les flèches et la molette dans la Library ; le nom entier ou le sprite ; les noms dans les seize cases USR.
- À juger : la place (sur les familles, ou ailleurs), l'ordre des familles (alphabétique, ou les batteries d'abord), le
  code seul dans les cases.
