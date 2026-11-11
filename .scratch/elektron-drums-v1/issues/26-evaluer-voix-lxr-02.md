# Évaluer le port des voix LXR-02

Type: HITL
Status: resolved
Assignee: 
Blocked by: 

## Quoi

Dire si les voix du LXR-02 d'Erica Synths peuvent devenir des machines d'une piste d'edDrums, à quel coût, et à quelles conditions. Rien n'est porté ici : ce ticket ne contient que l'évaluation.

Sources lues : `erica-synths/lxr-desktop-app` au commit `a43cd4f` (firmware, amont) et `mcamaradev/ELABS-RLX-02-LXR02-CLONE-VST` (plug-in macOS en Rust qui compile ce même commit). Les copies locales et le banc sont dans `C:\dev\elektron-work\oracle\lxr-eval\`, hors dépôt.

## Licence, vérifiée en amont

- GPL-3.0-only : fichier `LICENSE`, `NOTICE.md` et en-tête SPDX de chaque fichier lu, tables comprises (`Samples.c`, `wavetable.c`, `transientTables.c`), copyright Erica Synths 2012-2026.
- Compatible avec ce dépôt : AGPL v3 et GPL v3 se combinent par leur section 13, comme le dit déjà `NOTICE.md`.
- La règle « rien de dérivé du firmware dans le dépôt » vise les firmwares fermés d'Elektron. Ici le fabricant publie sources et tables sous licence libre : elles peuvent entrer, avec leur notice.
- Conditions de la section 7 du `NOTICE.md` d'Erica : les noms « Erica Synths », « LXR » et « LXR-02 » ne sont pas licenciés et ne doivent pas suggérer un lien avec Erica. Seule une mention factuelle de compatibilité est permise. Une version modifiée doit se présenter comme différente. Il faudra donc un nom de famille à nous dans l'interface, et une ligne dans `NOTICE.md` du dépôt.
- Le matériel n'est pas sous cette licence ; la façade n'est pas à copier.

## Ce que le code impose

- Quatre types de voix : Drum (trois exemplaires, oscillateur FM, transitoire, filtre, distorsion), Snare, Cymbal, HiHat (fermée et ouverte). Soit quatre machines, avec 247 paramètres pour le kit entier.
- Tout est en C et en `float`. Le code compile sous MSVC après trois retouches locales : neuf tableaux à taille variable (`int16_t mod[size]`) passés à une taille fixe, `__fp16` remplacé par `float`, et les `inline` du C99 neutralisés. `__fp16` ne sert que dans les effets, qu'on ne reprendrait pas. Tous les objets ont compilé ; l'édition de liens restait à vérifier quand l'exécution a été refusée.
- Le moteur vit dans des globales : `voiceArray[3]`, `snareVoice`, `cymbalVoice`, `hatVoice`. Le clone contourne cela en chargeant une copie de la DLL par instance. Pour 16 pistes il faut passer ces structures en paramètre. C'est mécanique mais ça touche chaque voix : environ 100 occurrences de `voiceArray[` dans `DrumVoice.c`.
- Les paramètres arrivent par `MidiParser.c` (55 ko), qu'on ne reprendrait pas : il faut une table des paramètres de voix vers leurs champs.
- Le moteur tourne par blocs de 64 échantillons (`OUTPUT_DMA_SIZE`), 1,45 ms, avec les enveloppes de volume calculées par bloc (`AMP_EG_SYNC 0`) puis lissées. Le code a une variante à 32 ; `edDrums` travaille à 32. Reste à vérifier que passer à 32 ne change pas la vitesse des enveloppes : aucune constante de `Decay.c` ni de `SlopeEg2.c` ne cite la taille de bloc, mais leur cadence d'appel en dépend.
- Chaque voix sort en `int16_t` : 16 bits, avec un dither dans le mixeur.
- `REAL_FS` vaut 44 140 Hz, soit 1,6 cent de haut si on joue à 44,1 kHz sans rien changer. Le passer à 44 100 corrige la hauteur des oscillateurs, reste à vérifier les filtres.
- Une piste devient une troisième `Family` à côté de `Syntakt` et `Machinedrum`, dans `edDrumsEngine.h`.

## Mesuré

Banc `C:\dev\elektron-work\oracle\lxr-eval\h\bench.c`, lancé avec l'accord de l'utilisateur le 2026-11-07, sur le kit d'init, MSVC `/O2 /fp:fast /arch:AVX2`, à travers le pont du clone, sur une copie locale du firmware avec les trois retouches ci-dessus et `FX/Distortion.c` renommé pour une collision de noms.

- Une voix seule, sans mixeur ni effets, 64 échantillons, redéclenchée toutes les 96 blocs : Drum 2,15 à 2,19 µs, Snare 1,71 µs, Cymbal 1,90 µs, HiHat 2,01 µs, p99 sous 2,8 µs. Soit environ 1 µs pour nos 32 échantillons, dans la plage de P-I (0,44 à 1,89 µs).
- Seize voix : environ 35 µs sur 1 451 µs de budget, 2,4 % d'un cœur à 44,1 kHz, hors chaîne de piste. Mesure extrapolée de la voix seule, pas seize voix réelles.
- Moteur complet, six voix et effets : 14,0 µs par bloc au repos, à peine plus quand elles jouent (14,2 µs). Les voix calculent même silencieuses : le port devra les arrêter comme le font les autres machines.
- Les pires blocs, jusqu'à 3,2 ms, restent à expliquer ou à attribuer à la machine : banc lancé sans machine calme, sur un cœur fixe.
- Départ : la voix sonne dès le premier échantillon du bloc qui suit sa note, 1 % de la crête à 0 ou 1 échantillon, 10 % à 4 au plus. Rien à ajouter à `Engine::HitDelay` (81) : la voix attendra dans `AlignedStart` comme les autres. Une note ne tombe qu'en tête de bloc dans ce banc ; la précision à l'échantillon reste celle d'`edDrums`.
- Attaques franches : saut de 1,07 sur une crête de 1,19 pour Cymbal, 0,62 sur 0,67 pour HiHat, 0,60 sur 1,24 pour Snare, 0,05 pour Drum 1. C'est le son du moteur d'origine, à ne pas lisser sans écoute.

## Non mesuré

- Passage aux blocs de 32 : vitesse des enveloppes à comparer au rendu de 64.
- Effet de `REAL_FS` à 44 100 sur les filtres.
- Écoute.

## Plan si l'utilisateur dit oui

1. Port de la voix Drum seule en machine de piste, structures passées en paramètre, blocs de 32 : rendu comparé au bit près au moteur d'origine à paramètres égaux, sans quoi l'écart se nomme.
2. Snare, Cymbal, HiHat.
3. Table des paramètres, valeurs de kit, page d'interface, nom de famille, `NOTICE.md`.

## Fait, 2026-11-11 (l'utilisateur a dit oui)

- Port dans `source/elektron/opendrum/` (C, `/fp:precise`), famille OPEN DRUM, machines OD DRUM, OD SNARE, OD CYMBAL, OD HIHAT ; licence et changements dans son `README.md`, section dans `NOTICE.md`.
- Blocs de 64 gardés : une voix rend 64 échantillons un bloc de 32 sur deux, un Hit la relance au bloc suivant. `REAL_FS` reste 44 140 (1,6 cent haut à 44,1 kHz).
- `edDrumsOpenDrumTest` : 25 rendus de l'original (`opendrumRefs`, hors dépôt, `OPENDRUM_REFERENCES`) identiques au bit près ; 180 Hits à travers le moteur, notes partout dans le bloc, vélocités 127 et 64 : mêmes échantillons, premier échantillon à `HitDelay` (81), gigue 0 ; voix arrêtée quand son enveloppe est fermée et 64 blocs muets ; rien d'alloué ; environ 1 à 1,3 µs par voix et 32 échantillons.
- Machine 1-24 : jusqu'à 24 paramètres par machine (22, 21, 22, 23), les MD et Syntakt gardent 8 ; IDs `tNN_m9` à `tNN_m24` ajoutés en fin de liste.
- Non fait : écoute ; les LFO et la modulation de vélocité de la voix d'origine ; PWM n'agit que sur l'onde PWM ; REPEAT garde ses 0-127.

## Écarté : ReplicantTr8s

`DanimanX/ReplicantTr8s` (MIT, TR-8S) a été lu pour la même question. Ses voix ne sont pas un moteur à porter : `ACBDrumVoice.cpp`, 1 215 lignes, somme des sinus et des exponentielles écrits à la main, sans rien de la machine, quoi qu'en dise son « 1:1 ». Le firmware Roland n'y fournit que kits, patterns et noms, lus à l'exécution dans un fichier de l'utilisateur. Rien à y prendre : le son ne vient pas de la machine et les données ne pourraient pas entrer dans le dépôt.

## Décision et port (2026-11-11)

L'utilisateur a dit oui. Les quatre voix sont portées sous le nom OPEN DRUM (`source/elektron/opendrum/`, section de
`NOTICE.md`), rendues au même échantillon que le code d'origine (25 rendus sur 25), à 1,0-1,3 µs par voix et bloc de
32. Le passage aux blocs de 32 garde la vitesse des enveloppes : la voix rend ses 64 échantillons un bloc sur deux.
Restent : l'écoute, la modulation par vélocité et les LFO propres des voix, et un désaccord de 1,6 cent (la voix
garde ses 44 140 Hz).

## État (2026-11-11) : SOPHIE devient une famille

La voix de Sophie (`source/elektron/sophie/`, MIT, section de `NOTICE.md`) joue comme famille SOPHIE, dernier choix de
Machine, sans que rien avant ne bouge. Une voix par piste sur les blocs, anneaux et chaîne du Machinedrum, au pad 1
(note 36) pour toutes les pistes, sans paramètre PAD. Machine 1-19 sont les paramètres de Sophie, Machine 20 BODY (le
corps sinus du kick, modèles 0-3, sur toute piste ; actif par défaut). Les 16 pads d'usine sont 16 sons
(`sophie::soundValues`, le pad compté dans Tune) offerts dans le bloc Machine ; une piste mise sur SOPHIE part sur KICK.
Gain de sortie 0,63 : le kick crête à 0,614 contre 0,629 (OD DRUM) et 0,601 (BD MODERN), sans chaîne.

`edDrumsSophieTest` : les 16 sons, 192 Hits (notes partout dans le bloc, vélocités 127 et 64) donnent les échantillons
de `sophie_render` seul à paramètres égaux, premier son à `HitDelay` (81) après la note, gigue 0 (le premier
échantillon de la voix, nul à la sortie du passe-bas, compte comme départ d'un échantillon) ; BODY coupé change le kick
de +1,6 dB d'énergie d'écart ; Machine 1-20 fixés ou lockés pareil ; voix lâchée une fois éteinte (304 à 1 649 ms selon
le son) ; rien d'alloué ; 1,7 à 4,9 µs par voix et 32 échantillons seule, 71 µs pour 16 pistes. `sophieTest` reste au
bit près. Non fait : l'écoute, les unités des valeurs à l'écran, et les sons d'usine dans la bibliothèque de sons du
ticket 19, proposés dans le bloc Machine à la place pour ne pas toucher ce travail en cours.
