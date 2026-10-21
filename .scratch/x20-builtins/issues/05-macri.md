# MACRI dans l'émulateur DSP56300

Type: task
Status: open
Assignee:
Blocked by:

## Question

NFX-UC utilise une fois MACRI, que l'émulateur de `source/dsp56300` (le fork de l'utilisateur,
c0remusic/dsp56300-md-mm) n'implémente ni dans le JIT ni dans l'interpréteur (research/01 de cette carte, fait 4) :
elle reste muette. L'ajouter aux deux, comme le DSP56300 le définit (MAC avec arrondi, opérande immédiat), avec un test
du fork contre MACI suivi de RND ; puis faire pointer le sous-module sur le nouveau commit. Fini quand NFX-UC sonne
dans la sonde de research/01 sans réécrire l'instruction.
