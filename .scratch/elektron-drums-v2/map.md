# Lockstep après la v1 — construction de la v2

## Destination

Les cinq jalons de `../elektron-drums/spec-v2.md` livrés, chacun à son critère de fin (§1), le pire kit sous 15 %
d'un cœur, garde-fou vert sans test sauté.

## Notes

- Spécification : [spec-v2.md](../elektron-drums/spec-v2.md), qui s'ajoute à `spec-v1.md`. Une contradiction entre un
  ticket ici et la spécification se règle dans la spécification d'abord.
- Mêmes règles que la carte v1 (`../elektron-drums-v1/map.md`) : règles `karpathy-guidelines` ; coût mesuré sur
  machine calme ; latence et gigue avant et après pour tout ce qui touche au chemin des trigs ou au rendu ; avant le
  garde-fou de chaque ticket, `/code-review`, revue d'architecture (`mattpocock-skills:codebase-design`), puis
  `/simplify`, les constats écartés nommés dans le commit.
- Dans chaque jalon : oracle et vecteurs d'abord, puis le port, puis l'interface. Firmwares, oracles et vecteurs
  restent dans `C:\dev\elektron\elektron-work`.
- Décidé avec l'utilisateur le 2026-11-17 : aucun import de sons DT2 dans la v2 (le delay Syntakt ne gagne que VOL) ;
  le format d'import des sons Rytm (`.syx` proposé) se confirme au ticket 21.

## Tickets

Ordre de la spécification (§9). `Blocked by:` en tête de chaque ticket.

- [01 Le pire kit au garde-fou](issues/01-pire-kit-au-garde-fou.md)
- [02 ANALOG ≈ du drive Syntakt, par piste](issues/02-analog-drive-syntakt.md) — après 01
- [03 ANALOG ≈ au master](issues/03-analog-drive-master.md) — après 02
- [04 Lecteur DT2 du Sampler en ONESHOT](issues/04-lecteur-dt2-oneshot.md) — après 01
- [05 GRID et REPITCH du lecteur DT2](issues/05-grid-repitch.md) — après 04
- [06 Couche sample par piste](issues/06-couche-sample.md) — après 04, 26
- [07 Lock de sample sur le lecteur du Sampler](issues/07-lock-sample-lecteur.md) — après 04
- [08 Lock de sample sur la couche, glisser sur un pas, nom court dans les lanes](issues/08-lock-sample-couche-glisser.md) — après 06, 07
- [09 Les six filtres du DT2 en types de FILTER](issues/09-filtres-dt2.md) — après 01
- [10 BR et SRR du DT2 en drives, son base-width en BAND](issues/10-br-srr-basewidth-dt2.md) — après 01
- [11 WERP du lecteur DT2](issues/11-werp.md) — après 04
- [12 SLICE du lecteur DT2 et son éditeur](issues/12-slice-editeur.md) — après 04
- [13 STRETCH du lecteur DT2](issues/13-stretch.md) — après 04
- [14 VOL du delay Syntakt](issues/14-vol-delay-syntakt.md) — après 01
- [15 Chorus du DT2](issues/15-chorus-dt2.md) — après 01
- [16 Reverb du DT2](issues/16-reverb-dt2.md) — après 01
- [17 Drive Cycles dist](issues/17-cycles-dist.md) — après 01
- [18 Famille Cycles avec KICK seul](issues/18-famille-cycles-kick.md) — après 17
- [19 SNARE, METAL, PERC, TONE, CHORD](issues/19-cycles-cinq-machines.md) — après 18
- [20 VEL DEST étendu aux réglages de la machine sur une piste Cycles](issues/20-vel-dest-cycles.md) — après 18
- [21 Import des sons Rytm dans la couche sample](issues/21-import-sons-rytm.md) — après 06
- [22 Compagnon Push : liaison et pads de pas](issues/22-push-liaison-pas.md) — après 01
- [23 Compagnon Push : pads des pistes et relais des encodeurs](issues/23-push-pistes-encodeurs.md) — après 22
- [24 Compagnon Push : lock par pas tenu](issues/24-push-lock-pas-tenu.md) — après 23
- [25 Compagnon Push : mode LIVE et choix de pattern](issues/25-push-live-pattern.md) — après 24
- [26 La chaîne à 4x sous 4 % d'un cœur](issues/26-chaine-4x-sous-4.md) — après 01 ; ajouté le 2026-11-17, bloque 06
