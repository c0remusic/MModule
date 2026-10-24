# Maquette v5 — notes

`v5.html` (1600x1000) part de v4. v1 à v4 restent intacts. Vérifiée en Chrome headless à 1600x1000 :
piste, piste MD, prompt de famille, SWAP, FX, MASTER, SEQ, KIT, Sound Browser. Aucun débordement, aucun texte
sous 11 px, aucune erreur JS (seule la grille des machines MD défile, comme en v4).

## Changé depuis v4

- **Chaîne décidée (ticket 02).** La bande SIGNAL suit le squelette : MACHINE + SAMPLE ▸ drives pré ▸ BAND ▸
  FILTER ▸ drives post ▸ AMP ▸ SENDS. Les drives s'y placent selon leur PRE/POST. Un slot Off est grisé. Un étage
  ajouté porte « + » (autre famille, MIX sous 100 %, EQ 3 bandes). À droite : « Chain: firmware-exact » ou
  « n additions + ».
- **Page piste réorganisée dans l'ordre du signal.** Rangée 1 : MACHINE, SAMPLE, DRIVE, BAND. Rangée 2 : FILTER,
  AMP, LFO, SENDS. La boîte ENVELOPES disparaît : l'enveloppe de filtre va dans FILTER, celle d'ampli dans AMP.
  Le Level quitte MACHINE pour AMP.
- **DRIVE** : deux slots A et B. Chacun a un menu de type (Off, Syntakt drive, MD DIST, MD SRR, MD AMD,
  DT2 Overdrive, DT2 Bit Reduction, DT2 SRR), PRE/POST, ses knobs d'origine (AMD + AMF pour MD AMD) et MIX « + »
  à 100 %. SWAP apparaît quand les deux slots sont du même côté. Dessiné : A pré, B post.
- **BAND** : interrupteur BASE/WIDTH | MD EQ (EQF, EQG) | 3-BAND « + ». Le 3 bandes compte six knobs (fréquence
  et gain grave, médium, aigu, ±18 dB), avec sa courbe. C'est l'état ouvert sur la piste 1.
- **FILTER** : 15 types sur deux rangées (8 Syntakt ; 6 DT2 + MD). Un type d'une autre famille porte « + ». Sur
  le filtre MD, les knobs deviennent Base, Width, Q. Enveloppe commune : Delay, ADSR (rangée ENV), Env. Depth,
  Key Track.
- **AMP** : AHD / ADSR (les knobs inutiles grisés), ENV RESET, Level, Pan, Accent. Sur une machine MD, l'enveloppe
  est remplacée par une note : « l'enveloppe reste dans la voix ». Rappel en pied de boîte sur les autres pistes.
- **Menu « + » d'un knob** : LFO 1, LFO 2, Velocity, Lock sur les pas choisis. La vélocité a sa couleur (vert
  clair). Elle module Level par défaut (anneau visible) et peut prendre une destination de plus.
- **Pied de page** : Drive oversampling Off / 2x / 4x, 2x choisi.
- **Prompt de famille** : la piste 1 a une chaîne modifiée (MD SRR post, EQ 3 bandes). Choisir une machine d'une
  autre famille, ou cliquer « demo: family change » dans le pied de page, ouvre un toast « Load the MD chain? ».
  Il montre la chaîne actuelle et celle de la famille, avec Keep current / Load. Même famille, ou chaîne non
  modifiée : aucune question.
- **SEQ** : la vue de piste réduite disparaît. À sa place, PARAMETERS (5 colonnes de grille) liste les paramètres
  verrouillables de la piste par boîte : Machine, Sample, Drive A, Drive B, Band, Filter, Amp, LFO 1, LFO 2, Sends.
  Chaque ligne montre sa valeur. Un clic ajoute ou retire la lane. Les lignes qui ont une lane sont surlignées,
  celles qui portent des Locks marquées ●. LOCK LANES passe à 7 colonnes, avec des lanes plus hautes, jusqu'à 8.

## Questions ouvertes

1. **Chaîne MD : trois étages drive (AMD, SRR, DIST) pour deux slots.** Faut-il un troisième slot, ou laisser
   DIST hors de la chaîne par défaut ? La maquette charge AMD pré + SRR post, et le toast le signale « ? ».
2. Chaîne par défaut des familles qui n'en ont pas (Rytm, Model:Cycles, Digitone II, Analog-approx) : celle du
   Syntakt dans la maquette. DT2 : slots Off, filtre MULTI, à confirmer.
3. Syntakt drive en slot et OVER (paramètre H de la page SYN) : est-ce le même paramètre, montré deux fois ?
4. « + » sur chaque type de filtre d'une autre famille : utile ou trop chargé ? Le garder seulement dans la bande
   SIGNAL ?
5. Vélocité : une seule destination en plus de Level ? Un LFO posé sur Level remplace-t-il la vélocité ?
6. Lane d'un paramètre que la nouvelle machine n'a plus (Machine Tune après passage en TRX-RS) : la garder, la
   griser ou la supprimer ?
7. Toujours ouvertes depuis v4 : fidélité (plat, texturé, photoréel), taille par défaut, noms longs ou courts des
   anneaux.
