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
