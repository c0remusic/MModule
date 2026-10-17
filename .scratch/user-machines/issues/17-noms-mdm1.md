# Les noms d'une Machine utilisateur dans l'éditeur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 

## Question

Ce que MDM1 nomme, montré là où une Machine intégrée montre les siens : le nom de la Machine dans la bande de tête et
dans la liste des Tracks (sa catégorie et son code, « TX8-BD », et son nom), les noms de SYN1-8 sur les boutons de la
Track (les paramètres que le paquet déclare, les autres éteints), et les mêmes dans le fichier `.kit` de md-drums-seq.
Un numéro vide se dit « USR-05 » et ses boutons sont éteints. Tests : le skin montre les noms du paquet de test sur
une Track en USR-05, un numéro vide n'en montre aucun, le fichier `.kit` les porte.

## Answer

Construit le 2026-10-17 :

- La bande de tête d'une Track sur une Machine utilisateur dit « USR », puis la catégorie, le code et le nom du paquet
  que l'instance tient (« TST-SW TEST SAW ») ; un numéro vide dit « empty ». La prévisualisation dit « user ».
- Les boutons SYN1-8 portent les noms des paramètres du paquet ; ceux qu'il ne déclare pas sont éteints, comme pour
  une Machine intégrée.
- Le fichier `.kit` de md-drums-seq porte les mêmes : « track 3 198 TST-SW PTCH DEC - - - - - - » ; un numéro sans
  paquet se lit « USR-06 » sans noms. Le texte est refait quand une Machine utilisateur de l'instance change.
- L'éditeur redessine quand le paquet change sous le même numéro (une installation), comparé par adresse.
- Le nom de la Machine elle-même (« USR-05 ») reste celui du paramètre, que le JSON fige ; le nom du paquet est sur la
  ligne d'à côté.
- Vérifié : `mdDrumsSkinTest` (un `.mds` lâché : USR, « TST-SW TEST SAW », PTCH, DEC, SYN3 éteint), `mdDrumsDeviceTest`
  (le `.kit`, avec et sans paquet).
