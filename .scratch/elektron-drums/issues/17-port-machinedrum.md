# Port du Machinedrum en C++

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Que faut-il pour porter le Machinedrum en C++ réécrit, sans DSP56300 ni 68k émulés au rendu, exact à l'échantillon
contre l'oracle (mdEngine de MoDule, et mdLib pour le firmware complet) ? Inventaire : combien de programmes de voix
distincts sur le DSP2 (par machine ou par famille : TRX, EFM, E12, P-I, GND, ROM, RAM, User Machines d'X.20), leur
taille en instructions, le code partagé ; les routines 68k appelées (machines, LFO, lissage) ; la chaîne de piste et
le master (DSP1 déjà en C++ dans mdEngine, master sur DSP émulé). Méthode d'oracle (vecteurs par machine comme
STV3 pour le Syntakt), points durs (arithmétique 56-bit du DSP56300, saturation, modes d'arrondi), coût CPU attendu
par piste face à l'actuel (~0,85 %/piste de voix, ~2 % fixes), effort en semaines d'agent, et ordre de port
conseillé (familles les plus jouées d'abord).

## Answer

Faisable, et petit. Le DSP2 de l'OS 1.63 a 44 entrées render distinctes (43 sonores, 39 à porter sans INP ni RAM-R)
pour 11 385 instructions DSP56300 statiques. Le code n'est partagé qu'entre machines d'une même routine (ROM ×52,
E12 ×9 et ×5) ; les familles ne partagent que des tables. Côté 68k, 55 fonctions de coefficients (4 714 instructions
ColdFire) ; le LFO et le slide sont déjà en C++. Le master fait 1 215 instructions DSP.

Port bit-exact en entiers : 0,30 ns par instruction DSP mesurée sur P-I-MA (le JIT en prend ~2,7), donc ~0,07-0,3 %
par voix [INFER] et ~6-10 % pour 16 pistes, `TrackFx` compris (c'est lui qui devient le poste dominant). En flottant,
les voix ne gagnent que ×1,5 ; le SIMD rapporte surtout sur `TrackFx` et le master (16 pistes, même code), qu'on
peut descendre à ~2-5 % pour 16 pistes [INFER]. En flottant, seul P-I (rétroactions longues) risque de s'entendre,
pourvu que phases et bruit restent entiers. Effort : ~2-3 semaines d'agent en bit-exact, +1 semaine pour le flottant
SIMD de la chaîne et du master. Ordre : harnais, ROM (dépend des échantillons, ticket 18) ou TRX d'abord, puis EFM,
E12, P-I, GND, master.

Détails : [research/17-port-machinedrum.md](../research/17-port-machinedrum.md).
