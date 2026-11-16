# Oracle des effets d'envoi du Digitakt II

Type: task
Status: open
Assignee:
Blocked by: 14

## Question

« Les effets du Digitakt II en envois » attend de savoir si le delay et la reverb du DT2 sont ceux du Syntakt. Le
pilote d'envois `0x1c18a6` tourne hors ligne depuis l'instantané du master, mais ses retours restent à zéro
(`research/14-effets-dt2.md`). Tracer sa sortie jusqu'aux retours (ou l'appeler avec le master dans le même bloc,
comme `0x1c2b24`), mapper ses 19 mots de réglages aux 23 réglages, identifier les tableaux d'envoi CHO, DEL et REV,
poser le tempo, sous garde de durée, hors dépôt. Enregistrer des vecteurs courts de chaque effet et comparer delay et
reverb à ceux du Syntakt portés dans Lockstep. Résultat : identiques ou non, les vecteurs, le coût mesuré.
