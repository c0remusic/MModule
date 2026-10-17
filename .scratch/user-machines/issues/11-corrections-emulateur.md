# Reprendre les corrections d'émulateur de x20-boot

Type: task
Status:
Blocked by:

## Question

Mettre sur `main` les trois corrections que « Booter X.20 dans mdLib » a faites sur la branche locale `x20-boot`
(worktree `C:\dev\MModule-x20`) : dans dsp56300 (commit 0e7b9234), CMPU et EOR #xxxx que le JIT compilait en rien ;
dans mc68k (commit 69939c8), le push d'un octet sur la pile du ColdFire, que Musashi décalait de deux comme un 68000.
Chacune avec un test qui échoue sans elle, le garde-fou complet vert, et ce que le push d'octet change au moteur de
MoDule (qui fait tourner le même ColdFire) mesuré. dsp56300 pointe sur le fork de l'utilisateur
(c0remusic/dsp56300-md-mm) ; mc68k pointe sur joelanders/mc68k-md-mm, qui n'est pas à lui : demander avant de
pousser là, ou de changer l'URL du sous-module vers un fork.
