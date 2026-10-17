# Le paramètre Machine et USR-01 à USR-16

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Comment un paramètre hôte choisit-il USR-01 à USR-16 ? Le paramètre Machine de chaque Track (et FocusMachine) va de 0
à 191 (`parameterDescriptions_mddrums.json`, ROM-48 = 191) ; X.20 numérote USR-01 à USR-16 de 194 à 209 (128 + 66 à
81). Étendre le maximum change la normalisation du paramètre : l'automation de Machine déjà enregistrée dans les sets
se décale. Choisir entre : étendre et l'accepter (0.1.x est une préversion), un numéro interne contigu (192 à 207)
traduit vers celui d'X.20 dans le Kit, un paramètre ajouté en fin de liste, ou aucun paramètre hôte (le navigateur
seul). Avec : ce que Live fait d'une valeur automatisée hors de l'ancienne plage, et un USR-05 choisi alors que rien
n'y est installé.

## Answer

Choix de l'utilisateur (2026-10-17) :

- **Machine va jusqu'à 209** (et FocusMachine avec lui) : USR-01 à USR-16 aux numéros d'X.20, 194 à 209 ; 192 et 193
  restent sans Machine. Un seul paramètre, une seule notion.
- **Le prix est accepté** : un hôte garde l'automation en valeur 0-1, que MoDule convertit par le maximum
  (`NormalisableRange` de `jucePluginLib/parameter.h`) ; l'automation de Machine des sets 0.1.0 et 0.1.1 se décale
  (ROM-48, 1.0, deviendrait USR-16). Les Kits et l'état du plug-in ne bougent pas, eux. Les notes de 0.1.2 le disent.
- **USR-05 sans rien d'installé** joue muet, comme ROM-33 à ROM-48 vides ; le navigateur montre les places vides en
  grisé, sans nom. Pas de refus.
