# Overdrive analogique du Syntakt : ce que disent les sources publiques

Recherche du 2026-11-15, ticket `../issues/01-circuit-overdrive-syntakt.md`. Sources consultées le 2026-11-15.
Tags : **[DOC]** document Elektron ; **[CODE]** dépôt public (SyntHack OS, MIT) ; **[LOCAL]** nos notes de
rétro-ingénierie (`../../elektron-engines/`), adresses et descriptions seulement ; **[FORUM]** avis d'utilisateur,
non vérifié ; **[INFER]** déduction de ma part.

## 0. Ce qu'il faut corriger dans la question

Le Syntakt n'a **pas d'overdrive « de master »** au sens du Digitone ou du Digitakt II (un étage numérique fixe sur
la sortie). Il a trois overdrives distincts :

| Étage | Où | Nature | Réglage |
|---|---|---|---|
| OVER des pistes numériques 1-8 | dans la voix (schéma §5.1, avec le filtre base-width) | **numérique** | OVER, « gain amount into the digital overdrive » (manuel, annexe A) |
| OVER des pistes analogiques 9-12 | dans la voix, entre le générateur et le filtre | analogique, mono, par voix | OVER, « gain amount into the analog overdrive » (annexe A.3/A.4) |
| **DRIVE du bloc Analog FX** | insert stéréo sur un bus, avant le mix principal | analogique, stéréo | DRIVE, page SYN de la piste FX (§12.3.8) |

Le « overdrive analogique au master » de la carte (jalon 1) ne peut être que le **DRIVE du bloc Analog FX** : c'est le
seul étage analogique qui traite des pistes numériques. Le reste de cette note porte sur lui ; l'OVER des voix
analogiques relève des machines analogiques (jalon J4 de la spec v1), pas de ce jalon.

Sources : Syntakt User Manual OS 1.42 [DOC],
https://www.elektron.se/wp-content/uploads/2026/10/Syntakt-User-Manual_ENG_OS1.42_260930.pdf, §5.1-5.4 p. 17,
annexe A ; page produit [DOC], https://www.elektron.se/en/syntakt (« Analog stereo overdrive », « Analog stereo
multimode filter » pour la piste FX ; « 1 × digital overdrive per track » pour les pistes numériques ; « 1 × analog
overdrive per track » pour les pistes analogiques).

## 1. Place dans le chemin du signal

**Manuel [DOC] :**
- §5.4 p. 17 : le bloc Analog FX « primarily consists of three analog parts, the drive, the multimode filter and an
  amplifier ». Le schéma de la page donne l'ordre **DRIVE → FILTER → AMP → MAIN MIX**. Les sorties des voix, les
  entrées externes et les retours d'effets passent chacun soit par le bloc (pre), soit directement au mix (post).
- §5.4.1 p. 18 : menu ANALOG FX BLOCK ROUTING ([FUNC] + [FX]) : pistes 1-12 une par une (TRIG 1-12), delay (14),
  reverb (15), entrée externe (16). Une piste MIDI ne se route pas. Même réglage par piste : ROUT, AMP page 2
  (§10.8.3). Entrée externe : IN, PRE/POST (§13.6). USB IN : PRE-FX ou POST-FX (§14.6.4).
- §12.6.10 : quand le delay ou la reverb est routé dans le bloc, les envois DEL/REV de la piste FX sont coupés
  « to prevent an audio feedback loop ». Donc la piste FX **envoie sa sortie au delay et à la reverb** (§12.6.7-8).
- §13.5 : niveau global FX sur la page MIXER 2 ; une piste routée au FX a une barre de niveau rayée.
- §14 (ROUTE TO MAIN, PRE/POST FADER) : les TRACK OUTPUTS (flux Overbridge par piste) sont « always without any effects ».

