# Un seul contrôle de hauteur : PITCH

Type: HITL
Status: done (VINTAGE et essai dans Live à venir)
Blocked by: aucun

## Quoi

Décidé par l'utilisateur le 2026-11-25, pendant la maquette du ticket 33 : TUNE (Machine 1) et NOTE ne font qu'un
contrôle, PITCH, en notes, pour accorder une piste à la note voulue.

Aujourd'hui les deux sont distincts. TUNE est le réglage propre à la machine (0-127, 64 au centre), que chaque machine
traduit à sa manière. NOTE est la note que joue le Hit (`Engine::setNote`) : les machines Syntakt la jouent, les machines
Machinedrum non ; le suivi de clavier du filtre et SAMPLE LAYER la lisent aussi.

## Décisions à prendre

- Ce que vaut PITCH sur chaque famille, en particulier sur celles dont TUNE n'est pas en demi-tons.
- Les identifiants d'hôte figés (`tNN_m1`, `tNN_note`) : ce qu'ils deviennent, et ce que lisent les sets et les
  automations enregistrés avant.
- Les Locks par pas sur NOTE et sur TUNE.

## Relevé (2026-11-25, dans le code)

- Ajoutent NOTE à TUNE : Syntakt (demi-tons, TUNE 64 joue la note, sauf BD MODERN 88 et CY ALLOY 40), FM DRUMS (PTCH
  autour de 64), les deux familles analogiques (mot de hauteur du firmware), le Sampler en CLEAN (TUNE − 64 + note − 60).
- Ignorent NOTE, TUNE en demi-tons : Sampler VINTAGE ((PTCH − 64) × 3/8, ±24), SOPHIE ((v − 64)/2, ±24), OPEN DRUM
  (TUNE est la note du firmware, 69 = A4).
- Ignorent NOTE, TUNE sans échelle de demi-tons : Machinedrum (TRX, EFM, E12, P-I, ROM, GND, CRUNCH 12, ondes maison)
  et le Sampler en 12-BIT : la table de hauteur du firmware, chaque machine à sa façon.
- NOTE est lu aussi par le suivi de clavier du filtre et par SAMPLE LAYER, quelle que soit la famille.
- Locks : la note du pas s'écrit en Lock de NOTE, qui tient jusqu'au Hit suivant ; TUNE se locke comme Machine 1.

## Décidé (2026-11-25, recommandations acceptées par l'utilisateur)

- PITCH est une note (C4, D#3…) et écrit toujours NOTE : la note des pas, le suivi de clavier du filtre et SAMPLE
  LAYER la suivent sur toutes les machines.
- Machines qui ajoutent déjà la note (Syntakt, FM DRUMS, analogiques, Sampler CLEAN) : PITCH écrit NOTE, TUNE reste
  caché à la valeur qui joue la note telle quelle (64 ; BD MODERN 88, CY ALLOY 40).
- VINTAGE, SOPHIE, OPEN DRUM : le moteur leur fait lire la note, qui décale leur hauteur comme TUNE ; TUNE reste au
  centre, PITCH les accorde dans leur plage (±24 demi-tons).
- Machinedrum et Sampler 12-BIT : TUNE reste visible à côté de PITCH sur ces machines ; PITCH y règle le filtre et la
  couche. Mesurer leur hauteur pour une table note → TUNE : ticket à part, s'il en vaut la peine.
- Identifiants d'hôte inchangés (`tNN_m1`, `tNN_note`) : PITCH est un contrôle de l'éditeur sur ces deux paramètres ;
  sets, automations et Locks enregistrés jouent comme avant.

- Sets où TUNE n'est pas à sa référence : PITCH affiche la hauteur jouée (la note plus l'écart de TUNE : C4 et TUNE 70
  sur SD BASIC, F#4) ; le toucher écrit NOTE et remet TUNE à sa référence, sans saut du son.
- Plage : les notes 0 à 127 de NOTE (C-1 à G9), un demi-ton par pas, sans cents ; sur les machines limitées à ±24
  demi-tons, la valeur s'arrête au bord et l'affichage le montre.
- Ordre : après le ticket 33 (la page Sound de la maquette v9), où la case PITCH montre NOTE en attendant.

## Écart de mise en œuvre (2026-11-25)

- Les deux familles analogiques gardent TUNE visible : elles ajoutent la note, mais leur TUNE passe par le mot de hauteur
  du firmware, dont l'échelle en demi-tons n'est pas vérifiée. PITCH y règle NOTE.

## Critère de fin

- [x] `edDrumsHitPreviewTest` : OPEN DRUM et SOPHIE jouent NOTE, une octave plus haut la période est divisée par deux
  (mesuré : 2,000 et 2,032).
- [x] `edDrumsEditorTest` : BD MODERN, TUNE 76, NOTE C4 : TUNE caché, PITCH C3 ; un clic sur PITCH met TUNE à 88 et NOTE à
  C3 ; ROUND KICK : TUNE affiché, PITCH est NOTE.
- [ ] VINTAGE : NOTE lue dans le moteur, pas testée (il faut un sample).
- [ ] Essai dans Live.
