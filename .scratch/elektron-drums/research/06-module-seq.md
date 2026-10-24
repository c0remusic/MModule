# Fonctions de MoDule Seq

Ticket : [06 — Fonctions de MoDule Seq](../issues/06-module-seq.md).

Sources lues (primaires) :
- clone local `C:\dev\md-drums-seq` (remote `origin` = c0remusic/MModuleSeq-dev, `public` = c0remusic/MModuleSeq),
  HEAD `37ccde8` « Record 0.2.1's public commit » ; tags v0.1.0 à v0.2.1. Dépôt public : dernier commit `42e837af`
  « MoDule Seq 0.2.1 » (export du dépôt dev), release v0.2.1 (gh, 2026-10-24).
- Code du noyau : `source/mdseq/mdseqTypes.h` (modèle de données et contrat MIDI), `mdseqSequencer.h/.cpp`
  (planification, émission), `mdseqCondition.h/.cpp`, `mdseqProtocol.h` (vocabulaire patch/external), `mdseqKit.h`
  (hello et fichier Kit), `mdseqCapture.*`, `mdseqPrint*.*`, `mdseqPush.*`.
- `README.md`, `.scratch/seq-v1/map.md` et tickets 02, 23, 30, 31 du dépôt.
- MModule : `docs/adr/0003-locks-travel-as-notes.md` et ses amendements (2026-10-11, 13, 16, 17, 19) ; CLAUDE.md,
  section State (notes 36-51, 64-87, 88, 89-114, 124-127).

## Nature

Device Max for Live (MIDI Effect) posé devant MoDule sur une piste MIDI. Toute la logique est un external C++
(`mdseq.mxe64` / `mdseq.mxo`, ADR 0002 du device ; pas de JS sur le chemin du jeu, ADR 0001) dont le noyau sans Max
est testé sous ctest. Il joue en direct par `midiout` sur le fil du scheduler de Max (le fil audio de Live) et n'écrit
par LOM que pour imprimer ou capturer en clips. Version 0.2.1, Windows et macOS (macOS non essayé). GPL-3.0+.

## Le Pattern (`mdseqTypes.h`)

| Fonction | Valeurs | Source |
|---|---|---|
| Tracks | 16 (`TrackCount`) | `mdseqTypes.h` |
| Pas | 1 à 64, longueur commune aux 16 Tracks (`MaxSteps`, défaut 16) ; pas de polymétrie (hors cadre, map.md) | `Pattern::length` |
| Échelle | 7, celles du MD MKII : 1/8x, 1/4x, 1/2x, 3/4x, 1x, 3/2x, 2x (pas = 960 à 60 ticks de 480/noire ; 1x = double-croche) | `ScaleTicks` |
| Hit | oui/non, vélocité 1-127 (défaut 127 dans le code ; le ticket 02 disait 100) | `Step` |
| Locks | 24 par pas, les paramètres de Track de MoDule (SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST VOL PAN DEL REV, LFOS LFOD LFOM), 0-127 ; 63 = « pas de Lock » (valeur du Kit), donc 63 n'est pas verrouillable | `LockCount`, `NoLock` |
| Conditions | 65 entrées : None ; X% (21 valeurs d'Elektron : 1 2 4 6 9 13 19 25 33 41 50 59 67 75 81 87 91 94 96 98 99) ; A:B (B 2-8) ; FILL, !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST | `mdseqCondition.*` |
| Ratchets | 1 à 8 Hits répartis également dans le pas, courbe de vélocité -100 % à +100 % ; la Condition décide pour tout le groupe | `MaxRatchets`, `ratchetVelocity` |
| Micro-timing | -23 à +23 24es de pas (384 par mesure), le groupe de ratchets bouge avec | `MaxMicro` |
| Swing | par Pattern, 50 % à 80 %, rangée de pas balancés (défaut : pas pairs 2, 4… = 0xAAAA…), retard (2 x montant - 1) d'un pas | `swingSubticks`, `swingSteps` |
| Accent | par Pattern : montant 0-127 + rangée de pas ; chaque Hit d'un pas accentué joue à 128 + 2 x montant, pour toutes les Tracks | `accent`, `accentSteps` |
| Slide | par Pattern : rangée de pas commune aux 16 Tracks ; les Locks du Hit glissent vers ceux du Hit suivant de la Track (ou vers la valeur du Kit), sur les clocks entre les deux | `slideSteps` |
| Mutes | 16, coupent aussitôt, événements planifiés compris | `Input::SetMute` |
| Fill | maintenu (bouton FILL de la bande, ou Push) ; joue FILL, coupe !FILL ; mode « Fill edit » : les Hits posés prennent FILL | `SetFill`, ticket 30 |

Résolution interne : 840 sous-ticks par pas, pour que chaque part de ratchet (1-8) et chaque cran de micro-timing
(35 sous-ticks) tombent sur des entiers.

### Sémantique des Conditions (`mdseqCondition.cpp`, `evaluate`)

- `X%` : tirage 0-99 depuis une graine par Pass (`passSeed(startSeed, pass)`), puis par Track et pas : un même Pass
  tire toujours pareil, quoi qu'on édite ailleurs ; la graine change à chaque départ du transport (entropie), et
  l'impression peut la rejouer.
- `A:B` : joue au Pass A de chaque cycle de B, Pass comptés depuis 1 à partir du départ du Pattern (ou de sa bascule).
- `1ST` / `!1ST` : Pass 1 seulement / sauf le Pass 1.
- `PRE` / `!PRE` : dernier résultat d'une Condition évaluée sur la même Track (PRE, NEI et pas sans Condition ne le
  changent pas). `NEI` / `!NEI` : résultat de la Track N-1 au même pas (Tracks évaluées de 1 à 16) ; Track 1 n'a pas
  de voisin.
