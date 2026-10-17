# Mémoire libre et point d'appel dans le DSP voix d'OS 1.63

Type: research
Status:
Blocked by:

## Question

Le DSP voix tel que MoDule le charge (le programme de la section 1 d'OS 1.63, `engine/VoiceEngine.*`) a-t-il la
place et le temps pour 16 Machines utilisateur ? Mesurer la mémoire P, X, Y et externe libre ; ce qu'il faut à 16
Tracks (64 mots X et 64 mots Y chacune, 1 536 mots externes, le code relogé de jusqu'à 16 paquets, la table de sinus
et les anneaux des imports) ; où la boucle de rendu par voix pourrait appeler RENDER d'une Machine utilisateur à la
place d'une Machine intégrée ; les cycles qu'elle prend aujourd'hui sur les 71 680 d'une trame. Rapporter aussi ce
qu'un DSP56300 émulé à part coûterait à la place (mémoire, CPU, mixage dans la chaîne de DSP1 en C++).
