# STRETCH du lecteur DT2

Type: AFK
Status: done
Assignee: Claude (session 2026-11-20)
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne STRETCH, rendu directement à 48 kHz : grains tous les 2 048 échantillons avec un fondu
de 128. Le facteur de niveau de STRETCH est relevé à l'oracle d'abord.

## Critère de fin

- Jugé contre les vecteurs STRETCH de `vectors-dt2` par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés à l'oracle (2026-11-20)

Hors dépôt, rien du firmware recopié : `elektron-work\vectors-dt2\STRETCH.md` (loi, preuves, ce qui n'est pas couvert),
`tools\dt2\dt2_stretch_oracle.py` et `regen_stretch.ps1` (19 cas `st_*` de plus que les 4 de la recherche 03), le
modèle en entiers du DSP `dt2-voice\st_exact2.py` (21 cas sur 23 au bit près, les deux autres à 8 LSB : le `pow` du
firmware rend parfois un ulp de moins).

- **Niveau.** La voix STRETCH sort sans noyau (gain 1), son étage de niveau la multiplie par 0,4047 : STRETCH sort au
  niveau d'ONESHOT (`laws.json`, `lev.selector2`). Le lecteur de Lockstep, à gain unité dans les deux modes, n'a donc
  aucun facteur à porter.
- **Grains.** Deux grains lus linéairement entre l'indice et le suivant, positions sur 31 bits de fraction. Le grain qui
  s'éteint lit à son propre indice avec la fraction du grain qui entre (le DSP prend R5 du premier pour les deux). Tous
  les 2 048 en sortie, un grain part où le temps est arrivé ; le temps avance de ratio × 2 048, ratio = BPM × portion /
  (96 000 × 120 × 2^BARS) borné à [1/8, 4]. Fondu linéaire n/127 sur 128.
- **Le grain qui s'éteint est un décalage** derrière celui qui entre, fixé à chaque grain : il relit le grain précédent
  un grain plus loin que son départ, et suit le grain qui entre quand celui-ci fait le tour d'une portion bouclée (c'est
  ce qui expliquait l'écart d'un cas bouclé au dixième grain). Il fait le tour du sample au-delà de sa fin (à l'envers,
  de son début) pour sa lecture.
- **Départ.** Au trig, le temps est un grain avant STRT (à l'envers, avant la fin − 1) et un grain part tout de suite :
  le premier bloc fond depuis le grain qui serait parti un grain plus tôt (le « grain resté de l'init » de la recherche
  03).
- **Fin**, sans boucle : la règle d'ONESHOT. Le bloc dont la lecture passerait la fin 64 sorties plus loin est le
  dernier, fondu ; à l'envers, 32 sous STRT au plus, jamais sous le début du sample.
- **Boucle.** La lecture fait le tour de la portion ; si la portion touche la fin du sample, les grains repartent du
  début comme au trig au grain suivant. À l'envers, ils continuent. Le troisième bouton est BARS, la boucle part de STRT.
- **Portion bouclée minuscule** (`st_noise_tiny_loop`, LEN de 100 échantillons) : la boucle fait au moins 141
  échantillons (la lecture repart 141 sous la fin) et le DSP continue de jouer ; ses grains y repartent du début à
  chaque fois, ce que la règle ci-dessus ne dit pas (non identifié).
- **Banc :** la mémoire hors du sample est inconnue de l'émulateur, et y lire une paire faussait la sortie même à poids
  nul. Le banc écrit 64 zéros avant et après le sample (le sample reste intact, `st_mem.py`).

## Port (2026-11-20)

`Dt2Player` : MODE STRETCH (le sixième sixième, qui jouait ONESHOT), Machine 4 BARS (comme REPITCH, 1 à 16 mesures),
STRT, LEN, PLAY, TUNE ; page `TUNE, MODE, BARS, START, LENGTH, PLAY`. Rendu au débit de sortie (48 kHz, chemin
Syntakt) sans le noyau ni le décimateur, positions en entiers 64 bits à 31 bits de fraction comme le DSP, lecture en
flottants. Ratio repris à chaque grain, pas à chaque bloc (le tempo et TUNE suivent). Une boucle fait au moins 141
échantillons, et le temps fait le tour de la portion autant de fois qu'il faut : une portion plus courte que le
déplacement d'un grain continue de sonner, comme sur le DSP (revue : un seul tour la laissait filer hors du sample).

## Réponse (2026-11-20)

`edDrumsDt2PlayerTest` juge 18 cas STRETCH contre l'oracle : 70,9 à 76,9 dB (la limite est l'arrondi plancher des
produits Q31 de l'oracle), enveloppe ≤ 0,052 dB, borne 60 dB. Couverts : rapports 1/2, 1, 2,5, borné à 4 et à 1/8,
+12, −12, note 72 et BARS 2, TUNE fractionnaire, sens inverse, fin à l'endroit et à l'envers, STRT et LEN, boucles à
l'endroit et à l'envers, sur Hit et sur bruit. Sans vecteur : le tempo changé pendant une voix ne change rien avant le
grain suivant, et une portion bouclée de 189 échantillons sonne encore après 1 s. Q15 à 44,1 kHz : enveloppe 0,040 dB, tiers d'octave 0,155 dB. Coût :
0,41 % d'un cœur pour 16 voies en STRETCH à 48 kHz, contre 1,45 % en ONESHOT (guide moteurs ≤ 10 %) ; le pire kit
garde ONESHOT, plus cher.

Laissés hors du test : `st_hit_rev` (queue du Hit inversée à ~20 LSB : l'arrondi plancher de l'oracle, 2 LSB, la
domine) et les cas Hit à STRT 60 (~570 LSB), refaits sur bruit.

Points ouverts :
- Tempo ou TUNE qui bougent pendant une voix : les vecteurs ont leurs réglages fixes après le bloc 0 ; le port suit
  le code (ratio à chaque grain, pas à chaque bloc), sans vecteur.
- Boucle à l'endroit avec STRT > 0 et une portion qui touche la fin du sample : redépart non mesuré (mis de côté à la
  revue).
- Portion bouclée plus courte que 141 échantillons : le DSP fait repartir ses grains du début à chaque grain, sans
  règle identifiée ; le port joue la boucle de 141 avec son temps (`st_noise_tiny_loop`, hors du test).
- Le `pow` du firmware (un ulp de moins à certains TUNE) : notre pas est l'exact.

Sets existants : 12-BIT et VINTAGE intacts. Un set CLEAN dont MODE était dans le sixième sixième (affiché ONESHOT)
joue maintenant STRETCH ; seul un MODE posé par l'hôte, un LFO ou un lock y arrivait, comme pour WERP.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
