# Faisabilité du Model:Cycles

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Processeur et firmware du Model:Cycles (le
firmware 1.13 est installé ; elektron-emu l'émule) : peut-on bâtir un harnais d'oracle comme pour le Syntakt, où sont
les routines de ses 6 machines, quel effort et quel coût CPU attendre ?

## Answer

Faisable, et c'est la cible la plus simple après le Syntakt. Un seul ColdFire V4e (MCF5441x), sans DSP. Section 3 =
MAIN OS, 1,74 MB à `0x40000400`. Le répartiteur `0x400a7d4a` rend les 6 voix dans `0x80001858 + 128 t` (32
échantillons à 48 kHz) avant gain, distorsion et mix. Tables : render `0x40118610`, update `0x40118628`. KICK
`0x400aa3c4`, TONE `0x400aa930`, CHORD `0x400ab24c` sont sûrs ; l'ordre SNARE/METAL/PERC des indices 1-3 est
[INFER]. Ce répartiteur tourne seul, hors OS (sondé dans Unicorn 2.1.4 + l'EMAC de SyntHack) : 5,6 à 8,5 k
instructions par voix et par bloc. Estimations [INFER] : port exact ≈ 0,35-0,5 % d'un cœur par voix, port flottant
SIMD 3-5 fois moins, 4 à 7 sessions agent. Il manque un état réel de l'OS pour des vecteurs musicaux, à
enregistrer avec digiemu. Détails : [research/11-model-cycles.md](../research/11-model-cycles.md).
