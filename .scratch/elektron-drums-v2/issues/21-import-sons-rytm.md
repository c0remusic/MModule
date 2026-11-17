# Import des sons Rytm dans la couche sample

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 06

## Quoi

Un son Rytm importé met son sample dans la couche sample, par traduction : TUNE ±24 dans les ±5 octaves, STRT/END
en STRT + LEN avec le sens, LEV au carré, fondu de fin. Format importé proposé : le dump SysEx d'un son Rytm (`.syx`),
à confirmer au début du ticket. Le sample lui-même vient du Locker de l'utilisateur, jamais du contenu d'usine.

## Critère de fin

- Test : sons Rytm de référence importés ; couche jouée contre la sortie de la couche du Rytm à l'oracle, par les
  tests Q.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §7 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
