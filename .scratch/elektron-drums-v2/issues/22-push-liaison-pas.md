# Compagnon Push : liaison et pads de pas

Type: AFK puis HITL (Push en main)
Status: ready-for-agent
Assignee:
Blocked by: 01

## Quoi

Un device Max for Live devant Lockstep sur sa piste, pour Push 2 et Push 3 en mode contrôleur. Il se présente à
Lockstep par des notes MIDI, comme MoDule Seq ; Lockstep écoute alors sur un port UDP local propre à l'instance, par
lequel le compagnon lit et écrit les patterns. Pads : les 16 pas de la piste affichée sur deux rangées de 8, trig posé
ou retiré à la vélocité du pad, couleur des pads pour lire les pas. Armé, il prend `Button_Matrix`, `Track_Controls`
et `Track_Select_Buttons`, et les lâche désarmé ; il suit `pad_layout` et repeint. Un seul compagnon arme Push à la
fois. Le compagnon ne joue pas les notes : le séquenceur interne joue à l'échantillon près. Rien sur l'écran de Push.

## Critère de fin

- Test sans Push : un client UDP de test lit et écrit un pattern ; le séquenceur joue ce qui est écrit.
- Deux instances : chacune son port, aucune ne lit l'autre.
- Essai de l'utilisateur sur Push : pas posés et retirés, armement et désarmement.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
