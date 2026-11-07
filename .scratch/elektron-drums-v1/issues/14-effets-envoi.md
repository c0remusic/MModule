# Effets d'envoi partagés

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 10, 11

## Quoi

Un exemplaire de chaque effet, retour stéréo, en parallèle : delay et reverb avec sélecteur de source (Syntakt,
Echo et Gate Box du MD), envoi delay vers reverb du Syntakt gardé ; chorus et phaser/flanger (deux modes) maison en
flottant, marqués « + ». Chaque retour sur Main ou une sortie choisie. Une piste séparée alimente quand même les
envois. Le réglage à l'oreille du chorus et du phaser/flanger contre le Virus TI se fait avec l'utilisateur, en fin
de ticket.

## Critère de fin

- Delay et reverb du Syntakt contre le port exact (Q1 ou Q2).
- Envoi à 0 : null (Q5).
- Coût des envois mesuré ; l'utilisateur a validé chorus et phaser/flanger à l'écoute.

## Références

Spécification §5. Ticket « Sources des effets d'envoi ».

## Noté par le ticket 10 (2026-11-05)

- `Echo` et `GateBox` sont prêts : pointeurs stéréo bruts, `GateBox` prend le retour de n'importe quel delay par `_echoL`/`_echoR`,
  y compris celui du Syntakt. DVOL existe sur la machine (Echo vers Gate Box) : le garder ou le laisser à 0 pour « en parallèle »
  se décide ici. L'ordre, delay avant reverb dans un bloc, reste implicite : à dire.
- FB au-delà de 64 auto-oscille, sur la machine et dans le port.

## Noté par le ticket 12 (2026-11-06)

`Engine::mix` tient déjà le signal de chaque piste après niveau, avant pan : les envois s'y branchent, `Chain` ne change pas. Reste à
décider comment les effets du Syntakt, à 48 kHz, reçoivent cette entrée.

## État au 2026-11-07

Le chorus, le phaser et le flanger sont écrits et leurs lois viennent de mesures sur des rendus d'OsTIrus, pas de son
code. Le chorus reprend le Classic : un retard par canal, 8 échantillons par pas de Delay, un triangle qui ne fait que
raccourcir, le canal droit à l'opposé, lecture linéaire, rien d'autre dans le chemin. Le phaser reprend le Virus : un à
six allpass du second ordre identiques, le canal droit 1,11 fois plus haut, le triangle balaie vers le haut seulement.
Le flanger n'existe pas au Virus : c'est le Classic à Delay court, ici de 1 à 33 échantillons sous l'envoi Phaser.

Contre les rendus : le phaser à l'arrêt tombe entre -22 et -48 dB d'erreur sur la transmittance, le chorus sur bruit
16 à 19 dB sous le signal. Il reste 1,6 % d'écart sur la vitesse entre deux points de la table de vitesses, non audible
pensons-nous. Types Vintage, Hyper, Air, Vibrato et Rotary du Virus non portés.

Écarts voulus avec le Virus : coefficients tous les 32 échantillons et non 64, un seul triangle pour le chorus (pas de
choix de forme), réglages lissés sur quelques blocs et Stages en fondu, pour ne pas cliquer. Le phaser tourne sur deux
intégrateurs : une forme directe partait à 1e28 avec une fréquence basse balayée à 28 Hz et le feedback au maximum.

Coût à 44,1 kHz : phaser à six étages 0,14 à 0,17 % d'un cœur, chorus 0,024 %, soit au pire 0,96 % avec delay et reverb
du Syntakt, sous le 1 % visé. Les deux tournent dès qu'un envoi est réveillé, même sans rien recevoir : 0,2 % à gagner
plus tard si le besoin vient.

Reste ouvert : la validation à l'écoute de l'utilisateur, critère de fin du ticket. DVOL reste à 0 pour des retours en
parallèle. FDBK au-dessus de 64 sur le delay du Syntakt reste hors Q2.

## Phaser du DP/4 (2026-11-07)

À la demande de l'utilisateur, l'envoi Phaser a un troisième mode, Mode 2 : le phaser de l'Ensoniq DP/4, mesuré sur ce
que rend Phaser-DDL de Dusty Devices, une émulation de ce phaser, à la cadence de l'hôte. Douze allpass du premier ordre
par canal, un seul coefficient que bouge un sinus, gauche et droite de part et d'autre, chaque chaîne réinjectée dans
l'autre, puis l'entrée remélangée pour creuser les encoches. Frequency donne le centre, Depth la largeur, Spread la
profondeur d'encoche, signée autour de 64 ; Stages ne sert pas. Sans le delay du DDL ni son mode à 35,7 kHz.

Contre le plug-in sur bruit stéréo : −37 à −52 dB d'erreur à l'arrêt, −17 à −29 dB avec le sinus sur 0,3 s, l'écart
venant surtout de 0,5 % sur la vitesse. Coût : 0,11 % d'un cœur. Le flanger du DEEP/4 de Temecula DSP n'est pas fait :
l'hôte de test ne charge pas ce plug-in, et l'utilisateur a choisi le phaser seul.
