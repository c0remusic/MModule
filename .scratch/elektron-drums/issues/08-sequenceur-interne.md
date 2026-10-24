# Séquenceur interne

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 06, 07

## Question

Quelles fonctions pour le séquenceur
interne, sur le modèle de MoDule Seq et des séquenceurs Elektron ? Relation avec le transport de l'hôte, le MIDI
entrant (qui reste possible), les locks par trig ; ce qu'un kit ou un pattern garde.

Exigence de l'utilisateur (2026-10-24) : dans la page du séquenceur, mapper des trigs facilement selon la page de
l'instrument sélectionné : la rangée de pas suit la piste choisie, et les boxes de sa page (machine, filtre, drive,
LFO...) restent sous la main pour poser des locks sur les pas (tenir un pas, tourner un knob, comme sur les machines
Elektron). À la souris on ne peut pas tenir un pas et tourner un knob : proposé (mockup v2) une box de pas fixe en
bas de chaque page de piste avec sélection verrouillée (clic sélectionne, tout knob tourné verrouille sur les pas
sélectionnés), des lignes de locks dessinables sur la page SEQ, et LOCK REC en lecture. L'utilisateur hésite entre
ces deux options (mockups v3a et v3b) et veut un mode « live » (enregistrement des trigs et des mouvements de knobs
en lecture) ; question ouverte : piloter le séquenceur depuis Push (Live garde Push ; un compagnon Max for Live peut
en prendre les contrôles, ou le mode User de Push envoie ses pads en MIDI à la piste). À trancher : comment la page du séquenceur montre la page de l'instrument (à côté, en dessous, la même
page avec la rangée de pas incrustée), sélection de plusieurs pas, et lecture des locks posés sur chaque pas.

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q13 ; recherches « Fonctions de MoDule Seq » et « Séquenceurs
des machines Elektron » ; mockups v3a à v5).

- Base : les fonctions de MoDule Seq, en interne, sans notes MIDI entre séquenceur et pistes : 65 conditions, ratchets
  1-8 avec courbe de vélocité, micro-timing ±23/24 de pas, swing 50-80 %, accent et slide par pattern, mutes, fill
  tenu, chaînes de patterns.
- Longueur et échelle par piste (polymétrie) ; une longueur de pattern commune fixe le changement de pattern.
- 128 patterns (8 banques de 16), 64 pas au plus par piste.
- Transport de l'hôte seulement (play, stop, tempo, position), pas d'horloge libre ; pattern choisi par un paramètre
  hôte automatisable « Pattern » ou par program change MIDI. Un pattern demandé part à la fin du pattern en cours.
- Le MIDI entrant (notes 36-51) joue toujours les pistes ; en mode LIVE il s'enregistre sur les pas, comme les
  mouvements de knobs (LOCK REC).
- Un pas porte : trig, note, vélocité, durée, condition, micro-timing, ratchet (nombre, vitesse, courbe), accent,
  slide, locks sur n'importe quel paramètre de la piste, sans plafond (stockage clairsemé). Le slide fait glisser les
  paramètres verrouillés vers les valeurs du pas suivant, comme sur le MD et le Syntakt.
- Conditions aléatoires tirées d'une graine par passage : un rendu ou un bounce rejoue ce qu'on a entendu.
- Trigs à l'échantillon près, sans gigue (amende « Une fréquence commune pour tous les moteurs », qui les plaçait au
  bloc : le micro-timing est plus fin qu'un bloc, et une gigue variable fait varier la phase des sons empilés) ;
  enveloppes et LFO avancent au bloc.
- Locks : box de pas fixe en bas de chaque page de piste avec sélection verrouillée, plus lignes de locks sur l'onglet
  SEQ, choisies dans la liste des paramètres verrouillables ; menu « + » d'un knob.
- Ajouté par l'utilisateur le 2026-10-24 : sur un pas vide sélectionné, tourner un knob crée un trig portant ce lock
  (Alt en tournant : un lock sans note) ; la valeur d'un trig se règle ensuite en glissant verticalement sur le pas
  (sa hauteur suit) ou en continuant de tourner le knob. Modes d'édition de la box de pas : Trig, Vel, Accent, Slide.
  En Vel, la hauteur d'un trig est sa vélocité et se règle en glissant ; en Trig, les trigs sont des blocs pleins et
  glisser sur un trig sélectionné règle la valeur du dernier paramètre verrouillé pour lui.
- Push : en deuxième temps (compagnon Max for Live, `grab_control` à vérifier) ; les 64 relais couvrent les knobs en v1.
- Édition en v1 : copier/coller de pas, de pistes et de patterns, effacer, décaler une piste ; euclidien et aléatoire
  plus tard. Export d'un pattern en fichier MIDI par glisser-déposer (trigs en notes 36-51 avec vélocité, sans les
  locks) ; pas de mode song, l'hôte arrange.
