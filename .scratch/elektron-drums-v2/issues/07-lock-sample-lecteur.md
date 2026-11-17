# Lock de sample sur le lecteur du Sampler

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 04

## Quoi

Un lock de sample fait jouer à un pas un autre sample du Locker, sur les trois lecteurs du Sampler. Il nomme le
sample par son empreinte : ni banque, ni numéro, ni limite. Il se pose depuis le menu de lock du pas. Lockstep charge
seul, hors du fil audio, les samples que les patterns d'une piste nomment, partagés par empreinte dans le processus ;
le fil audio passe d'un sample chargé à un autre sans allocation. Seuls les locks changent le sample, à leur trig : ni
cible de LFO, ni paramètre hôte. Sample absent : le pas joue le sample de la piste, le lock reste et rejoue dès que la
même empreinte revient ; la ligne d'état nomme ce qui manque. Le lock vit dans l'état, les Kits et les patterns.

## Critère de fin

- Test : un pattern à quatre samples locks joue chacun à son pas, à l'échantillon près, sans allocation sur le fil
  audio.
- Sample absent puis rendu : comportement de la spécification, ligne d'état comprise.
- État sauvé et relu : mêmes locks, même son.
- Latence et gigue rapportées avant et après.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.3 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
