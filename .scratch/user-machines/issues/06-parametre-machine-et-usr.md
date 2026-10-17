# Le paramètre Machine et USR-01 à USR-16

Type: grilling
Status:
Blocked by:

## Question

Comment un paramètre hôte choisit-il USR-01 à USR-16 ? Le paramètre Machine de chaque Track (et FocusMachine) va de 0
à 191 (`parameterDescriptions_mddrums.json`, ROM-48 = 191) ; X.20 numérote USR-01 à USR-16 de 194 à 209 (128 + 66 à
81). Étendre le maximum change la normalisation du paramètre : l'automation de Machine déjà enregistrée dans les sets
se décale. Choisir entre : étendre et l'accepter (0.1.x est une préversion), un numéro interne contigu (192 à 207)
traduit vers celui d'X.20 dans le Kit, un paramètre ajouté en fin de liste, ou aucun paramètre hôte (le navigateur
seul). Avec : ce que Live fait d'une valeur automatisée hors de l'ancienne plage, et un USR-05 choisi alors que rien
n'y est installé.
