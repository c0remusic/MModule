# Paramètres hôte et sorties

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 02, 04

## Question

Avec 16 pistes, une chaîne à
emplacements et des machines de plusieurs familles, le nombre de paramètres explose. Quelle stratégie d'automation
(paramètres par piste figés, relais vers la piste montrée comme dans MoDule pour Push, pages) ? Quelles sorties (Main
et 15 pistes, comme aujourd'hui, ou autre) ?

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q5).

- Tout réglage de piste, d'effet et du master est un paramètre hôte automatisable (~75 par piste, ~1 260 au total),
  identifiants figés : on ajoute, on ne renumérote jamais (règle de MoDule). Paramètres de machine génériques
  (« T01 Machine 1 » à « 8 ») : le nom reste, la valeur s'affiche avec le texte de la machine (« Decay 64 ») ; pas de
  renommage à la volée, que les hôtes suivent mal.
- Machine et famille ne sont pas automatisables (elles se changent dans le kit : une automation ne remet pas de défauts
  ni ne retire de locks en cours de morceau). Types de filtre et de drive automatisables, avec rampe anti-clic.
- 64 relais vers la piste affichée, 8 pages de 8 qui suivent les boxes (Machine, Drive, Band, Filter, Amp, LFO 1,
  LFO 2, Sends), pour Push, sur le modèle de MoDule.
- Sorties : Main et 15 sorties stéréo (Live ouvre au plus 16 bus par plug-in) ; chaque piste sur Main ou sur
  n'importe laquelle des 15.
- Une piste sur une sortie séparée sort avec sa chaîne (drives, filtre, ampli), sans les effets d'envoi, et alimente
  quand même les envois ; chaque retour d'effet (delay, reverb, chorus, phaser/flanger) va sur Main ou sur une sortie
  choisie ; compresseur et overdrive du master sur Main seulement.
