# Séquenceur interne

Type: grilling
Status: open
Assignee:
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
