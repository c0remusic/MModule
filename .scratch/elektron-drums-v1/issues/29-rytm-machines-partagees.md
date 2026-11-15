# Rytm : les trente machines partagées avec le Syntakt

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 28

## Quoi

Le ticket 28 n'a pris du Rytm que ses trois machines propres (CP CLASSIC, BT CLASSIC, XT CLASSIC). Les trente autres
machines visibles de l'OS 1.74 existent aussi sur le Syntakt, sous le même nom, avec les mêmes constantes et la même
structure de routine ; Lockstep les joue par le port du Syntakt. Leurs écarts entre les deux OS n'ont jamais été
mesurés par vecteurs : un cache de hauteur, des bornes, huit voix au lieu de quatre (ticket 28, étape 2).

Mesurer ces écarts : enregistrer les routines de contrôle du Rytm pour les trente machines (le banc d'oracle3 avec la
section 3 du Rytm, comme `record_rytm.py` pour les trois du ticket 28) et les comparer, cas par cas, au port du
Syntakt (`edDrumsAnalogControl`). Puis, selon ce qui diffère :

- rien d'audible (même sortie dans 0-127) : le noter, rien à changer ;
- un écart qui change le son : le dire avec ses chiffres, et proposer soit une variante Rytm de la machine, soit de
  garder celle du Syntakt. Ce choix revient à l'utilisateur.

## Critère de fin

- Les trente machines comparées sur leurs vecteurs, le nombre de cas égaux et différents de chacune noté, chaque
  écart expliqué (quelle instruction, quelle borne).
- Pour chaque écart audible, la décision de l'utilisateur appliquée et testée.
- Vecteurs et sections hors du dépôt ; rien du firmware n'y entre, hors constantes mesurées.

## Références

Ticket 28 (étapes 1 et 2). `C:\dev\elektron\elektron-work\tools\analog\record_rytm.py`, `rytmrig.py`,
`vectors-rytm\rytm.rav`. Port : `source/elektron/edDrums/edDrumsAnalog*`.
