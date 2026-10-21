# Lire l'OS X.20 que l'utilisateur donne

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by:

## Question

Un lecteur C++ (sans JUCE, à côté de `md::fw::loadRomBankFromFlash`) qui prend le fichier `.syx` de l'OS X.20 et rend
ses quatre charges décodées (research 01 §1-3 de la carte des Machines utilisateur : le flux du premier étage, `S2MD`,
LZMA1 avec le décodeur du LZMA SDK, domaine public, `DSU`), avec leurs CRC vérifiés ; il trouve ce dont les tickets
suivants ont besoin par forme, pas par les adresses d'X.20 B (le code DSP des 6 par sa différence avec 1.63, les
enregistrements de Machines par leurs noms). Il refuse un fichier qui n'est pas un OS X.20 en disant pourquoi.

Tests : un fichier vide, tronqué ou d'un autre OS refusé ; avec `MD_X20_OS`, les CRC, la taille de chaque charge et
le code des 6 trouvé (sauté sans, code 77). Rien de l'OS dans le dépôt.

## Answer

Construit le 2026-10-21 dans `mdEngine/tools/mdfw` : `md::fw::parseX20Os` (`X20Os.*`) et un décodeur LZMA1 brut
(`Lzma.*`, écrit d'après la spécification, aucun octet d'X.20). Le flux du premier étage dépacké par `aplibDepack`, l'en-
tête `S2MD` trouvé par sa signature et vérifié par son CRC, les quatre charges vérifiées par leur CRC : les programmes
des voix et de mixage (`DSU`, LZMA lc 0 lp 0 pb 0, enregistrements en plans d'octets ; leur CRC « décompressé » ne
couvre rien de connu, le CRC propre du DSU sur ses mots le remplace), l'OS 68k (LZMA lc 0 lp 0 pb 1, 780 604 octets),
le noyau DSP (une section façon 1.63). Un fichier sans messages d'OS, coupé ou sans `S2MD` est refusé avec la raison.

`mdX20OsTest` (au garde-fou) : le décodeur contre quatre flux que le module `lzma` de Python a faits de données du
test ; les refus ; avec `MD_X20_OS` (sur ce PC, à côté des ROM, jamais dans le dépôt), les 102, 72 et 10 enregistrements,
l'OS de 780 604 octets, les entrées INIT, TRIG et RENDER des 6 dans les tables de 1.63 qui pointent sur leur code, ses
2 255 mots présents, et le même fichier coupé au quart refusé (coupé en deux, il garde la section 0 entière et se lit). Ce que la tranche suivante en tire « par forme » (le code
des 6 d'une autre version d'X.20) se décide avec elle.
