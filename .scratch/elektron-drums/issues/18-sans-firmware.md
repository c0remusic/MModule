# Instrument sans firmware chez l'utilisateur

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Qu'est-ce que chaque moteur lit encore dans l'image du firmware au lancement, et que faire de chaque donnée pour que
l'instrument tourne sans aucun firmware chez l'utilisateur ? Partir de ST Drums (sections 3 et 7 du .syx lues par
`stDrumsFirmware`, état de départ du SRAM, tables lues par le port du Syntakt) et du Machinedrum (flash image :
code, tables, banque ROM UW), puis ce qu'on sait du Digitakt II, du Digitone II, du Model:Cycles et des effets du
Virus. Pour chaque donnée : calculable par formule (générée par notre code, un test la compare au firmware),
remplaçable (samples de l'utilisateur, réglages maison), ou indispensable (extraite du firmware de l'utilisateur s'il
en a un, absente sinon). Taille, effort, et ce que cela change à la distribution (rien d'Elektron livré).

## Answer

Le port du Syntakt ne lit plus aucun code du firmware, seulement des données. Mesuré par XOR de chaque page puis de
chaque bloc de 256 octets sur un scénario des 12 machines : la section 7 compte pour ~183 Ko (~155 tables de
constantes, l'image SRAM du CPU voix), la section 3 pour ~26 Ko (~20 tables, 505 descripteurs, 143 mots d'état SRAM).
Cinq tables sont régénérées au mot près par une formule (exp2, log2, sinus RAM, exp2 RAM, vélocité linéaire), et la
plupart des autres ont une forme régulière (ondes = sommes de ≤ 16 harmoniques). Elles restent à régénérer famille
par famille, chacune testée contre le firmware : 4 à 8 sessions [INFER]. Sont remplaçables les descripteurs et les
défauts, l'état SRAM, la table machine → moteur, les samples (SP TWINSHOT, UW du MD, DT2) et les kits d'usine. Pour le
Machinedrum, les tables DSP1/DSP2/OS sont à inventorier avec le ticket 17 ; un précédent existe déjà, le sinus du DSP
mixeur recalculé par la récurrence du boot. Une table restée sans formule bloque son moteur, ou le rend approximatif
et signalé comme tel. Le plug-in livré n'a alors plus de dossier `roms`, et les firmwares ne servent qu'aux tests.
Détails : [research/18-sans-firmware.md](../research/18-sans-firmware.md).
