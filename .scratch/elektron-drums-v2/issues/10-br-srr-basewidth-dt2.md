# BR et SRR du DT2 en drives, son base-width en BAND

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 01

## Quoi

BR et SRR du DT2 deviennent des types de drive, son base-width un type de BAND, tous après les types actuels (le
BaseWidth existant est celui du Syntakt). Le type BR garde la loi du DT2. Relevés à l'oracle s'il manque des
vecteurs.

## Critère de fin

- Chaque type jugé contre ses vecteurs par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.2 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
