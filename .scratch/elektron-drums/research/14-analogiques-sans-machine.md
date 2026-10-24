# 14 — Machines analogiques sans machine

Recherche du 2026-10-24, ticket [14](../issues/14-analogiques-sans-machine.md). Point de départ :
`.scratch/elektron-engines/machines.md` (table des routines, dispatch, transport des CV) et `analog-literature.md`
(modèles, méthode d'ajustement). Ce document ajoute : l'inventaire des 30 routines du CPU #1 et de leurs aides
communes, les tables de fréquences métalliques lues dans le firmware, les enregistrements publics trouvés, et un coût
CPU mesuré. Suppositions marquées [INFER].

Scripts et sorties (lecture seule, hors dépôt) : `C:/dev/elektron-work/scratch-arch/research/`
- `an14_routines.py` → `an14_routines.txt` : chaque routine de la table 0x402A1B34 désassemblée par `cfdis.py`
  (descente récursive jusqu'à rts), nombre d'instructions, instructions EMAC, appels, tables référencées.
- `an14_helpers.txt`, `an14_more.txt` : désassemblage des aides communes.
- `an14_tables.py` : tables de fréquences des voix métalliques.
- `an14bench/` : banc C++ du coût d'un modèle par voix (MSVC /O2 /fp:fast /arch:AVX2).

## Réponse courte

- Les 30 routines analogiques du Syntakt (pistes 9-12) sont petites : 105 à 778 instructions ColdFire, 2 à 34 EMAC
  chacune, et elles reposent sur une douzaine d'aides communes (hauteur → CV exponentiel, temps → pente
  d'enveloppe, échelle de vélocité, effacement d'état, filtre de lissage, banque de six oscillateurs). Port exact
  faisable comme celui du CPU #2 : même oracle, pas de matériel requis.
- Les voix métalliques (piste 12) lisent six fréquences d'oscillateurs dans des tables du firmware. CH/OH/CY CLASSIC
  et CY RIDE donnent 205, 304, 370, 522, 540, 800 Hz en proportion : les six oscillateurs du TR-808, au millième près.
  Le modèle de Werner (cymbale 808) est donc le bon point de départ pour la piste 12.
- Aucun enregistrement public n'apporte à la fois un son isolé et ses réglages exacts. Les plus utiles : les fils
  « Syntakt Science Lab » (audio + parfois le projet `.stprj`, donc réglages connus, mais en contexte de pattern),
  le pack gratuit de Lars-Erik Johansson (Rytm, effets et compresseur coupés, réglages non donnés), les sons par
  défaut des machines dans les démos (réglages = valeurs par défaut du firmware). Réglage à l'oreille possible ;
  précision réelle seulement avec un Syntakt et Overbridge.
- Coût mesuré d'un modèle « boîte blanche » par voix, à 48 kHz : 0,03 % d'un cœur (BD), 0,04 % (SD), 0,13 % (six
  carrés métalliques). Seize voix métalliques suréchantillonnées ×2 restent sous 5 %. Le port du CPU #1 ajoute
  [INFER] moins de 0,05 % par voix.

## 1. Routines du CPU #1 par famille (Syntakt OS 1.41, section 3)

Table par machine : 0x402A1B34, 45 longs indexés par id (`machines.md`). Appel à chaque bloc :
`fn(voix, 0x80003160, paramètres, long[0x80004688 + 4t])`. Inventaire (`an14_routines.txt`) :

| Famille (octet de famille) | Machines (id : routine, instructions, EMAC) |
|---|---|
| BD, pistes 9-11 (0) | BD HARD 8 : 0x400989FA, 313, 20 · BD CLASSIC 9 : 0x40097C8A, 502, 22 · BD FM 10 : 0x4009830E, 251, 26 · BD PLASTIC 11 : 0x400996BC, 362, 26 · BD SILKY 12 : 0x400990A6, 415, 34 · BD SHARP 13 : 0x40099EE2, 306, 22 · BD ACOUSTIC 41 : 0x400A0C3A, 355, 30 |
| SY, pistes 9-11 (0) | SY DUAL VCO 20 : 0x4009F146, 509, 34 · SY RAW 39 : 0x4009F986, 391, 22 · SY CHIP 40 : 0x400A0096, 778, 30 |
| SD, pistes 9-11 (1 ; ACOUSTIC 0) | SD HARD 14 : 0x4009DD0A, 406, 18 · SD CLASSIC 15 : 0x4009D1DE, 412, 26 · SD FM 16 : 0x4009D7A2, 384, 20 · SD NATURAL 17 : 0x4009E28A, 269, 22 · SD ACOUSTIC 42 : 0x4009E7DA, 306, 32 |
| RS, pistes 9-11 (2) | RS HARD 18 : 0x4009CC9E, 370, 20 · RS CLASSIC 19 : 0x4009C7B2, 350, 20 |
| Métal, piste 12 (3) | CH CLASSIC 22 : 0x4009BBA4, 179, 2 · OH CLASSIC 23 : 0x4009C260, 128, 2 · CY CLASSIC 24 : 0x4009AE3C, 199, 8 · CB CLASSIC 25 : 0x4009A810, 190, 6 · CH METALLIC 26 : 0x4009B998, 142, 2 · OH METALLIC 27 : 0x4009C070, 139, 2 · CY METALLIC 28 : 0x4009ABA0, 183, 14 · CB METALLIC 29 : 0x4009A574, 186, 6 · CY RIDE 30 : 0x4009B162, 294, 10 · HH BASIC 31 : 0x4009BE28, 165, 4 · HH LAB 43 : 0x4009C440, 246, 4 |
| UT, pistes 9-12 (0xFF) | UT NOISE 32 : 0x4009B6C4, 164, 22 · UT IMPULSE 33 : 0x4009B5A4, 105, 4 · DISABLE 34 et MIDI 35 : 0x4009B544, 26, 0 |
| CP CLASSIC 21 | routine nulle, permise sur aucune piste : morte en 1.41 |

Total ≈ 9 000 instructions pour les 30 routines distinctes : moins que les moteurs du CPU #2 déjà portés.

### Aides communes (`an14_helpers.txt`, lecture [INFER] sauf mention)

| Adresse | Utilisée par | Ce qu'elle fait |
|---|---|---|
| 0x401159A4 | 25 routines | memset à zéro (effacement d'état au trig). Lu. |
| 0x400A23B2 | 15 (BD, SD, SY) | hauteur : (note << 13 + fin << 5) × 0x55555555 (2/3 en Q31) + décalage : mot de hauteur log. |
| 0x400A2382 | BD FM, SD FM, via 0x400A2418 | même conversion avec un deuxième coefficient (0x2AAAAAAA) : hauteur du modulateur. |
| 0x400A23DC | 17 | décale, borne, puis saute dans 0x40115E8C. |
| 0x40115E8C | — | exp2 par table interpolée (0x40243700, 2 coefficients par segment) : log → linéaire. Lu. |
| 0x400A1C40 | via 0x400A2282 | log2 (ff1 + polynôme). |
| 0x400A2282, 0x400A2370 | 10 + 7 | 0x7FFFFFFF / f(paramètre) : temps → pente d'enveloppe ; 0x400A2370 l'appelle avec la constante 0x10945. |
| 0x400A2406 | 10 | (a·b·2)² : échelle quadratique de vélocité ou d'accent. |
| 0x400A16E2, 0x400A177A | 3 + 3 | paramètre 0-127 → Q31 (× 0x41041041 ≈ 1/63), bipolaire, bornes : mapping de bouton. |
| 0x400A1432 | 10 | filtre récursif à 4 accumulateurs, état en 0x800071C4 + 0x2C·n, coefficients 0x40222CF8 : lissage des CV (glissé, ou passe-bas de commande). |
| 0x400A220E | 11 (piste 12) | saut par table sur un index 0-7, écrit dans la structure + 0xA3 : choix de topologie (bits de commutation). |
| 0x400A2418 | 9 (piste 12) | six mac d'une table de 6 longs par un facteur de hauteur, borne à 0x3FFFFF : les six fréquences d'oscillateur. |
| 0x400A2074 | UT, DISABLE, MIDI | coupe la voix : enveloppes à zéro, commande 0xB0 au générateur. |

Tables propres par voix : bits de commutation 0x401E5D18 / 0x401E5D40 / 0x401E5D68 (voix 0, 1, 2), référencées par
toutes les routines BD/SD/RS/SY ; 0x401E9510 aussi. Table 0x402A1BE8 = 2^(n/12) en Q13 (0x2000, 0x2087, 0x2110…) :
accord par demi-tons des voix métalliques. Lu.

### Fréquences métalliques (piste 12), lues dans le firmware

Chaque table de machine métallique porte, à +8, six longs que 0x400A2418 multiplie par la hauteur
(`an14_tables.py`). Rapportés à 2^24/1500 par Hz (échelle [INFER], seuls les rapports sont sûrs) :

| Machine (table) | Six mots | Hz à cette échelle |
|---|---|---|
| CH, OH, CY CLASSIC, CY RIDE, HH LAB (0x401E4620 et voisines) | 005C28F5 00888888 00591687 0033E1F6 003F258B 0022FC96 | 540, 800, 522, 304, 370, 205 |
| CH, OH METALLIC (0x401E4400, 0x401E4C60) | 002E6BDC 05AF9DB2 006C0831 060C756B 00F8D4FD 09000000 | 272, ·, 633, ·, 1458, · |
| CY METALLIC (0x401E2130) | 002E6BDC 05BBE76C 006D65B7 061B7A32 00FD70A3 0922A534 | 272, ·, 641, ·, 1485, · |
| HH BASIC (0x401E4840) | 0029FBE7 004756B2 003490B9 004B17E4 00292173 006B020C | 246, 418, 308, 440, 241, 627 |

- CLASSIC : rapports 1 ; 1,483 ; 1,805 ; 2,546 ; 2,634 ; 3,902. Ceux du TR-808 (205,3 / 304,4 / 369,6 / 522,7 /
  540 / 800 Hz) : 1 ; 1,483 ; 1,800 ; 2,546 ; 2,630 ; 3,897. L'émulateur Rytm d'elektron-emu code les mêmes
  rapports (`native/rytm_dsp.c:190`, `METAL[6] = {1.0, 1.4829, 1.8003, 2.5463, 2.6304, 3.8967}`).
- METALLIC : trois paires (fréquence, grand mot) [INFER : trois oscillateurs avec un mot de modulation en anneau ou de
  désaccord chacun, cohérent avec « ring-modulated oscillators » du manuel].
- HH BASIC : six autres fréquences, deux paires presque à l'unisson (241/246, 418/440) [INFER : battements].
- CB CLASSIC et CB METALLIC n'ont pas ce bloc ; leurs tables 0x401E1778 et 0x401E1520 sont des courbes monotones
  [INFER : deux oscillateurs calculés depuis une courbe de hauteur]. Le modèle Rytm d'elektron-emu note 524/774 Hz lus
  dans la trame FPGA pour CB CLASSIC (`rytm_dsp.c:383`), contre 540/800 sur un 808.

Conséquence : sur la piste 12, la partie « réglage à l'oreille » se réduit aux filtres passe-bande et passe-haut et
aux VCA ; les fréquences sortent du port exact.

## 2. Modèles candidats par famille

Repris d'`analog-literature.md` §1, §5 et §6, recoupés avec ce que les routines calculent :

| Famille | Ce que le CPU #1 fournit | Modèle candidat | Sources |
|---|---|---|---|
| BD (HARD, CLASSIC, SHARP, SILKY, ACOUSTIC) | hauteur exp + balayage (0x400A23B2/DC), enveloppes (pentes 0x400A2282), forme d'onde par bits | VCO exponentiel, conformateur triangle → sinus (polynôme + ADAA), VCA exponentiel, transitoire par impulsion filtrée, OVER en Wiener-Hammerstein | Plaits `synthetic_bass_drum.h` (MIT) ; Werner et al. DAFx-14 (808 BD) pour les sous-circuits |
| BD FM, PLASTIC, SY DUAL VCO, SY RAW | deux hauteurs (0x400A2382), mix/anneau/FM par bits | deux VCO, FM linéaire analogique (garder l'erreur de suivi documentée), multiplicateur 4 quadrants | manuel Syntakt A.3/A.4 ; Välimäki-Huovilainen 2006 |
| SY CHIP | arpège rapide (0x4009FFEC), bruit et PWM numériques | VCO analogique + parts numériques à trouver dans le firmware, pas à modéliser | manuel |
| SD, RS | deux VCO, bruit, filtre de bruit (SD NATURAL : SVF) | Plaits `analog_snare_drum.h` / `synthetic_snare_drum.h` ; SVF TPT (Zavalishin) | Synth Secrets « Practical Snare Drum Synthesis » |
| BD/SD ACOUSTIC | IMP : PCM + bruit numérique (manuel) | PCM à extraire du firmware (table 0x4029F5A8 pour BD ACOUSTIC [INFER]) | manuel |
| Métal, piste 12 | six fréquences exactes, topologie par 0x400A220E | six carrés polyBLEP, logique ou anneau, banc passe-bande + passe-haut, VCA par enveloppe | Werner et al. ICMC 2014 (cymbale 808), AES 9207 (cloche 808) ; Plaits `hi_hat.h` |
| UT NOISE, UT IMPULSE | bruit, impulsion | spectre du bruit ; UT IMPULSE sert de sonde du filtre de piste | `analog-literature.md` §6 |

Rytm : « The analog machines are all from rytm » (Elektronauts, _Niko, 2022) ; mêmes familles plus toms, clap
(CP CLASSIC vit sur le Rytm), BT. Le firmware Rytm MKII OS 1.73 et 1.74 est sur ce PC
(`%APPDATA%/Elektron Emu/Analog Rytm MKII/Firmware/`), et elektron-emu y lit déjà une trame FPGA avec les six
fréquences métalliques (`rytm_dsp.c:49`). La table par machine équivalente du Rytm n'a pas été cherchée.
elektron-emu porte un modèle maison (SVF, overdrive, `powf`/`sinf` par échantillon) : utile comme lecture de la trame,
sans licence publiée (`README.md` des notes de portage), donc pas réutilisable tel quel.

## 3. Enregistrements publics

Critère : un son isolé, et des réglages connus. Aucun ne remplit les deux.

| Source | Machine | Isolé ? | Réglages connus ? | Usage |
|---|---|---|---|---|
| Elektronauts « Syntakt Science Lab », un fil par machine (ex. #9 RS HARD/CLASSIC, #19 SD FM/SD HARD, #22 SY RAW) — https://www.elektronauts.com/t/syntakt-science-lab/235364 | Syntakt | non (morceaux, MP3/M4A sur le CDN du forum) | parfois : des participants joignent leur projet `.stprj` (ex. « NODE SCILAB 4(3).stprj », #9) | **le meilleur** : un `.stprj` donne chaque paramètre ; si le format de projet est lu, le port du CPU #1 produit les CV exacts de l'enregistrement. FX et mix à retirer ou à reproduire. |
| Lars-Erik Johansson, 49 one-shots Rytm, WAV 24 bits mono, effets et compresseur coupés — via KVR (cité par la recherche, page non ouverte) | Rytm | oui | non (étiquettes par type) | priors de timbre, pas de vérité |
| Synthycraft, 80 one-shots gratuits (démo d'un pack de 350) — https://synthycraft.gumroad.com/l/analog-rytm-one-shot-sample-pack | Rytm MKII | oui | non | idem |
| Gradient Lab Rytm Tracker, 662 échantillons, 16 bits 44,1 kHz — https://gradientaudio.bandcamp.com/album/gradient-lab-rytm-tracker-sample-pack | Rytm | oui | non ; 16 bits / 44,1 kHz : déjà rééchantillonné | faible |
| HelloSamples Raw Series 4 (payant, 2000+ WAV, 5 round robins, partie numérique coupée) — https://www.kvraudio.com/news/36904 | Rytm (MKI ou MKII non dit) | oui | non | les round robins donnent la dispersion d'un coup à l'autre |
| 0b5vr, Scrapbox, une page par machine (ex. BD CLASSIC, 3 clips OGG) — https://scrapbox.io/0b5vr/Syntakt:_BD_CLASSIC | Syntakt | oui | non | écoute |
| Packs Elektron officiels Rytm (ex. Legowelt Studio : kits, patterns, 4 démos) — https://www.elektron.se/product/legowelt-studio | Rytm | non (démos mixées) | oui pour les kits achetés, mais les démos sont des morceaux | faible |
| Démos et revues montrant un son par défaut | Syntakt | parfois | oui si le son est l'init : valeurs par défaut du firmware (table 0x4022C8D8 par piste) | réglage grossier |

- Elektron ne publie aucun pack de sons d'usine du Syntakt (page de support : OS 1.42, Transfer, manuels ; le reset
  d'usine restaure les banques A-E depuis la machine) — https://www.elektron.se/download-support-syntakt. Je n'ai pas
  trouvé les sons d'usine dans la section 3 (recherche de noms) : inconnu s'ils sont dans l'image.
- Overbridge : le flux par piste (pistes 9-12, avant le bloc FX [INFER, `analog-literature.md` §0]) est la seule
  capture propre ; il faut la machine. Sur Rytm MKII, le flux des 8 voix demande Overbridge Premium (selon une fiche
  revendeur, non vérifiée chez Elektron).
- Aucun enregistrement avec réglages connus ne sort du mix sans effets : réglage « à l'oreille » oui, mesure non.

## 4. Coût CPU d'un modèle par voix (mesuré)

Banc `an14bench/bench.cpp` : trois voix types avec le nombre d'opérations des structures ci-dessus, 4 voix × 20 s,
blocs de 32 à 48 kHz, coefficients du SVF (un `tan`) une fois par bloc, 3 passes, machine non calme (autres sessions
actives) :

| Voix type | Contenu par échantillon | ns par bloc et par voix | % d'un cœur par voix |
|---|---|---|---|
| BD | VCO + sinus polynomial, 2 enveloppes, SVF TPT, saturation rationnelle | 178-196 | 0,027-0,029 |
| SD | 2 VCO, bruit xorshift, 2 SVF, saturation | 254-310 | 0,038-0,047 |
| Métal | 6 carrés polyBLEP, 3 SVF, VCA | 808-935 | 0,121-0,140 |

- Ce n'est pas le modèle final : c'est l'ordre de grandeur de la classe « boîte blanche » d'`analog-literature.md` §6.
- Suréchantillonnage ×2 de la saturation et du filtre résonant : [INFER] ×1,7 à ×2.
- Port des routines du CPU #1 en C++ : 105-778 instructions par bloc et par voix, [INFER] moins de 0,05 % par voix.
- Une partie neuronale (GRU 8-32 sur OVER ou le drive FX) coûterait « quelques % d'un cœur par canal »
  (`analog-literature.md` §2, d'après les articles) : à réserver au bloc FX partagé, pas à chaque voix.
- Pire cas, 16 pistes toutes métalliques suréchantillonnées : 16 × 0,14 × 2 ≈ 4,5 % d'un cœur. Le budget (15 % à
  16 pistes, `map.md`) tient.

## Inconnues

- Ce que pilote chaque bit et chaque mot CV (tables 0x401E5D18+, structure 0x80003160) : non décodé ; nécessaire
  pour savoir quelle topologie chaque machine sélectionne.
- L'échelle absolue des mots de fréquence métallique (2^24/1500 par Hz est déduit des rapports, non prouvé), et le
  rôle des grands mots des tables METALLIC.
- La table par machine du Rytm MKII et le recouvrement exact de ses routines avec celles du Syntakt.
- Si les sons d'usine du Syntakt sont dans l'image OS (sinon, rien d'officiel avec réglages connus).
- Si le format `.stprj` est lisible (les projets Science Lab deviendraient des références à réglages connus).
- Le générateur d'enveloppe matériel (FPGA, section 1) : sa loi de courbe se règle à l'oreille faute de mesure.
