# Ce que la DSP des voix doit faire pour qu'une NFX entende n'importe quelle Track

Type: research
Status: open
Assignee:
Blocked by:

## Question

Le 2026-10-20, l'utilisateur veut mettre un effet sur n'importe quelle Track (« comment je fais pour router une piste vers
les pistes d'effets ? », puis le choix « Choisir la source » plutôt que réordonner les Tracks). Aujourd'hui une Machine
NEIGHBOUR entend les une à quatre Tracks d'avant, par l'anneau de quatre tampons et le descripteur de Y:`$7F0`
(research/12, ticket 22). Établir ce qu'il faut pour qu'elle entende des Tracks choisies, sans réordonner :

- où mettre seize tampons de 32 mots, un par Track, dans la mémoire de la DSP des voix ; ce que coûte d'y écrire la voix
  de chaque Track (cycles par bloc, la marge de research/04) ;
- ce que lisent les 13 paquets NEIGHBOUR du zip (le descripteur, mot par mot ; NFX-MX et NFX-EN qui lisent quatre en
  arrière, et leur propre tampon avant d'écrire) : peut-on leur donner un descripteur dont « un en arrière » pointe la
  Track choisie, sans rien changer à leur code ;
- une source rendue après l'effet dans le bloc : ce qu'elle lui donne (le bloc d'avant, 32 échantillons plus tôt), ce que
  devient le délai de la Track d'avant que prend une NEIGHBOUR (ticket 20), et l'aperçu (ticket 21) ;
- ce que fait X.20 s'il permet un tel choix ;
- ce qui garde le chemin d'aujourd'hui (les Tracks d'avant) pour un set qui ne choisit rien.

Un document `research/14-sources-d-une-nfx.md`, mêmes marques [V] [M] [I] que research/12 ; rien de construit.
