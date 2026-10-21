# Les 6 dans le plug-in : l'OS X.20 chargé, les Machines offertes

Type: prototype
Status: open
Assignee: c0remusic (session du 2026-10-21)
Blocked by: 04

## Question

Ce que voit et fait l'utilisateur dans MoDule : où il donne son OS X.20 (une entrée du Menu, le lâcher sur la fenêtre,
la ligne d'état, comme le flash image), la copie gardée dans le dossier de données, un fichier refusé (et pourquoi) ;
le paramètre Machine qui propose les 6 (leurs numéros 1, 4, 5, 7, 8, 9 réservés : `mdProtocol/mdmachines.cpp`) ; le
navigateur (GND-SW, GND-PU dans GND, les trois NFX, GND-SN-PRO à la place de GND-SN) ; leurs noms de paramètres
(l'éditeur, le fichier Kit de MoDule Seq, Push) ; un set qui joue une des 6 ouvert sans l'OS X.20. Un prototype dans
le skin, au pas de 4, à juger par l'utilisateur.

## Answer (prototype)

Construit le 2026-10-21, à juger : [capture](../08-navigateur-x20.png).

- Où : « Load MDX X.20 OS... » dans le Menu, ou le `.syx` lâché sur la fenêtre (un `.syx` de plus de 512 Ko est un OS :
  `Processor::importKind`). `Processor::loadX20Os` le vérifie (`md::fw::parseX20Os`), l'écrit dans les `roms` du dossier
  de données (`mdx_x20_os.syx`), puis refait le Device, qui installe les six avant d'appliquer le Kit. Chaque Device
  suivant l'y trouve. Un fichier refusé le dit (« Not MDX X.20's OS: ... »).
- Le navigateur : GND-SW et GND-PU sous GND (le compte passe de 4 à 6), une famille NFX (« effects ») en colonne 11 avec
  EV, CO, UC ; cachées sans l'OS X.20 ; la molette et les flèches les sautent alors. GND-SN reste « SN » (il joue
  GND-SN-PRO quand l'OS X.20 est là).
- Les noms : SYN1-8 de GND-SW, GND-PU et des trois NFX d'après la table d'X.20 (`md::machines`), dans l'éditeur, le
  menu des LFO et le fichier Kit de MoDule Seq.
- Vérifié : `mdDrumsProcessorTest` (sans l'OS, le Device refuse GND-SW ; un faux fichier refusé ; l'OS chargé, GND-SW
  sur la Track 1 avec PTC1 73 sonne, crête 0,105 ; un second Processor le retrouve dans les `roms`) ; `mdDrumsSkinTest`
  (cachées puis montrées, à 256, 416 et 816, 352 ; la molette ; GND-SW choisi avec PTC1), dans des dossiers de données
  temporaires, jamais celui de l'utilisateur.
- Pas encore : un set qui joue une des 6 ouvert sans l'OS X.20 (la Track garde sa Machine, rien ne le dit) ; le nom
  « GND-SN-PRO » dans la bande de tête ; l'aperçu du Hit et du navigateur pour les six ; Push.
- À juger : la place (GND, colonne 11), le chargement par le Menu et le lâcher.
