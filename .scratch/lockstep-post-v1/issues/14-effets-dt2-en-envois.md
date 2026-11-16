# Les effets du Digitakt II en envois

Type: grilling
Status: resolved
Assignee:
Blocked by:

## Question

La carte a décidé que les effets du DT2 (delay, reverb, chorus) entrent comme types des envois existants, après les
types actuels, pour que les sets gardent leur choix ; leur guide est dans les 0,7 % des envois (« Répartition du budget
CPU entre les jalons »). Pour chacun : quels réglages il expose, ce qu'il partage avec les types actuels (envois de la
v1, ticket 14 de `../elektron-drums-v1/`) et ce qui lui est propre, son ordre dans les choix, et la façon de le juger
(oracle du DT2 et tests Q, ou oreille seule là où l'oracle ne le rend pas).

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling), sur les faits de [research/14-effets-dt2.md](../research/14-effets-dt2.md)
(manuel de l'OS 1.17, table des paramètres du ColdFire, appels du DSP ; sondes courtes sous garde).

- **Delay et reverb du DT2 :** leurs réglages sont ceux du delay et de la reverb du Syntakt (TIME, X, WID, FDBK, HPF,
  LPF, REV ; PRE, DEC, FREQ, GAIN, HPF, LPF), plus un VOL par effet. Leur code est d'abord comparé à celui du Syntakt
  dans l'oracle (« Oracle des effets d'envoi du Digitakt II »). Identiques : pas de nouveau type, le type Syntakt
  gagne VOL et un son DT2 importé le prend. Différents : chacun devient un type de son emplacement, après les types
  actuels.
- **Chorus du DT2 :** cinquième type de l'emplacement Chorus, après Bucket Brigade. Réglages DPTH, SPD, HPF, WDTH, VOL,
  sans synchro au tempo comme sur le DT2 ; ses envois DEL et REV vont aux emplacements Delay et Reverb quel que soit
  leur type, à 0 par défaut. Les autres types ne changent pas.
- **Jugement :** oracle d'abord (retours tracés, 19 mots de réglages mappés aux 23 réglages, pilote d'envois
  `0x1c18a6` puis master dans le même bloc), tests Q contre des vecteurs courts (l'oracle rend environ une seconde
  d'audio par heure), l'oreille pour le reste.
- **Budget :** les envois mesuraient déjà 0,96 % au pire en v1, au-dessus du guide de 0,7 % que fixait « Répartition
  du budget CPU entre les jalons ». Corrigé : envois ≤ 1,2 % (chorus du DT2 compris), chaîne ramenée à ≤ 2,0 %, son
  guide de la v1, pour garder 15 % au total.
- **Tranché le 2026-11-16** par « Oracle des effets d'envoi du Digitakt II », selon la règle ci-dessus : le delay du
  DT2 est celui du Syntakt (écart −97 à −128 dB, deux constantes près) : pas de nouveau type, le type Syntakt gagne
  VOL, avec le retour × 0,5 et la loi de FDBK du DT2 pour un son DT2 importé. La reverb du DT2 est un autre
  algorithme : elle devient le type « DT2 reverb » de l'emplacement Reverb, après les types actuels, jugée contre les
  vecteurs de l'oracle (coût de l'original ~14 560 instructions SHARC par bloc).