- `FILL` / `!FILL` : selon le Fill tenu.

## Banque, bascules, chaînes, transport

- 16 Patterns par device, gardés dans le set Live (un `pattr` Blob par Pattern ; la chaîne a le sien). L'annulation
  de Live (Ctrl+Z) rend un Pattern copié, effacé ou édité, et une chaîne.
- Bascule de Pattern : en lecture, mise en file pour la fin du Pass joué (le bouton clignote), ancrée là, Passes
  recomptés depuis 1 ; à l'arrêt, immédiate. Le paramètre Live « Pattern » s'automatise (Arrangement, enveloppe de
  clip par scène, contrôleur). Une commande mappable par Pattern est un ticket ouvert (35).
- Chaînes : jusqu'à 16 Patterns (`ChainLength`), mode Chain de la bande, chaque Pattern une Pass, en boucle ; un
  Pattern choisi à la main l'arrête (`SetChain`, `ChainEntry`).
- Édition : Copy to (Pattern sur un autre), x2 (longueur doublée, pas recopiés), Clear, Length, Scale, Follow.
- Transport : esclave de Live. Position lue en ticks ITM de Live (480/noire) à chaque regard de l'adaptateur ; le
  noyau détecte départ, arrêt et saut (seek, boucle) et coupe alors toutes les notes tenues. Le pas n est planifié
  quand Live atteint n-1 ; une édition du pas planifié le replanifie (sauf Tracks déjà commencées). Grâce de 20 ms
  (`GraceMs`) pour envoyer en retard les Hits d'un départ ou d'un saut. Pas de transport propre, pas de pas de pause,
  pas de sens de lecture, pas de pas méta (hors cadre).
- Mesure de bout en bout (README, ticket 04) : en direct, depuis un clip imprimé ou fait à la main, les attaques
  tombent au même échantillon, 2 échantillons après la grille, sans gigue mesurable (Live 12.2.1, 44,1 kHz).

## Ce qu'il envoie à MoDule (ADR 0003 de MModule et amendements)

Tout est note MIDI, ni CC ni SysEx (une piste Live ne porte pas de SysEx).

| Notes | Rôle | Vélocité |
|---|---|---|
| 36-51 | Hit des Tracks 1-16 | vélocité du ratchet (courbe appliquée) |
| 64-87 | Locks des 24 paramètres, avec leur Hit | valeur : 1-63 → 0-62, 64-127 → elles-mêmes (`lockVelocity`) |
| 88 | Accent du Hit (Hit et son Link joués à 128 + 2 x montant) | montant, même échelle |
| 89 | Slide : clocks 24 ppqn jusqu'au Hit suivant de la Track, swing et micro-timing compris, arrondis | 1-127 |
| 90-113 | Valeur cible du Hit suivant, pour chaque paramètre que ce Hit verrouille et que le suivant verrouille aussi (sinon cible = Kit, côté MoDule) | même échelle |
| 114 | Clocks au-delà de 127 : clocks = v(89) + 127 x v(114) (max 3072 : 64 pas à 1/8x) | 1-127 |
| 124-127 | « hello » : 4 note-on canal 1, vélocité 1 + 6 bits d'un id de 24 bits ; MoDule écrit alors `%TEMP%/md-drums-seq/<id>.kit` | id |

