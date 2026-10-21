# MACRI dans l'émulateur DSP56300

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by:

## Question

NFX-UC utilise une fois MACRI, que l'émulateur de `source/dsp56300` (le fork de l'utilisateur,
c0remusic/dsp56300-md-mm) n'implémente ni dans le JIT ni dans l'interpréteur (research/01 de cette carte, fait 4) :
elle reste muette. L'ajouter aux deux, comme le DSP56300 le définit (MAC avec arrondi, opérande immédiat), avec un test
du fork contre MACI suivi de RND ; puis faire pointer le sous-module sur le nouveau commit. Fini quand NFX-UC sonne
dans la sonde de research/01 sans réécrire l'instruction.

## Answer

Fait le 2026-10-21 : c0remusic/dsp56300-md-mm 77ebd463, branche `mmodule`, que le sous-module suit. MACRI est MACI
plus l'arrondi convergent, comme MPYRI est MPYI plus lui : `alu_mpy` qui accumule puis `alu_rnd` dans l'interpréteur,
`alu_mpy` avec ses drapeaux d'accumulation et d'arrondi dans le JIT. Test unitaire du fork, sous les deux : MACI sans
arrondi ($000002:812345), MACRI arrondi ($000003:000000), une égalité exacte vers le pair ; une valeur attendue fausse
le fait échouer sous le JIT. Dans le moteur, NFX-UC sur la Track 2, qui entend GND-SW sur la Track 1, sonne (crête
0,103, `mdX20BuiltinsTest`).
