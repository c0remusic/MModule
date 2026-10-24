# Machines analogiques sans machine

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Pour chaque famille
analogique (pistes 9-12 du Syntakt, Rytm) : les routines du CPU #1 qui la pilotent (machines.md), les modèles de
circuits candidats (analog-literature.md), les enregistrements publics utilisables pour régler à l'oreille, et le coût
CPU d'un modèle par voix.

## Answer

Les 30 routines analogiques du CPU #1 (table 0x402A1B34) sont petites (105 à 778 instructions) et partagent une
douzaine d'aides (hauteur exponentielle, pentes d'enveloppe, vélocité, lissage, banque de six oscillateurs) : port
exact faisable sans machine. Les voix métalliques lisent six fréquences dans le firmware, celles du TR-808 en
proportion : modèle de Werner pour la piste 12. Aucun enregistrement public n'est à la fois isolé et à réglages
connus ; le meilleur est le Syntakt Science Lab d'Elektronauts (audio + projets `.stprj`). Coût mesuré d'un modèle
boîte blanche : 0,03 à 0,14 % d'un cœur par voix. Détails :
[research/14-analogiques-sans-machine.md](../research/14-analogiques-sans-machine.md).
