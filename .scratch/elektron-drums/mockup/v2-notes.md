# Maquette v2 — notes

Fichier : `v2.html` (autonome, Barlow via Google Fonts, JS vanilla). Fenêtre fixe 1440x900, thème sombre seul.
Ouvrir dans un navigateur ; rien ne sonne.

## Pourquoi v2

v1 refusée : trop proche de MoDule (bande de tête, rangées pleine largeur). v2 suit les captures Overbridge
(`refs/`) : mosaïque de panneaux séparés par une gouttière de 6 px du fond de fenêtre, chaque panneau avec son
fond (#2c2f33), bord 1 px (#3a3d42), bandeau de titre 28 px (titre majuscule à gauche, sous-onglets à droite).
Une section = un panneau, jamais deux sections dans un même panneau.

Vérification : captures de chaque vue dans le navigateur intégré, comparées à `ob-rytm-1.webp` et
`ob-digitakt.png` (gouttières, bandeaux, anneaux avec valeur au centre, code couleur par panneau, graphes dans
les panneaux, pastilles en bas, pied avec zoom et version). Contrôle JS : aucun panneau ne déborde, sur toutes les vues.

## Cadre

- Barre haute : « elektron / Drums », onglets Kit Editor / Sound Browser, icônes clavier, réglages, menu.
- Onglets de pistes : KIT, 16 pistes nommées par leur machine (BD 1, SD 2, RS 3…, le nom suit la machine
  choisie), FX, MASTER, SEQ.
- Bande SIGNAL (vue piste seulement) : sélecteur de sous-page SYNTH / CHAIN / MOD + SEQ, puis la chaîne
  MACHINE+SAMPLE ▸ DRIVE pre ▸ BAND ▸ FILTER ▸ DRIVE post ▸ AMP ▸ SENDS ; un clic ouvre la sous-page du panneau et
  le cerne. Les emplacements drive pre/post s'y réécrivent selon la pile.
- Bas : 16 pastilles (nommées par machine) + clavier, puis pied : zoom %, version.

## Vue piste : trois sous-pages + SEQUENCER fixe

Trop de panneaux pour une page à taille Overbridge : découpe en sous-pages plutôt que panneaux en lanières.

| Sous-page | Panneaux |
|---|---|
| SYNTH | MACHINE (familles, grille 2 colonnes groupée, 8 anneaux, fader Volume) · SOUND SETTINGS (nom, Save/Reload/Clear, chromatique, courbe de vélocité, choke) · SAMPLE LAYER (forme d'onde S/E, Start/End/Loop/Tune/Level/Mix M↔S ; Options : mode de lecture, déclenchement, routage) · AMP (Volume, Pan, Accent, Vel→Vol) |
| CHAIN | DRIVE (pile : ajouter, retirer, monter/descendre, type parmi 6, PRE/POST, quantité ; anneaux du slot choisi) · FILTER (14 types en deux rangées Syntakt / DT2, courbe de réponse, Freq/Res/Env) · BAND (Base/Width ou EQ 3 bandes +, courbe) · MIXER (Levels : Delay, Reverb, Chorus, Ph/Fl, Track ; Config : pré/post, envoi Main ; sortie Main ou Out 01-15) |
| MOD | ENVELOPES (Filter / Amp, points déplaçables liés aux anneaux, modes RESTART/ADSR/AHD/AD/INFINITE) · MODULATIONS (LFO 1 / LFO 2 : formes, courbe, destination, Speed, Multiplier, Depth, Fade, Start Phase, Trig Mode) |

### Séquenceur avec les panneaux de l'instrument

Panneau SEQUENCER posé sous chaque sous-page de piste, toujours visible : la rangée de 16 pas et l'indicateur de
page suivent la piste choisie dans les onglets. Sélection d'un pas (clic ; Ctrl-clic ou bouton SEVERAL pour
plusieurs ; double-clic pour la trig), puis tourner n'importe quel anneau d'un panneau au-dessus : le paramètre est
verrouillé sur ces pas, l'anneau prend un cercle jaune pointillé et son nom un point jaune ; le pas porte un point
de Lock. À droite du panneau : liste des Locks de la sélection.
État montré : pas 5 sélectionné, trois Locks (Machine Tune, Sample Start sur SYNTH, Filter Frequency sur CHAIN),
points de Lock sur les pas 5, 11 et 15.

Alternatives non dessinées :
- Séquenceur en sous-page à part (comme SEQ) avec un panneau « paramètres de la piste » réduit : oblige à dupliquer
  les panneaux ou à basculer de vue pour verrouiller.
- Pas superposés aux pastilles du bas (façon Digitakt, pastilles = touches de pas) : plus compact, mais mélange
  jeu live et édition, et les pastilles ne montrent pas condition, ratchet ni micro-timing.
- Mode « maintien » strict (verrouiller seulement pendant que le pas est tenu à la souris) : fidèle au matériel,
  pénible à la souris ; la sélection collante est retenue.

Onglet SEQ (vue toutes pistes) : PATTERN (16 + chaîne), SETTINGS (Length 1-64, Swing, 7 échelles 1/8-2x, pages),
TRANSPORT (esclave de Live, BPM, position, FILL), TRACKS (16 pistes, Mute, aperçu), STEPS (16 pas, Accent, Slide,
Locks), STEP (condition, ratchet 1-8, micro-timing ±23/384, vélocité, longueur, liste des Locks).

## Autres vues

- FX : DELAY (Syntakt/DT2), REVERB (Syntakt/DT2), CHORUS (DT2), PHASER/FLANGER (Virus / Octatrack / Own +),
  chacun avec graphe et anneaux ; SENDS (matrice 16 pistes x 4 effets) ; FX RETURNS (4 faders).
- MASTER : COMPRESSOR DT2 (seuil, attaque, relâche, makeup, ratio, sidechain, mesure de réduction animée),
  OVERDRIVE DT2 (courbe de transfert), MAIN OUT (crêtes, fader), MIXER 16 voies (pan, M/S, fader, crête, sortie).
- KIT : KIT SETTINGS (nom, Save/Save as/Reload/Clear, Import/Export), KITS (64 emplacements), TRACKS IN THIS KIT.
- Sound Browser : familles, liste ; contenu factice.

## Exact, approximatif, ajouté

- Marqués « + » (ajouts, pas du firmware) : famille Analog-approx, EQ 3 bandes, phaser/flanger « Own ».
- Exacts au firmware (intention, pas preuve) : machines Syntakt et Machinedrum, filtres, drives, effets repris.
- Approximatif dans la maquette : noms des paramètres de machine (seuls TRX-BD, TRX-SD, EFM-BD, KICK suivent
  leur appareil ; les autres sont des libellés génériques), courbes (formules analogiques, pas les filtres du
  firmware), valeurs, mesures CPU et latence affichées, contenus des listes de kits et de sons.

## Questions ouvertes

1. Le brief dit 8 types multimode Syntakt ; je n'en connais que 7 (LP2, LP1, BP, HP1, HP2, BS, PK). Huitième
   montré « ST 8? ». Lequel ?
2. Legacy LP/HP du DT2 : un type avec un mode (dessiné ainsi, pour arriver à 14) ou deux types ?
3. Machines du Digitakt II : ONE SHOT et WERP seulement, ou d'autres (le DT2 en a ajouté) ?
4. Noms des machines Analog-approx : génériques (BD ANALOG…) ou noms Syntakt analogiques ?
5. Découpe SYNTH / CHAIN / MOD acceptée, ou préférer une page unique plus dense façon Rytm ?
6. Pastilles + clavier du bas : toujours visibles, ou repliables par l'icône clavier pour agrandir les panneaux ?