- Canal : le device émet chaque Track sur son canal (status `0x90 | track`), mais Live ramène les notes d'un device
  Max sur le canal 1 (mesuré). D'où l'ordre imposé à chaque instant (`rankOf`) : note-offs d'abord, puis Track par
  Track le Hit suivi de ses Locks, de l'Accent, puis du Slide (89, cibles, 114) — MoDule lie un Lock du canal 1 au
  dernier Hit du canal 1 reçu juste avant au même échantillon.
- Les Locks sont réémis avec chaque ratchet ; l'Accent avec chaque ratchet d'un pas accentué ; le Slide avec le
  dernier ratchet seulement. Côté MoDule, Lock, Accent et Slide valent pour le Hit du même bloc de 32 échantillons ;
  sans Hit ils sont jetés. Le Hit suivant de la Track termine le Slide ; le Slide avance aux clocks MIDI du transport
  de l'hôte et s'arrête avec lui.
- En direct, un Lock (et les notes qui vont avec) finit aussitôt : Live garde au plus 128 notes tenues par device et
  quitte s'il déborde (ticket 24). Imprimé, chaque note dure la part du ratchet.
- Retour MoDule → device : le fichier Kit (nom du Kit, Machine par Track, noms SYN1-8, 24 valeurs du Kit), lu par le
  device pour nommer les Tracks (« 01 TRX-BD ») et montrer les valeurs du Kit sous les boutons de Lock. Le
  séquenceur ne change jamais le Kit (pas de Kit par Pattern : hors cadre).

## Impression, capture, sortie

- Print : le Pattern affiché sur 1 à 16 Passes (graine du dernier départ), ou la dernière prise, en un clip Session
  par Track sur une piste MIDI routée vers « N-MoDule » (canal gardé), créée si absente ; rien n'est écrasé.
- Capture : journal d'un million de notes, écrit sans allocation sur le fil du scheduler ; « Take n » en Session,
  Conditions telles que tirées, Fill, Mutes et bascules tels que joués. Vers l'Arrangement : ticket 23 ouvert,
  attend Live 12.4 (ticket 11).
- Interrupteur Device / Clips (paramètre « Output ») : sur Clips, plus de note-on vers MoDule, les clips jouent seuls.

## Interface

- Bande (169 px) : 16 Patterns (états vide/plein/joué/en file), Tracks et Mutes, 16 pas de la Track affichée par
  pages, FILL, Chain, Edit, Push, Capture, Clips only, ligne de mise à jour.
- Fenêtre : grille 16 x 32 (deux moitiés pour 64), rangée de trig (Swing / Accent / Slide), panneau du pas (Hit,
  vélocité, Condition, micro-timing, ratchets et courbe, 24 Locks nommés d'après la Machine).
- Push 2 (mode « à la Tuple », `grab_control`) : 32 pas en pads, Locks d'un pas tenu sur les 8 encodeurs (SYN1-8 /
  EFX / ROUTING), 16 Tracks façon Drum Rack, Mute, 16 Patterns, Delete, Duplicate, Double, Accent.
- Contrôle de mise à jour GitHub au plus une fois par jour.

## Ce qui n'existe pas (ou pas encore)

- Enregistrement live (Hits joués en temps réel, Locks pris des encodeurs) : non spécifié (map.md).
- Longueur par Track, scènes, pas méta, Kit par Pattern, export/import de Patterns : hors cadre.
- Durée de note du Hit (gate) : ce n'est pas un paramètre ; la note dure la part du ratchet (sans effet sonore chez
  MoDule, qui joue au note-on) [INFER : MoDule ignore les note-offs des Hits, d'après ADR 0003 qui ne parle que du
  note-on].
- Locks mappables depuis un contrôleur (ticket 34) et Patterns mappables (ticket 35) : ouverts.
- Probabilité/trig sans note (trigless lock) : pas de Lock sans Hit ; écarté par l'ADR (sur canal 1 un Lock sans Hit
  ne peut pas nommer sa Track).

## Portée pour l'instrument de la carte [INFER]

Pour un séquenceur interne, MoDule Seq donne déjà un modèle complet et testé (types de `mdseqTypes.h`, Conditions,
planification exacte en sous-ticks, ordre des événements) qui peut être repris sans la couche Max ni le contrat MIDI :
à l'intérieur du plug-in, Locks, Accent et Slide n'ont plus besoin de voyager en notes, et les contournements dus à
Live (canal 1, 63 non verrouillable, notes tenues limitées, hello + fichier Kit) tombent. Licence GPL-3.0+ (même
auteur que MoDule, AGPL-3.0) : à vérifier avant d'embarquer le code.
