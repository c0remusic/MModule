# Des noms de paramètres qui disent ce qu'ils font

Type: HITL
Status: needs-triage
Assignee: 
Blocked by: 

## Quoi

Demandé par l'utilisateur le 2026-11-11 : chaque paramètre de moteur doit porter un nom qui dit clairement ce qu'il fait au son. Les noms repris des machines d'origine (Machinedrum, Syntakt, DP/4, Virus, OPEN DRUM, Sophie, etc.) sont souvent des abréviations opaques (PTCH, DEC, HP, RAMP, SNAP…) ou des noms de marque.

Règle : tout paramètre importé d'une machine Elektron ou d'une autre machine doit évoquer cette machine sans citer son nom. Pas de « Machinedrum », « Syntakt », « DP/4 », « Virus », « Elektron » dans les noms montrés à l'utilisateur ; un nom de famille ou de type à nous qui rappelle la machine (son époque, sa façon de sonner, sa forme) à la place.

## Contraintes

- Les ID des paramètres sont gelés (`edDrumsParameterIds.txt`) : seuls les noms montrés changent, dans l'hôte et dans l'éditeur. Un set, un Kit ou un son de piste sauvés avant doivent se recharger à l'identique.
- Les noms de l'hôte restent lisibles sur l'écran de Push (« T01 » puis le nom, court).
- L'éditeur Lockstep montre des noms courts dans ses cellules : chaque paramètre a un nom court (cellule) et un nom long (hôte, infobulle).
- À trancher avec l'utilisateur : les noms des machines elles-mêmes (TRX-BD, EFM-SD, BD MODERN…) et des familles suivent-ils la même règle ?

## Fait quand

- Une table de proposition (famille, machine, paramètre : nom actuel, nom court, nom long, ce que fait le paramètre) est approuvée par l'utilisateur.
- Les noms approuvés sont appliqués à l'hôte et à l'éditeur ; aucun nom montré ne contient un nom de marque ou de machine (test qui parcourt les noms).
- Les états et Kits sauvés avant se rechargent à l'identique (test).

## Décisions (2026-11-11)

Réponse de l'utilisateur à la première proposition (`../noms-proposition.md`) :

- Les noms sont en anglais.
- Les noms sont des noms maison : aucune marque ni nom de machine, ni dans les paramètres, ni dans les machines, ni dans les familles.
- HPF et LPF sont gardés (pas LOW CUT et HIGH CUT).
- Dans l'hôte, les contrôles de machine restent « T01 Machine 1 » à « T01 Machine 24 » : un nom d'hôte ne peut pas suivre la machine choisie. Leurs noms par machine vivent dans l'éditeur (cellule, infobulle, lignes de locks de Seq).
- Les autres recommandations sont acceptées : 10 caractères visés et 12 au plus pour un nom court, 10 pour un nom de machine ; un mot par fonction partout ; pas d'unité dans les noms ; des noms longs dont les 10 premiers caractères après « T01 » se distinguent.
- Refusés : des noms de machine faits de préfixes codés (VA-BD, FM-SD, 12B-RC…) et des noms de famille abstraits (STEEL, HYBRID, SHARD…). Les noms de machine et de famille disent l'identité : ce que la machine sonne ou comment elle fait son son, pour qu'on la choisisse par son nom.
- Le cœur du ticket : chacun des contrôles de chaque machine (Machine 1-8, et 9-24 là où elle en a) porte un nom clair qui dit ce qu'il fait sur cette machine-là (sur une grosse caisse : TUNE, SWEEP, SWEEP TIME, DECAY, CLICK, NOISE, BODY, DRIVE ; pas PTCH, DEC, RAMP, HP).

La proposition révisée selon ces décisions est `../noms-proposition.md` ; elle attend l'approbation des noms.

Deuxième réponse de l'utilisateur, le même jour :

- Le plug-in s'appelle Lockstep (et non plus « Elektron Drums Dev ») ; son dossier de données suivra, avec une migration de l'ancien.
- Band devient EQ.
- Les emplacements ROM et les machines ROM ne servent plus : ils quittent la liste ; les samples de la banque d'usine seront extraits ou lus autrement, comme samples du Sampler.
- « Elektron content » et « UW BANK » laissent place à une extension de stockage, dans l'esprit d'un disque de samples intégré, qui gère tous les samples et toutes les banques au même endroit (import, banques d'usine lues dans l'image de l'utilisateur, dossiers) ; son nom évoque sans citer de marque.
- Les contrôles et noms marqués « incertain » sont rendus pour écoute ; l'utilisateur décrit ce qu'il entend, et le nom en découle.
