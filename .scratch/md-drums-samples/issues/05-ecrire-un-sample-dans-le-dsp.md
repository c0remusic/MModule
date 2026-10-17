# Écrire un Sample dans le DSP voix en marche

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Mesurer ce que coûte de mettre un Sample dans le DSP voix d'un moteur qui joue, pour décider comment le faire : écrire
le répertoire et les données (jusqu'à 459 758 mots) entre deux blocs sur le thread audio, ou les préparer ailleurs et
ne faire sur le thread audio qu'un échange court ; ce que fait le JIT du dsp56300 d'une écriture en mémoire P hors du
code ; une voix qui joue le Sample remplacé pendant l'écriture ; et le moteur de la prévisualisation du Hit, qui doit
avoir les mêmes Samples. AFK : un test qui mesure, sans rien construire dans le plug-in.

## Answer

Mesuré le 2026-10-17 par `mdDrumsEngineTest` (« Sample write »), sur le moteur nu (`md::engine::Engine`) avec la
banque d'usine, sans rien construire dans le plug-in :

- **Le JIT ne gêne pas.** Un Sample est de la donnée en mémoire P, que le JIT ne compile jamais. ROM-33, muette, joue
  le Sample du test (un sinus de 441 Hz à mi-échelle, codé en 12 bits par la table de l'OS du flash image) écrit
  au-dessus de la banque d'usine, puis son entrée de répertoire (P:$147EC0) pointée dessus entre deux blocs. Le code
  de la voix, compilé avant l'écriture, lit les nouveaux mots au Hit suivant : crête 1 647 212 sur 8 388 608.
- **Le coût.** Toute la place libre, de $18FC12 à $1FFFFF (459 758 mots, 20,85 s à 44,1 kHz), s'écrit en 0,738 ms
  d'un coup (`VoiceEngine::writeP`, 1,6 ns par mot) : autant qu'un bloc de 32 échantillons (726 µs). Par tranches de
  4 096 mots par bloc, 12,6 µs au pire : la place entière en 113 blocs, 82 ms.
- **Une voix qui joue le Sample écrasé** continue sans faute (chaque bloc rend) ; son son change, crête 3 259 280
  au lieu de 3 412 952.
- **Donc, pour les tranches :** préparer les mots ailleurs (la conversion, sur le thread de l'éditeur), puis les écrire
  sur le thread audio entre deux blocs du moteur, par tranches bornées (4 096 mots, 13 µs), l'entrée de répertoire en
  dernier : le Sample joue à partir du bloc qui suit sa dernière tranche, et un Hit de sa Machine avant joue
  l'ancien. Jamais `Plugin::withDeviceLocked`. Le moteur de la prévisualisation du Hit, neuf à chaque rendu, reçoit les
  Samples de l'instance à sa construction, comme la banque d'usine.
- **Reste à trancher en construisant :** les Machines E12 lisent `$1F85B5-$1FFFFF`
  ([research/04 des Machines utilisateur](../../user-machines/research/04-voice-dsp-memory.md)). Y écrire un Sample
  changerait leur son ; la place sûre s'arrête à $1F85B4 (428 451 mots, 19,4 s à 44,1 kHz).
