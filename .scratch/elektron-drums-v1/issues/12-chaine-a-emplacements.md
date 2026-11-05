# Chaîne de piste à emplacements

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 10, 11

## Quoi

La chaîne de la spécification §4 : deux drives pré ou post (Off, Syntakt drive, MD DIST, MD SRR ; DT2 Overdrive au
ticket 15), MIX « + » ; BAND avant ou après le filtre (BASE/WIDTH, EQ MD, EQ 3 bandes « + ») ; FILTER (8 types
Syntakt, filtre MD) avec l'enveloppe de filtre commune ; AMP complet (AHD/ADSR, niveau, pan, accent) ; vélocité
assignable avec profondeur ; chaîne par défaut de la famille ; types automatisables avec rampe anti-clic ;
suréchantillonnage global des drives off / 2x / 4x (HIIR SIMD), 2x par défaut.

## Critère de fin

- Une piste dans sa chaîne de famille rend les mêmes échantillons que son moteur seul (Q1 ou tolérance de l'étage).
- Q5 (neutre), Q6, Q7, Q9, Q12 passent.
- Coût de la chaîne à 16 pistes, drives 2x, mesuré.

## Références

Spécification §4. Ticket « Chaîne de piste ».

## Noté par le ticket 10 (2026-11-05)

- Un étage traite tout le `Block`, or la spec donne à chaque piste ses emplacements. Le plus simple : copier le bloc, traiter la
  copie, reprendre les voies choisies, à mesurer d'abord ; si le coût lâche, `process(Block&, uint16_t _lanes)`, qui saute les groupes
  sans voie choisie et mélange au `store`. Pas de classe de base.
- Les gains du DSP sont gardés, et ne s'annulent que dans l'ordre du firmware : EQ ×1/2, filtre ×1/4 avec 4 échantillons de retard,
  DIST ×8 et plus avant son écrêtage. Remplacer l'EQ par Off ou par l'EQ 3 bandes fait sauter le niveau : ce ticket possède un
  ajustement par emplacement, avec des constantes nommées. Mesurer le gain de DIST plutôt que le supposer.
- DIST, placé en pré sur une voix Syntakt pleine échelle : le décalage boucle à 56 bits, donc un échantillon à |gain × x| ≥ 0,5
  se replie sur le rail opposé. Gardé, c'est le son de l'étage, mais la chaîne doit le savoir.
- `set` ne limite pas : le code qui ajoute enveloppe et LFO reste dans 0 à $3FFF, sinon les tables sont lues hors bornes.
- Point plat de l'EQ : Kit 63, le 64 du boot fait déjà +0,24 dB.