**Firmware, CPU #1 [LOCAL] (`../../elektron-engines/cpu1.md` §4, `machines.md`) et SyntHack [CODE]
(https://github.com/karigardarsson/Synthack-OS, `docs/audio-path.md`) :**
- Deux bus numériques sortent par les DAC : **0x8000DAD0** (pistes numériques non routées + retours non routés, le
  direct) et **0x8000DFD0** (pistes numériques avec ROUT + retours routés au FX). Le second entre dans le bloc
  analogique ; « nothing digital follows » les VCA de master (cpu1.md §4 pas 7-8).
- Les pistes analogiques 9-12 restent analogiques jusqu'au bloc ; elles sont aussi numérisées (ADC 0-3) pour les
  envois et Overbridge.
- ADC 6-7 : « analog mix bus L/R, before the master VCAs » (SyntHack). Notre lecture : le firmware s'en sert comme
  « FX track input », envoyé au delay et à la reverb (+0x742, +0x744) et mis dans le flux Overbridge principal
  (canaux 0-1 via 0x8000DED0, canaux 14-15 bruts). [INFER] C'est ce qui permet à la piste FX d'envoyer au
  delay/reverb : la sortie du bloc analogique revient en numérique par l'ADC.
- Après le bloc : VCA de master L/R (CV 35/36), recalculés chaque bloc (0x400904B2-0x4009055E, SyntHack) ;
  SyntHack s'en sert pour un compresseur de bus « on the analog master (via the master VCAs) ». [INFER] L'AMP du
  bloc (enveloppe, VOL, PAN) ou le niveau principal passe par ces VCA ; non tranché.

**Ce que le modèle doit reproduire :** un insert stéréo sur un bus d'envoi dédié, pas sur la sortie. Entrées :
pistes routées (numériques et analogiques), entrée externe, retours delay/reverb si routés ; sortie vers le mix
principal **et** vers les envois delay/reverb (sans boucle). Les sorties par piste restent sèches.

## 2. Réglages (manuel)

- **DRIVE** (§12.3.8) : seul paramètre de la page SYN de la piste FX ; « sets the amount of analog drive that is
  added to the audio routed through the FX track ». Valeurs 0-127 (SyntHack `ui-parameters.md` : id 150, 0..0x7F00 ;
  le manuel ne donne pas d'échelle en dB). CC 15, NRPN 1:29 (annexe B.10). Lockable, cible de LFO, global possible
  (OS 1.10). Pas de réglage de ton, de mix ni de niveau de sortie propre au drive.
- **FILTER** (§12.4-12.5) : ATK, DEC, SUS, REL, FREQ, RESO, TYPE (4-pole LP, 2-pole LP, BP, HP, Notch, Double-Notch,
  LP-Notch, OFF), ENV (bipolaire), DEL (retard d'enveloppe), FENR, **FPAN** (décalage de coupure opposé L/R, OS
  1.40). Loi de coupure ≈ 14,645 Hz · e^(0,05741 · FREQ) (SyntHack, mesurée sur l'appareil, partagée avec les pistes
  9-12).
- **AMP** (§12.6) : ATK, HOLD, DEC, SUS, REL, DEP, MODE (AHD/ADSR), enveloppe d'amplitude **inversée** à HOLD fixe
  (schéma p. 65), DEL et REV (envois), PAN, VOL (lockable). [FORUM] L'enveloppe inversée sert de « sidechain » :
  sortir le kick du bloc et déclencher la piste FX avec lui (https://www.elektronauts.com/t/your-kicks-on-the-syntakt/189626).
- Release notes [DOC] (https://elektron.se/?p=2682) : rien sur le caractère du drive ; des correctifs de routage et
  d'enveloppe de la piste FX (1.01 à 1.40A).

## 3. Type de circuit

**Rien de public.** Pas de schéma, pas de teardown qui identifie l'étage. Le fil « Syntakt PCB » (Elektronauts, avril
2022, https://www.elektronauts.com/t/syntakt-pcb/171667 et `?page=2`) identifie deux ColdFire, deux Spartan
(XC3S200A), la DRAM, un convertisseur Cirrus et « an SSI2164 » (quadruple VCA) ; aucun ampli op, transistor ou diode
du drive. [INFER] Un SSI2164 irait bien aux VCA pilotés par CV (master 35/36, AMP du bloc) ; rien ne dit lequel.
[INFER] « Analog stereo overdrive » : deux canaux identiques commandés par un même CV (DRIVE n'a pas de pan) ; le
circuit (diodes en contre-réaction d'ampli op, transistors) reste inconnu.

## 4. Caractère sonore rapporté (aucune mesure publique)

Avis [FORUM], fil « Elektron Syntakt - live user reflection », https://www.elektronauts.com/t/elektron-syntakt-live-user-reflection/182793
(pages 1 et 2, novembre 2022) :
- Dépend du niveau d'entrée : l'effet n'apparaît qu'avec des pistes poussées près du maximum ; à bas niveau,
  « mild clipping » (Humanprogram, 22/11/2022 ; Gino, 17/11/2022). La vélocité ajoute du niveau.
- Garde le grave aux réglages moyens : « doesn't thin out the sound like… a distortion pedal » (pselodux, 7/11/2022).
- Au maximum, sur une grosse caisse : le niveau et le grave **baissent**, et la résonance du filtre est « complètement
  domptée » (sezare56, 7/11/2022). [INFER] Compression forte (sortie à niveau quasi constant) et filtre après le drive
  qui reçoit un signal écrêté ; une perte de grave suggère un couplage ou un passe-haut dans l'étage.
- Du doux au dur ; à 127 « it distorts everything » (Jeanne, 7/11/2022) ; « a volume bump at best » pour un autre.
- Bruyant à fort gain (sezare56, aaanicholls, 12/11/2022), comparé à l'Analog Heat ou à l'« Analog Drive »
  (pédale d'Elektron, [INFER], le fil ne le précise pas).

## 5. Enregistrements publics à réglages connus

**Aucun ne convient.** Les démos (Reverb, Synthtopia, revue de Ziv Eliraz à 23:10 : https://www.synthtopia.com/content/2022/04/21/elektron-syntakt-in-depth-review/ ;
https://reverb.com/news/video-elektrons-new-syntakt-is-the-analog-digital-drum-machine-of-dreams) montrent le bloc
sur des patterns, réglages non lisibles et son non isolé. Même constat que `../../elektron-drums/research/14-analogiques-sans-machine.md`
§3 pour les voix analogiques. Le seul chemin propre reste celui de `../../elektron-engines/analog-literature.md`
(« Recordings to make », point 5) : un Syntakt, sinus et bruit sur INPUT L/R en PRE, DRIVE × niveaux d'entrée, FILTER
OFF, enregistrés en sortie principale.

## 6. Le firmware ne pilote-t-il qu'un niveau ?

Oui, autant que les sources le montrent :
- DRIVE est un mot de paramètre (piste FX, id 150, 0..0x7F00 ; DRIVE à 0x800028F0 dans la copie moteur selon
  SyntHack). Aucun code CPU #1 ne traite l'audio pour le drive : le bus 0x8000DFD0 part tel quel aux slots DAC 5 et 3
  (`cpu1.md` CORR 4.8), et le résultat analogique revient par l'ADC 6-7.
- Les CV partent chaque bloc dans une structure de 1000 octets (40 CV) par DSPI1, eDMA canal 15 (0x40101FF0 à
  0x400A5304, `machines.md` ; SyntHack). Indices publiés par SyntHack : 0-9 niveaux voix et entrée externe L/R, 10-33
  coupures, résonances, accords et niveaux des voix, **34 non listé**, 35/36 VCA de master L/R, 37/38 coupure du filtre
  de master. [INFER] 37/38 = le filtre du bloc FX (deux coupures pour FPAN). [INFER] Le CV du DRIVE est l'un des
  indices non attribués (34 en premier candidat) ; non vérifié. Le routine de contrôle de la piste FX est dans
  0x400A4F08-0x400A4FB4 (`0x4008EE54`, `0x400962E8`, `0x40095DA4`, CV `0x400903CA`), non décodée.
- Les enveloppes du filtre et de l'AMP du bloc passent par le générateur d'enveloppe matériel (tables de vitesses
  0x401DE254, 0x401DE054), comme celles des voix.

Donc : **le firmware fixe un gain (un CV) ; toute la non-linéarité est dans le circuit.** Rien à porter côté code pour
le drive lui-même ; à porter pour l'ensemble du bloc : routage, lissage des CV, loi du filtre, enveloppes.

## 7. Ce que le modèle approché doit reproduire

1. **Topologie** : insert stéréo sur un bus FX à part, DRIVE → FILTRE (7 types + OFF, FPAN) → AMP (enveloppe AHD/ADSR,
   inversée possible, VOL, PAN) → mix principal, sortie aussi envoyée au delay/reverb ; routage par piste, retours,
   entrée externe ; sorties par piste sèches.
2. **Un seul réglage**, DRIVE 0-127, monotone, lockable et modulable par LFO sans clic (CV lissé [INFER]).
3. **Dépendance au niveau** : la saturation dépend du niveau d'entrée ; à DRIVE bas, presque transparent sauf pour des
   pistes chaudes ; le niveau des pistes et la vélocité changent le grain.
4. **Comportement à fort DRIVE** : compression marquée (niveau de sortie qui ne monte plus, voire baisse), grave qui
   recule, résonance du filtre aval qui « s'écrase » ; écrêtage doux, harmoniques surtout impaires si symétrique
   [INFER], plus un bruit de fond qui monte avec le gain.
5. **Pas d'oracle** : marqué « ≈ » comme la carte le prévoit ; jugé à l'oreille et contre les avis ci-dessus, jusqu'à
   ce qu'un Syntakt permette la mesure par l'entrée externe (§5).

## Sources

- Elektron, Syntakt User Manual OS 1.42 (2026-09-30), https://www.elektron.se/wp-content/uploads/2026/10/Syntakt-User-Manual_ENG_OS1.42_260930.pdf — §5, §10.8.3, §12, §13.5-13.6, §14.6, annexes A, B.10. Consulté le 2026-11-15.
- Elektron, page produit Syntakt, https://www.elektron.se/en/syntakt. Consultée le 2026-11-15.
- Elektron, notes de version du Syntakt, https://elektron.se/?p=2682. Consultées le 2026-11-15.
- SyntHack OS (MIT), https://github.com/karigardarsson/Synthack-OS : `docs/audio-path.md`, `docs/ui-parameters.md`. Consultés le 2026-11-15.
- Elektronauts, « Syntakt PCB », https://www.elektronauts.com/t/syntakt-pcb/171667 (pages 1-2, avril 2022). Consulté le 2026-11-15.
- Elektronauts, « Elektron Syntakt - live user reflection », https://www.elektronauts.com/t/elektron-syntakt-live-user-reflection/182793 (pages 1-2, novembre 2022). Consulté le 2026-11-15.
- Elektronauts, « Your kicks on the Syntakt », https://www.elektronauts.com/t/your-kicks-on-the-syntakt/189626. Consulté le 2026-11-15 (résultat de recherche, page non ouverte).
- Synthtopia, revue de Ziv Eliraz, https://www.synthtopia.com/content/2022/04/21/elektron-syntakt-in-depth-review/ ; Reverb, https://reverb.com/news/video-elektrons-new-syntakt-is-the-analog-digital-drum-machine-of-dreams (résultats de recherche, vidéos non regardées).
- Notes locales : `../../elektron-engines/cpu1.md` §4, `machines.md`, `analog-literature.md` ; `../../elektron-drums/research/14-analogiques-sans-machine.md`.
