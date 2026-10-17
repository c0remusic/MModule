# Reprendre les corrections d'émulateur de x20-boot

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Mettre sur `main` les trois corrections que « Booter X.20 dans mdLib » a faites sur la branche locale `x20-boot`
(worktree `C:\dev\MModule-x20`) : dans dsp56300 (commit 0e7b9234), CMPU et EOR #xxxx que le JIT compilait en rien ;
dans mc68k (commit 69939c8), le push d'un octet sur la pile du ColdFire, que Musashi décalait de deux comme un 68000.
Chacune avec un test qui échoue sans elle, le garde-fou complet vert, et ce que le push d'octet change au moteur de
MoDule (qui fait tourner le même ColdFire) mesuré. dsp56300 pointe sur le fork de l'utilisateur
(c0remusic/dsp56300-md-mm) ; mc68k pointe sur joelanders/mc68k-md-mm, qui n'est pas à lui : demander avant de
pousser là, ou de changer l'URL du sous-module vers un fork.

## Answer

Fait le 2026-10-17 :

- dsp56300 : la correction de CMPU et EOR #xxxx reprise sur la branche `mmodule` du fork (au-dessus de « Leave out
  the wxWidgets debugger »), avec des tests unitaires qui tournent sous le JIT et l'interpréteur (`unittests.cpp`
  `cmpu`, `eor_xxxx`). Sans la correction, `dsp56kTestRunner` échoue au premier test de CMPU (Z non posé par le
  JIT) ; avec, tous ses tests passent.
- mc68k : l'utilisateur a forké joelanders/mc68k-md-mm en c0remusic/mc68k-md-mm ; le sous-module y pointe, branche
  `mmodule`, commit du push d'octet. `mdDrumsEngineTest` vérifie qu'un octet par -(A7) et (A7)+ déplace le pointeur
  de pile du ColdFire de un (deux sans la correction : le test échoue).
- Ce que le push d'octet change au moteur de MoDule : rien. Instrumenté localement (jamais commité), le ColdFire n'a
  fait passer aucun octet par A7 dans tout `mdDrumsEngineTest` (chaque Machine frappée, les LFO, le master) ni dans
  les boots du firmware et les scénarios des quatre fixtures, hormis les deux du test lui-même. Les enregistrements
  du firmware des fixtures sont identiques, octet pour octet, avant et après la correction.
