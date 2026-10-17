# Un Sample joué par le moteur

Type: task
Status:
Blocked by: 

## Question

Première tranche, comme « Écrire un Sample dans le DSP voix en marche » l'a mesuré : `mdDrums::Engine` reçoit le Sample
d'une Machine ROM (ses mots de 12 bits, sa longueur, sa boucle, son rate en fraction de 44,1 kHz) ou son retrait, et le
Device l'écrit dans le DSP voix sur le thread audio, entre deux blocs, par tranches de 4 096 mots, l'entrée de
répertoire en dernier. Les Samples de l'utilisateur vivent dans la place sûre au-dessus de la banque d'usine
($18FC12-$1F85B4, sous ce que lisent les E12), à la suite, réécrits à partir du premier qui change ; un Sample qui ne
tient pas est refusé. Retirer remet celui d'usine (ROM-01 à ROM-32) ou rend la Machine muette (ROM-33 à ROM-48). Le
moteur de la prévisualisation du Hit reçoit les mêmes à sa construction. Tests : un Sample joue sur ROM-33 et sur
ROM-05 (à la place de celui d'usine), retiré il rend l'usine ou le silence, la prévisualisation joue le même son, une
écriture ne dépasse pas sa tranche par bloc, aucune allocation sur le thread audio, latence note→son inchangée.
