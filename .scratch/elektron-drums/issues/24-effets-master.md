# Effets master

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 23

## Question

Quels effets au master, et quand ? L'utilisateur veut ceux du Syntakt ou du DT2. Le Syntakt n'a pas de master
numérique : son overdrive et son filtre sont analogiques, après les convertisseurs, sans compresseur (modèle approché
seulement, piloté exactement par le CPU #1). Le DT2 a un compresseur (`0xb82d41`) et un overdrive numériques. Porter
compresseur et overdrive du DT2 en avance, dans la v1, comme premier morceau de son port, ou attendre son jalon ?
Garder l'overdrive analogique du Syntakt en modèle « ≈ » comme second choix ? Décider une fois l'inventaire du DT2
fait.

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q5), sur l'inventaire « Cartographie du firmware du Digitakt
II » : master du DT2 en `0x1c207b` = compresseur `0xb82d41` (11 fonctions, ~16 500 instructions pondérées par bloc),
puis le saturateur `0x1cb3d8` (le même que l'overdrive de piste), puis écrêtage et rampe de gain ; pas d'EQ.

- v1 : compresseur et overdrive du DT2, portés en avance comme premier morceau du port du DT2 (d'abord le harnais
  d'oracle sharcvm/digikit, qui ne tourne pas encore : compilateur C et chaîne Rust à installer). Le saturateur sert
  au master et comme overdrive de piste DT2.
- Ordre du firmware gardé : compresseur, overdrive, écrêtage ; pas d'inversion.
- Réglages du compresseur tels que dans le firmware (dont sa source de sidechain s'il en a une), vumètre de réduction
  de gain ; rien d'ajouté.
- L'overdrive analogique du Syntakt, en modèle approché « ≈ », arrive avec le jalon des machines analogiques comme
  second choix d'overdrive au master.
- Chaîne de piste amendée (voir « Chaîne de piste ») : le BR du DT2 passe dans la voix DT2 (page SRC du sample sur la
  machine, toujours en premier) ; la BAND gagne un bouton avant/après le filtre (avant par défaut) et un quatrième
  type, le base-width du DT2 ; une piste DT2 tient ainsi dans les deux emplacements drive (overdrive, SRR) et reste
  exacte.
