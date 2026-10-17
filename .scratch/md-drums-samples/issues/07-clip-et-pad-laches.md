# Ce qu'un clip ou un pad lâché donne vraiment

Type: task
Status: outil construit, attend les lâchers de l'utilisateur dans Live
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

La recherche (ticket 01) ne trouve rien de documenté pour un clip audio d'une piste de Live, un sample de Simpler ou un
pad de Drum Rack glissés sur une fenêtre de plug-in, et des échecs rapportés. Mesurer dans Live 12 sous Windows ce
qui arrive à MD Drums dans chaque cas (un chemin, lequel, rien), ainsi qu'un sample d'un Pack d'Ableton : AFK, un
élément du skin qui accepte tout lâcher et écrit ce qu'il reçoit dans un journal ; puis HITL, l'utilisateur fait les
lâchers dans Live et l'agent lit le journal.

## Answer (partie AFK)

Construit le 2026-10-17 : le journal des lâchers (`DropView::journal`). Avec la variable `MD_DROP_LOG` qui nomme un
fichier, chaque glisser qui passe sur le nom de la Machine ou sur l'écran Hit y écrit une ligne : « over » au survol,
« drop » au lâcher, le nombre de fichiers et leurs chemins. Une ligne par liste nouvelle : le survol qui se répète
n'écrit rien de plus. `mdDrumsSkinTest` le vérifie.

Pour la partie HITL, à faire par l'utilisateur :

1. Fermer Live. Dans PowerShell : `[Environment]::SetEnvironmentVariable("MD_DROP_LOG", "$env:TEMP\md-drops.log", "User")`.
2. Rouvrir Live (il lit la variable au démarrage), ouvrir MoDule et sa fenêtre.
3. Glisser chacun sur le nom de la Machine, sans lâcher puis en lâchant : un clip audio d'une piste ; le sample d'un
   Simpler ; un pad de Drum Rack ; un sample d'un Pack d'Ableton dans le navigateur ; un fichier de l'Explorer.
4. Dire à l'agent que c'est fait : il lit `%TEMP%\md-drops.log`, puis la variable peut être retirée
   (`[Environment]::SetEnvironmentVariable("MD_DROP_LOG", $null, "User")`).
