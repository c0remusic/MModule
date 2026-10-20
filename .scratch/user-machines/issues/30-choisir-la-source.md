# Choisir les Tracks qu'entend une NFX

Type: prototype
Status: open
Assignee: c0remusic (session du 2026-10-20)
Blocked by: 29

## Question

Une Track qui joue une Machine NEIGHBOUR dit, dans sa bande de tête, quelle Track elle entend (« Hears track 03 »),
comme les lignes Link et Choke ; un menu sur l'écran Hit y choisit la Track, ou les quatre d'une NFX qui en mélange
quatre (NFX-MX, NFX-EN : T-1 à T-4). Par défaut, les Tracks d'avant, comme aujourd'hui. Où ce choix vit (une valeur du
Kit comme les Links, dans l'état du set, dans `Bank.machines` avec le Kit), ce que montre la ligne quand la source vient
après l'effet (un bloc plus tard), et ce que voit Push. Un prototype dans le skin à juger par l'utilisateur, au pas de 4.

## Answer (prototype)

Construit le 2026-10-20 avec le ticket 31, à juger : [capture](../30-sources.png).

- Où : le titre de l'écran Hit dit « hears track 11 » (et « and 1 more » pour une NFX qui en lit plusieurs) quand la
  Track joue une Machine NEIGHBOUR ; un clic ouvre le menu « hears » sur l'écran Hit, comme Links et Chokes.
- Le menu : une rangée « Input » avec un onglet par Track que lit le paquet, T-1 seul pour la plupart, T-1 et T-2 pour
  un paquet qui nomme T-2 ou un SIDE (les comps, BBD CHORUS, SYN-F1), T-1 à T-4 pour NFX-MX et NFX-EN (deviné des noms
  de ses paramètres, research/12 §1.4) ; puis les seize Tracks, la sienne grisée, et Auto pour la Track d'avant.
- Les Tracks qu'elle entend sont soulignées dans la liste des Tracks.
- Où vit le choix : avec le Kit joué, dans l'état du set (un message Sources par Track qui ne prend pas le défaut) et
  dans `Bank.machines` avec le Kit sauvé ; un Kit importé ou sauvé sans, chargé, rend à chaque Track les quatre d'avant.
  Le choix n'est pas un paramètre de l'hôte : Push ne le voit pas.
- Une source après la NFX : rien de plus ne le dit ; elle arrive un bloc plus tard (0,7 ms).
- À juger : la place (le titre de l'écran Hit), les onglets T-1 à T-4, le soulignement dans la liste, Auto.
