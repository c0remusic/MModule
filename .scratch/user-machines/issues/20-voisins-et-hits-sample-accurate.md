# NEIGHBOUR et les Hits sample-accurate

Type: grilling
Status: open
Assignee:
Blocked by: [Les tampons des voisins](19-tampons-des-voisins.md)

## Question

Depuis le ticket 19 de la carte de l'éditeur, chaque Track de MoDule attend le reste du bloc du moteur pour que son
Hit sonne `Engine::HitDelay` échantillons après sa note, et chaque voix est retenue jusqu'à `Engine::AlignedStart`.
Une Machine NEIGHBOUR traite le bloc brut de ses voisines sur le DSP voix, avant ces délais : selon où tombent les Hits
de ses sources et le sien, son effet peut arriver jusqu'à environ 63 échantillons à côté du son sec des sources, et
d'un Hit à l'autre ([research/11](../research/11-mdm1-flags.md)).

Trois pistes à trancher avec l'utilisateur :

- donner à la Track NEIGHBOUR le délai de ses sources (lesquelles, s'il y en a quatre qui diffèrent) ;
- accepter l'écart, comme la machine a le sien (un bloc de 32) ;
- couper le sample-accurate pour ces Tracks.

La latence se mesure avant et après (CLAUDE.md : note→son et gigue).
