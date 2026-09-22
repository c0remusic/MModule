# Go/no-go étayé

Type: grilling
Status: resolved
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

## Answer

Ratifié par l'utilisateur le 2026-09-22 (réponses A / A / A — go).

1. **Métrique de parité re-cadrée : cœur le plus chargé** vs OsTIrus 23 %
   realtime. L'ancienne formulation « coût CPU thread audio » était gagnable
   trivialement en vidant le thread audio.
2. **GO pour la refonte transport** selon le modèle Convoi (04), le service
   HI08 (05) et la validation MM (06). Livraison attendue : ~34-37 % realtime
   (×2,6-2,8 depuis 94 %), chemin critique DSP1 ≈ 31,7 pts. Alternatives
   écartées :
   - Statu quo (acquis −10 % + freeze de pistes) : insuffisant pour l'usage.
   - Upgrade CPU (~2× single-thread) : ne résout pas, coût matériel.
   - Attente/implication amont : partage prévu APRÈS la spec (cadrage Q3=B).
   - « Engines-only » sans UC/séquenceur (question de session, analogie
     Overbridge) : fausse piste — le séquenceur ne pèse que ~6 pts d'UC
     (24,7 % idle vs 30,5 % playback), le skip idle mange déjà 82 % des
     cycles UC, supprimer l'UC ne baisse pas le plafond DSP1, et l'approche
     exige la rétro-ingénierie du protocole HI08 (triggers, kits→paramètres,
     upload UW, mixer) vivant dans le firmware ColdFire, avec risque de
     fidélité. Convoi sort déjà l'UC du chemin critique pour bien moins cher.
     Nuance (précisée par l'utilisateur) : l'objectif réel est UX — UI type
     Overbridge et séquençage dans Ableton. Cet objectif n'exige PAS de
     retirer l'UC : l'émulation intacte se pilote déjà par MIDI (notes par
     piste, paramètres CC/NRPN, kits par sysex) et une UI moderne est une
     surcouche plugin qui parle ces protocoles. Chantier UI séparé, hors de
     cette carte, orthogonal au transport.
3. **Étage 2 = effort séparé, charté après la spec** : fast-forwards par
   composant (DSP1 −27 %, UC −20 % ; trampoline batch 128, maxDoIterations
   MM 4→64, fast-forward attente-ESSI conditionné à un profil JIT préalable).
   Requis pour la parité ×4,1 complète ; le go assume que CETTE carte livre
   ~34 %, la parité 23 % venant du second effort, instruit par les mesures
   que le transport parallèle rendra possibles.

Gate levée : la spec (08) peut s'écrire.
