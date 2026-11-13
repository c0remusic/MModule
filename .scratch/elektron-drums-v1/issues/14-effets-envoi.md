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

## État (2026-11-13)

Deux types du chorus Virus sont portés, Vibrato et Air (`Vibrato`, `Air` dans `edDrumsSendFx.*`), mesurés sur des rendus
d'OsTIrus faits en local (`vfx.exe` sous garde d'horloge, rendus dans `elektron-work/oracle/virus-fx/renders`, scripts et
résultats dans `elektron-work/oracle/virus-fx/t14`, hors du dépôt). Les deux lisent leur ligne par un allpass d'ordre un
entre deux échantillons, de coefficient 1 − t et non (1 − t)/(1 + t) : la forme exacte restait 14 dB sous le signal sur
bruit, celle-ci 25 à 38 dB. Le Classic, lui, lit en ligne droite, comme avant.

Vibrato : sec absent, pas de feedback, Shape et Feedback sans effet (vérifié), les deux canaux au même retard, un sinus
de 0 au sommet. LFO à 2^(rate/32) Hz (1,9997 Hz mesuré à 32, 15,64 à 127). Le sommet est un écart de hauteur : sommet × Hz
constant (633,6 à depth 127), relevé à 4 Hz tous les 16 de depth (2,72 à 16, 158,44 à 127), borné à 512. Sous rate 9 le
sommet touche 512 : non vérifié là.

Air : sec absent, Rate, Shape, Feedback et Amount sans effet (vérifié), une ligne par canal, le droit à sommet − gauche.
Le balayage est moitié triangle, moitié cosinus surélevé (écart 0,1 % du sommet). Depth seul le règle : le sommet croît
en ligne droite jusqu'à 32 (103,84), puis de 4/3 tous les 8, jusqu'à 512 à 77 ; au-delà la vitesse monte en huitièmes de
0,09989 Hz (8 jusqu'à 79, 49 à 127), relevés un par un : entre deux huitièmes la comparaison perdait 10 dB. Air n'a donc
ni Rate ni Sync, comme sur le Virus.

Comparaison sur bruit stéréo, retour seul (mix 127), après 5 s, avance de notre LFO cherchée par fenêtre comme pour le
Classic (`cmp_vib.py`, `cmp_air.py`) : Vibrato, huit réglages, 17,3 dB sous le signal au pire (depth 24, rate 40 ; 20,7 à
38 dB ailleurs) ; Air, neuf depths de 16 à 127, 22,6 dB au pire. Au-dessus des 16 dB du Classic. Dans le dépôt,
`edDrumsEngineTest` (`chorusTypes`) vérifie sur notre sortie les sommets et vitesses mesurés sur le Virus (à 2 échantillons
et 2 % près) et la relation des canaux, et `sends` joue les trois types (retour présent, mêmes échantillons quels que
soient les blocs de l'hôte, Sync qui change Vibrato et pas Air). Coût : Vibrato 0,019 %, Air 0,033 % d'un cœur (Classic
0,024 %).

Dans le moteur, le type est l'octet `ChorusType` de `Sends::setChorus` (0 Classic, 1 Vibrato, 2 Air), ajouté après ceux
que compte `ChorusBytes`, comme le S&H du DP/4 ; un changement passe comme un changement de source (fondu, ligne vidée).
Côté hôte, `chorus_type` (« Chorus Type », Classic, Vibrato, Slow Sweep ; un type nouveau s'ajoute en fin de liste) est
ajouté en fin d'`edDrumsParameterIds.txt`, enregistré après les relais, transmis par `fx.setChorus(ChorusType, …)`, et a
sa cellule TYPE en tête du bloc CHORUS de la page FX. `edDrumsProcessorTest` le couvre (liste figée : les relais derniers
sauf ce qui vient après eux ; le paramètre joue sur le moteur comme les autres), `edDrumsEditorTest` dessine la page pour
chaque type, `edDrumsVst3HostTest` vérifie sa place après les relais, qu'il s'automatise, et qu'un état le garde.

Hyper, essayé et laissé hors de la liste. Mesuré : sous un crossover du premier ordre vers 307 Hz l'entrée passe telle
quelle ; au-dessus, une part sèche et une à trois voix, chacune une ligne lue sur le balayage d'Air entre 0,059 et 1 fois
son sommet, le canal droit un quart de tour plus loin. Le sommet vaut 2,1243 fois celui d'Air, jusqu'à 509,9 ; passé 55
la vitesse monte par huitièmes, ceux d'Air 21 plus haut (relevés à chaque depth de 40 à 127). Amount ralentit la
première voix (0,697 fois à 127), fait entrer la deuxième puis la troisième (vers 1,4 et 1,5 fois), et baisse le sec de
0,711 à 0,505. Porté sur ces lois, il passe à depth 32 pour Amount 0, 32, 64, 96 et 127 : 17,5 dB sous le signal au pire
(les phases des voix prises sur celles du Virus à chaque fenêtre, comme l'avance du LFO pour les autres types). Mais aux
Amount intermédiaires (16, 48, 80, 112) les voix du Virus disparaissent par périodes (un tiers du temps à 48), ce que ni
le modèle ni notre port ne font : 0 à 4 dB seulement à 48, et 12,6 dB à depth 80, Amount 48. Sous le seuil, donc hors de
la liste. Le port, ses lois et les scripts sont gardés hors du dépôt (`t14/hyper-attempt`, `hyp*.py`, `cmp_hyp.py`,
`hyp_check.py`) ; il reste à relever ce que font les voix aux Amount intermédiaires.

Rotary, mesuré, non porté. Sur impulsions (`rot1.py`) : à chaque instant une dizaine de prises faibles (0,06 à 0,36) de
−11 à 120 échantillons, qui bougent avec le temps, des deux signes ; canaux identiques à Amount 64, différents à 0 (Amount
règle la largeur) ; Rate change leur mouvement, Depth peu de chose. La méthode des types précédents suit une à trois prises
nettes ; elle ne sépare pas ces chemins, et un modèle qui atteigne 16 dB sur bruit demanderait leur structure entière
(rotor, tambour, réflexions). Sous le seuil par cette méthode : hors de la liste.

Vintage, non repris dans cette session : sec à 0,707, une voix vers 134 échantillons filtrée (−5 dB à 5 kHz, −35 dB à
16 kHz), une seconde plus faible, une distorsion qui dépend du niveau (H3 à −44 dB pour 0,3 en entrée), LFO à
0,25 × 2^(rate/24) Hz, X-Over ; Feedback, Shape et Amount sans effet. Non porté, hors de la liste.
