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
