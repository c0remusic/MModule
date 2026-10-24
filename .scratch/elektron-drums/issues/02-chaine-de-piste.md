# Chaîne de piste : emplacements, ordre, routage

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 

## Question

Quels emplacements
compte une piste, dans quel ordre, et avec quel routage ? Combien d'emplacements drive empilés, routables pré ou post
filtre ; l'emplacement bande (BASE/WIDTH ou EQ 3 bandes) ; l'emplacement filtre (8 types du multimode Syntakt, puis les
6 filtres du DT2) ; ampli, envois. Avec les types d'origine choisis à leur place, la piste reste exacte au firmware :
quelles positions le garantissent ?

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, réponses Q1-Q15). Ordres d'origine lus dans le code : Syntakt
machine, drive, BASE/WIDTH, filtre multimode, enveloppe d'ampli (`stTrack.h`) ; Machinedrum voix, AMD, EQ, filtre,
SRR, DIST (`mdEngine/engine/TrackFx.h`).

- Squelette fixe : machine + sample, drives pré-filtre, BAND, FILTER, drives post-filtre, AMP, envois. Seuls les
  drives bougent.
- Deux emplacements drive, chacun pré ou post filtre ; deux du même côté s'inversent librement.
- Types de drive : Off, Syntakt drive, MD DIST, MD SRR, DT2 Overdrive, DT2 Bit Reduction, DT2 SRR. Chaque
  type garde ses réglages d'origine, plus un MIX sec/traité ajouté (« + »), à 100 % par défaut.
- Suréchantillonnage des drives : réglage global off / 2x / 4x, 2x par défaut (HIIR en SIMD, ~0,4 % à 16 pistes
  d'après la recherche « Qualité audio »).
- BAND, avant le filtre : BASE/WIDTH du Syntakt, EQ du MD, ou EQ 3 bandes ajouté (« + » : plateau grave, cloche
  médium à Q fixe, plateau aigu ; fréquence et gain par bande, ±18 dB).
- FILTER : 15 types (8 Syntakt, 6 DT2, le filtre du MD). Enveloppe de filtre du Syntakt (délai, ADSR, profondeur,
  suivi de clavier) commune à tous les types ; sur le filtre MD elle déplace toute la bande (FLTF monte, FLTW suit),
  profondeur 0 par défaut.
- Chaîne par défaut de la famille au premier choix d'une machine (Syntakt : drive pré, BASE/WIDTH, filtre Syntakt ;
  MD : EQ MD, filtre MD, SRR puis DIST post) ; changer de machine dans la famille ne touche pas la chaîne ;
  changer de famille la recharge, en demandant si elle a été modifiée.
- AMP en fin de chaîne : enveloppe (AHD ou ADSR), niveau, pan, accent. Pour le MD, l'enveloppe reste dans sa voix
  (exacte) et l'étage AMP ne garde que niveau, pan, accent.
- Vélocité sur le niveau par défaut, plus une destination assignable par le menu « + » d'un knob, avec profondeur.
- Chaîne mono jusqu'au pan ; un sample stéréo en layer est sommé en mono.
- Envois (delay, reverb, chorus, phaser/flanger) après l'ampli.
- Amendé le 2026-10-24 (révélé par le mockup v5 : la chaîne MD comptait trois étages pour deux emplacements) :
  l'AMD du MD (modulation d'amplitude par oscillateur) n'est pas reprise, l'utilisateur la juge inutile. Une piste MD
  reste exacte tant que son AMD d'origine est à 0, sa valeur par défaut.
- Une piste dont la chaîne reste celle de sa famille reste fidèle à son firmware ; tout ajout (MIX, EQ 3 bandes,
  étage d'une autre famille) est marqué « + ».
