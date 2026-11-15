# Circuit de l'overdrive analogique du Syntakt

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Que sait-on, de sources publiques, de l'overdrive analogique du master du Syntakt : sa place dans le chemin (avant ou
après les sorties numériques, ce qu'il reçoit des pistes analogiques et numériques), ses réglages (ceux du manuel), le
type de circuit (diodes, transistors, ampli op, alimentation), et tout enregistrement public à réglages connus qui
permettrait de régler un modèle à l'oreille ? Le firmware ne le pilote-t-il que par un niveau (registre, DAC), et où ?
Le modèle qui en sortira est approximatif : la question est ce qu'il doit reproduire, pas le circuit au composant près.

## Answer

Détail et sources : `../research/01-circuit-overdrive-syntakt.md`.

- Pas d'overdrive « de master » au Syntakt : l'étage visé est le DRIVE du bloc Analog FX (piste FX), un insert
  stéréo analogique DRIVE → FILTRE → AMP sur un bus à part, avant le mix principal (manuel OS 1.42, §5.4).
  L'OVER des pistes 1-8 est numérique, celui des pistes 9-12 analogique mais dans la voix.
- Entrées routées une à une (pistes 1-12, delay, reverb, entrée externe, USB) ; les pistes numériques y arrivent par
  DAC (bus 0x8000DFD0). La sortie va au mix et revient par ADC 6-7 vers les envois delay/reverb [INFER].
- Un seul réglage, DRIVE 0-127 (CC 15, NRPN 1:29), lockable. Circuit inconnu : pas de schéma ni de teardown publics.
- Le firmware ne fixe qu'un gain : un CV dans la trame DSPI de 40 CV, indice non identifié (34 en candidat [INFER]).
- Pas d'enregistrement public isolé à réglages connus. À reproduire, d'après les avis : saturation qui dépend du
  niveau d'entrée, grave gardé aux réglages moyens, compression et perte de grave au maximum, résonance aval écrasée,
  bruit qui monte avec le gain. Le modèle reste marqué « ≈ ».
