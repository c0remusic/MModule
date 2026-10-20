# Ce que la DSP des voix doit faire pour qu'une NFX entende n'importe quelle Track

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
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

## Answer

[research/14](../research/14-sources-d-une-nfx.md) : un tampon par Track à Y:`$1000` (la mémoire Y de l'émulateur,
au-delà de ce qu'a une Machinedrum), seize descripteurs à Y:`$7A0` et un mot « quatre en arrière » par Track à
Y:`$7E0`, dans les 80 mots que l'anneau libère. Aucun des 13 paquets ne lit le mot 0 ; des descripteurs qui pointent
des Tracks choisies leur suffisent sans changer leur code ; NFX-MX et NFX-EN prennent leur quatrième source par une copie
dans leur propre tampon avant RENDER. Une source rendue après la NFX donne son bloc d'avant, 32 échantillons plus tard.
Un set qui ne choisit rien sonne exactement comme avant (0 bloc différent sur les 112 Machines, 20 Kits mêlés et les
13 paquets). Coût : 11 cycles par voix, 77 de plus par User Machine, 2 % d'un bloc au pire. X.20 ne permet pas ce choix :
il est propre à MoDule.
