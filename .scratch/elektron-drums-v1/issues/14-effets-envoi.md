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

## Réglages ajoutés (2026-11-08)

À la demande de l'utilisateur, ce qui était mesuré sans être réglable l'est devenu. Le chorus a la forme de LFO du
Virus (sinus, triangle, scie descendante, carré, S&H, S&G, une valeur tirée par tour), triangle par défaut ; un saut de
carré, de scie ou de S&H s'étale sur huit blocs, 5,8 ms, pour ne pas cliquer, là où le Virus saute. Le phaser du DP/4 a
sa phase de LFO (ensemble ou opposés, opposés par défaut) et sa ligne de delay : la sortie de la chaîne gauche, une prise
gauche passe-bas réinjectée dans la chaîne gauche, une prise droite passe-bas ajoutée au retour droit, d'un échantillon
plus longue que la mesure ne le disait, 0 à 1,6 s par pas de 1,6 ms ramenés à 128 valeurs. Contre Phaser-DDL avec delay :
−25 à −28 dB.

Toujours pas faits : S&H et soft clip du DP/4, sync tempo des effets, types Vintage, Hyper, Air, Vibrato et Rotary du
chorus Virus.

## État (2026-11-11)

Sync au tempo. Les deux delays prennent leur temps en valeurs de note du tempo de l'hôte quand Sync est allumé
(`delay_sync`, allumé par défaut, et `delay_note`, DURATION, de 1/32 à 1/1 avec triolets et pointées, 1/4 par défaut) :
le temps est calculé en double depuis le BPM de l'hôte, triolets exacts, au lieu de passer par les 128es de TIME. Sync
coupé, c'est HZ (`delay_free`), et à 0 Hz le TIME de la machine joue encore, pour les hôtes qui l'automatisent ; TIME
n'a plus de cellule visible. Mesuré par `sendTimes` : noire à 120 BPM, 22 049 échantillons pour 22 050 ; croche de
triolet à 100 BPM, 8 819 pour 8 820 ; à 137,5 BPM, 6 413 pour 6 415 ; croche pointée de l'Echo à 90 BPM, 22 051 pour
22 050. Le chorus et le phaser (les trois modes) ont leur Sync et leur SPEED en valeurs de note, de 1/64 à 16 mesures,
un tour de LFO par valeur (`chorus_sync`, `chorus_speed`, `phaser_sync`, `phaser_speed`, coupés par défaut) ; la phase
du LFO reste la sienne, elle ne se cale pas sur la position du transport. Un ancien kit sans ces paramètres prend Sync
allumé à la noire : son delay change de temps s'il avait un autre TIME. Les ms au tempo courant à côté de DURATION ne
sont pas affichées.

S&H du DP/4. Mesuré sur les rendus de Phaser-DDL (Host, 44,1 kHz) : la cadence vaut trois fois la loi du LFO au même
nombre (0,74, 1,49, 2,75, 4,80, 8,4 et 14,1 Hz de 30 à 80, à 2 % près, la gigue des pas mesurés), le sinus est pris au
départ puis une demi-période plus tard, puis à chaque période, et le coefficient est lissé ensuite comme sans S&H.
Porté (`phaser_dp4_sh`, 0 coupé). Contre le plug-in, sur le coefficient suivi dans le temps : mêmes instants de pas à
1 ms près à 80 et 100, valeurs tenues à 0,05 près sur la première seconde ; plus loin, l'écart de 0,5 % de vitesse du
LFO déplace les valeurs prises. Le soft clip n'est pas porté : coupé, Phaser-DDL écrête à ±1 exactement (harmoniques
identiques à un écrêtage franc de 0,1 à 4 d'amplitude) ; allumé, +0,37 dB de gain, une courbe douce sous 1 (H3 à
−86 dB à 0,5, −47 dB à 1) et un plafond près de 0,99 en sortie, mais ces rendus ne disent pas sans ambiguïté où se
placent les deux écrêtages dans la chaîne (avant ou après le remélange de l'entrée). Notre phaser du DP/4 n'écrête pas,
comme avant.

Types Vintage, Hyper, Air, Vibrato et Rotary du chorus Virus : pas faits. Les rendus d'OsTIrus se font en local
(`virus-fx`, `vfx.exe`) et une partie des lois est déjà relevée (c8, c9 : prises, profondeurs, vitesses, X-Over de
Vintage) ; Rotary n'a jamais été mesuré. Le port et sa vérification restent à faire.

Mètres de retour : `Sends::peak` (crête des deux canaux, −20 dB/s, atomique écrit par le fil audio), relu par le
processeur et dessiné sur FX et Master, valeur en dB entiers dessous, −inf sous −60 dB.

Coût des envois, mesuré par `effectsCost` (une piste frappée quatre fois par seconde, blocs de 128, meilleur de trois,
machine chargée par les autres sessions) : les quatre envois en plus d'une piste, Echo et Gate Box 0,80 %, Syntakt
0,66 à 0,69 %, Syntakt avec le phaser du DP/4 0,65 à 0,83 % d'un cœur, sous le 1 % visé.

Reste ouvert, et non revendiqué ici : la validation à l'écoute du chorus et du phaser/flanger par l'utilisateur, critère
de fin du ticket. Le ticket reste donc ouvert.
