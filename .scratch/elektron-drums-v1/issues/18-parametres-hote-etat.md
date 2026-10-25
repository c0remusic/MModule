# Paramètres hôte, relais Push, état

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 12, 13, 14, 15, 17

## Quoi

Tous les réglages de piste, d'effet et du master en paramètres hôte (~1 260), identifiants figés dans un fichier
de description (ajouter, jamais renuméroter) ; paramètres de machine génériques « T01 Machine 1-8 » à valeur nommée ;
machine et famille hors automation ; 64 relais en 8 pages vers la piste affichée ; paramètre « Pattern ». État
versionné et compressé : kit joué, son emplacement, 128 patterns, réglages globaux, samples utilisés ; valeurs par
identifiant, défaut pour un identifiant inconnu. Reprendre les leçons de MoDule sur ce que l'hôte voit (édition ou
relecture, rien dit à l'hôte au chargement).

## Critère de fin

- Chaque paramètre se règle et se relit par l'hébergement VST3 ; un test fige la liste des identifiants.
- Un état restitué rend les mêmes échantillons ; un état ancien à paramètre manquant se charge.

## Références

Spécification §8, §9. Tickets « Paramètres hôte et sorties », « Format des kits et de l'état ».
