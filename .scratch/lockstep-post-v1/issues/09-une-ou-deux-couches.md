# Couche du Rytm et layering du DT2 : une couche par piste ou deux

Type: grilling
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 05, 08

## Question

Avec « Place du Digitakt II dans Lockstep » et « La couche sample du Rytm dans son firmware » : une piste a-t-elle une
seule couche sample dont le lecteur se choisit (DT2 ou Rytm), ou deux couches distinctes ? Où chacune entre dans la
chaîne (avant les drives comme le layering, ou ailleurs pour coller au Rytm), et avec quels réglages communs ?

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling).

- Une seule couche sample par piste, sur toute piste (machine Rytm comprise, aucune famille réservée), lue par le
  lecteur DT2 du Sampler en ONESHOT (« Place du Digitakt II dans Lockstep »). Le Rytm n'apporte pas de deuxième couche.
- Le Rytm entre par traduction de ses réglages à l'import d'un son : TUNE ±24 dans les ±5 octaves, STRT/END en
  STRT + LEN avec le sens (END < STRT : à l'envers), loi de LEV. Un son Rytm qui a un sample allume la couche ; ce
  qu'il n'a pas (enveloppe propre, décalage) prend sa valeur neutre. Si « Où le Rytm rend sa couche sample » montre que
  son lecteur sonne autrement (interpolation, loi de BR), il devient un choix de lecteur de cette même couche, jamais
  une deuxième couche.
- La couche garde tous les réglages du layering v1 (ticket « Layering » de la planification v1) : niveau propre,
  accord et fin, début, longueur, boucle, sens, enveloppe propre (attaque, maintien, déclin), décalage de 0 à 100 ms,
  suivi de note, éteinte par défaut et sans coût, retrig avec la rampe anti-clic ; tout verrouillable et modulable.
  Plus ceux du lecteur DT2 en ONESHOT, et un BR propre (sur le sample seul, avant la somme, comme le Rytm ; le BR de
  drive garde son rôle sur toute la voix).
- Place inchangée : sommée à la machine, avant les drives pré, puis toute la chaîne (spec v1, section 4).
- Terme : « couche sample » nomme le son ajouté à la voix d'une piste ; « layering » l'action, « box SAMPLE »
  l'élément d'interface ; le Sampler reste la machine qui ne joue que des samples (`../GLOSSARY.md`).
