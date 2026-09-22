# Go/no-go étayé

Type: grilling
Status: open
Blocked by: 01, 04, 05, 06

## Question

Verdict avec l'utilisateur, chiffres en main : le modèle de découplage retenu
atteint-il la parité Virus (baseline du 01) pour un coût d'implémentation et
un risque acceptables ? Alternatives à poser en face : rester sur l'acquis
(-10 % + freeze), upgrade CPU (~2× single-thread), attendre/impliquer l'amont.
Gate : la spec (08) ne s'écrit que sur un go explicite.

Deux points imposés par la résolution du 04 :

- Re-cadrer la métrique de parité AVANT le verdict : « coût CPU thread audio »
  est gagnable trivialement en vidant le thread audio ; proposition = cœur le
  plus chargé (transport seul : DSP1 ≈ 31,7 pts, plafond 34-37 % realtime,
  ×2,5-2,9).
- Statuer sur l'étage 2 (fast-forwards par composant : DSP1 −27 %, UC −20 % ;
  trampoline batch 128, maxDoIterations MM 4→64, fast-forward attente-ESSI) :
  requis pour la parité ×4,1, actuellement hors périmètre de la carte — le go
  doit dire si on re-charte cet étage ou si on accepte le plafond transport.
