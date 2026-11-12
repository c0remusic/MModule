# Machines analogiques : Rytm et pistes 9-12 du Syntakt (J4)

Type: AFK puis HITL (écoutes)
Status: ready-for-agent
Assignee: 
Blocked by: 16

## Quoi

Deux familles nouvelles dans `edDrums` : les machines analogiques du Rytm MKII (OS 1.74 : grosses caisses, caisses
claires, rimshots, clap, toms, charleys, cymbales, cowbell, SY DUAL VCO, SY CHIP, SY RAW, bruit, impulsion) et celles
des pistes 9-12 du Syntakt. Décidé par l'utilisateur le 2026-11-12 : modèle maison, réglé contre le firmware.

## Comment

- **Contrôle exact.** Ce que le firmware calcule pour une voix (paramètres vers tensions de commande, enveloppes,
  hauteurs exponentielles, vélocité, lissage, fréquences des oscillateurs métalliques) est porté en C++ et comparé aux
  routines du firmware exécutées en émulateur, par vecteurs, comme les machines numériques du Syntakt. Les vecteurs et
  le firmware restent hors dépôt (`C:\dev\elektron\elektron-work\`).
- **Timbre maison.** Le firmware ne contient pas les circuits : chaque voix est un modèle de circuit classique écrit
  ici (ticket 14 et sa recherche : Werner pour les métaux), sans aucune donnée du firmware. Réglage final à l'oreille
  de l'utilisateur, par écoutes de contrôle.
- **Point de départ.** Un brouillon non relu existe hors dépôt (`elektron-work\gemini\edDrumsRytm.*`,
  `edDrumsSyntaktAnalog.*`, 36 machines Rytm) : son test vérifie seulement que chaque machine sonne et quelques sens
  de variation, rien contre le firmware ni contre un enregistrement. Le relire, garder ce qui tient, retirer de ce qui
  entre au dépôt toute donnée, adresse, mot ou nom tiré du firmware.
- **Noms.** Noms maison selon le ticket 27 : aucune marque, aucun nom de machine, des noms qui décrivent le son.

## Critère de fin

- Contrôle : chaque routine portée égale le firmware sur ses vecteurs.
- Chaque machine sonne, se fige au silence, et tient le pire kit sous le budget CPU de la spécification à 44,1 et
  48 kHz (brouillon : 3,6 % d'un cœur pour 16 voix, filtre et overdrive compris).
- Hits exacts, latence et gigue inchangées.
- Écoutes de contrôle envoyées et notes de l'utilisateur appliquées.
- Gate vert.

## État

Pas commencé. À lancer après le ticket 16, avant le ticket 18.
