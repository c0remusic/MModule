# L'aperçu du Hit d'une Machine voisine

Type: grilling
Status: open
Assignee:
Blocked by: [Les tampons des voisins](19-tampons-des-voisins.md)

## Question

L'écran Hit et l'aperçu du navigateur rendent le Hit de la Track montrée seule, sur un second moteur sans les effets
master (`mdDrums/mdDrumsHitPreview.*`, ticket 28 de la carte de l'éditeur). Une NFX ou une SYN-F n'y entend aucune
voisine : son aperçu serait muet ou sans rapport avec ce qu'elle joue.

À trancher avec l'utilisateur : rendre aussi les Tracks qui la précèdent (lesquelles, à quelle vélocité, ce que coûte
le second moteur), montrer l'aperçu tel quel avec une mention, ou ne pas en montrer pour ces Machines.

Fait le 2026-10-19 en marge ([research/12](../research/12-neighbour-buffers.md)) : la requête d'aperçu ne portait
aucune Machine utilisateur, et toute USR s'y entendait muette. Elle porte maintenant celles de l'instance, et la Track
montrée installe la sienne ; `mdDrumsHitPreviewTest` le vérifie (USR-02 sur Track 3 : le premier Hit du Device, colonne
pour colonne). Reste la question de ce ticket : les voisines d'une NEIGHBOUR.
