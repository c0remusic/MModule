# Rytm : les trente machines partagées avec le Syntakt

Type: AFK
Status: ready-for-agent
Assignee: agent (session du 2026-11-16)
Blocked by: 28

## Quoi

Le ticket 28 n'a pris du Rytm que ses trois machines propres (CP CLASSIC, BT CLASSIC, XT CLASSIC). Les trente autres
machines visibles de l'OS 1.74 existent aussi sur le Syntakt, sous le même nom, avec les mêmes constantes et la même
structure de routine ; Lockstep les joue par le port du Syntakt. Leurs écarts entre les deux OS n'ont jamais été
mesurés par vecteurs : un cache de hauteur, des bornes, huit voix au lieu de quatre (ticket 28, étape 2).

Mesurer ces écarts : enregistrer les routines de contrôle du Rytm pour les trente machines (le banc d'oracle3 avec la
section 3 du Rytm, comme `record_rytm.py` pour les trois du ticket 28) et les comparer, cas par cas, au port du
Syntakt (`edDrumsAnalogControl`). Puis, selon ce qui diffère :

- rien d'audible (même sortie dans 0-127) : le noter, rien à changer ;
- un écart qui change le son : le dire avec ses chiffres, et proposer soit une variante Rytm de la machine, soit de
  garder celle du Syntakt. Ce choix revient à l'utilisateur.

## Critère de fin

- Les trente machines comparées sur leurs vecteurs, le nombre de cas égaux et différents de chacune noté, chaque
  écart expliqué (quelle instruction, quelle borne).
- Pour chaque écart audible, la décision de l'utilisateur appliquée et testée.
- Vecteurs et sections hors du dépôt ; rien du firmware n'y entre, hors constantes mesurées.

## Références

Ticket 28 (étapes 1 et 2). `C:\dev\elektron\elektron-work\tools\analog\record_rytm.py`, `rytmrig.py`,
`vectors-rytm\rytm.rav`. Port : `source/elektron/edDrums/edDrumsAnalog*`.

## État (2026-11-16)

Mesure faite : [research/29-rytm-machines-partagees.md](../research/29-rytm-machines-partagees.md). 32 524 cas
enregistrés au banc d'oracle3 avec la section 3 du Rytm (vecteurs `elektron-work\vectors-rytm\shared-*.rsv`), rejoués
dans le port du Syntakt par un harnais hors dépôt.

- Aucune des 28 machines sonores n'est égale au port : chacune écrit son niveau par une autre loi. Le reste mis à part,
  8 sont égales, 9 diffèrent de 13 à 55 cas, 11 de beaucoup plus. Les deux utilitaires sont égaux. Toutes les tables
  que le port ajuste ou remplace sont dans la section 3 du Rytm, mot pour mot ; le cache de hauteur ne change rien.
- Trois écarts changent le son :
  1. **Loi de niveau**, les 28 : le Syntakt lit une table de 18 dB, le Rytm écrit c·w0². Par rapport à k = 127 : Rytm
     muet à 0, 10,4 dB plus bas à 32, 3,0 dB à 64, 0,5 dB à 96 [INFER : si le VCA suit le mot linéairement].
  2. **Clic et bruit suivent LEV** sur le Rytm (le Syntakt les tient fixes) : kicks HARD, CLASSIC, FM −18 dB à 32,
     −6 dB à 64, +6 dB à 127 ; les autres entre ces lois ; les corps échantillonnés de BD et SD ACOUSTIC aussi.
  3. **Hauteur du modulateur FM**, BD FM et SD FM : (v − 64)/2 demi-tons d'écart, −32 à v = 0, +31,5 à v = 127.
- Comptés, cause pas établie : les trois synthés (les plus gros écarts), SD CLASSIC, SD NATURAL, RS CLASSIC, BD SHARP,
  BD SILKY, SD HARD, le bloc de clic de trois métaux. Les lire demande chaque routine du Rytm contre sa jumelle du
  Syntakt, voix par voix.
- **Décision de l'utilisateur en attente** : pour chacun des trois écarts audibles, garder la version du Syntakt ou
  ajouter une variante Rytm.
