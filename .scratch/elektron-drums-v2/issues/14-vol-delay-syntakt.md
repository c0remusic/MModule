# VOL du delay Syntakt

Type: AFK
Status: done
Assignee:
Blocked by: 01

## Quoi

Le type Syntakt de l'emplacement Delay gagne VOL, le niveau de retour du DT2, dont l'oracle des envois a montré
que le delay est celui du Syntakt. Le retour × 0,5 et la loi de FDBK du DT2, réservés à un son DT2 importé, ne sont pas
construits : aucun import de sons DT2 n'est dans la v2 (décidé avec l'utilisateur le 2026-11-17).

## Critère de fin

- VOL à son défaut : aucun échantillon changé sur un set existant.
- VOL mesuré contre les vecteurs de `vectors-dt2\sends`.
- Gate vert.

## Références

Spécification v2, §5.4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.

## Fait (2026-11-23)

- **Loi relevée à l'oracle** : trois cas pilote ajoutés à `vectors-dt2\sends` (`drv_imp_del_vol64`, `_vol127`,
  `drv_imp_rev_vol64`, README mis à jour). Le retour du delay vaut sa sortie brute × 2 (VOL/128)², celui de la reverb
  sa sortie brute × (VOL/128)², exacts (résidu ≤ 4e-10) à 64, 110 et 127. La sortie brute du delay du DT2 étant la
  moitié de celle du port Syntakt, son retour vaut celui du port × (VOL/128)². VOL agit après l'envoi DEL→REV : la part
  donnée à la reverb ne bouge pas (`drv_imp_all`, 0,25 à DEL REV 64, quel que soit VOL).
- **Loi de Lockstep, choisie par l'utilisateur** : (VOL/127)², défaut 127, donc gain 1 et aucun set changé. Écart au DT2
  de (128/127)², 0,136 dB, partout (annoncé d'abord à tort comme 0,07 dB lors du choix).
- `delay_vol` (« Delay Volume »), ajouté après les paramètres de la couche, dans l'ordre gelé ; un état ou un Kit qui
  n'en a pas prend 127. Montré VOL dans le bloc DELAY quand la source est le Syntakt. Rampe d'un bloc à chaque
  changement ; rien n'est calculé à 1.
- Tests : `edDrumsEngineTest` (delayVolume : 127 identique à avant, gain (VOL/127)² à 64, 110 et 127, 0,136 dB au DT2
  mesuré, retour de la reverb inchangé) ; `edDrumsProcessorTest` (le paramètre jusqu'au moteur, son rang).
