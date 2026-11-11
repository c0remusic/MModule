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

## Noté par le ticket 11 (2026-11-06)

- Persister la machine en famille + id, jamais par index : la liste du paramètre Machine bouge à chaque portage. Figer la liste
  d'identifiants par test.
- `Processor::sync` lit tous les paramètres à chaque bloc : à rendre piloté par changement.
- `dataFolder` suit le nom de travail, à changer avec lui. « Elektron Drums Dev » imite la marque d'autrui : nom à trancher avant
  toute publication.

## Noté par le ticket 12 (2026-11-06)

Les paramètres de chaîne ne sont pas encore des paramètres hôte. `Chain` est seul propriétaire des réglages ; rajouter une lecture (
`Chain::settings`) pour l'état, `Engine::chain` a été retiré faute d'appel. Le changement de famille recharge la chaîne sans demander :
la question reste à l'interface. Un type d'étage automatisé à la cadence d'un bloc attend la fin de chaque fondu.

## Noté par le ticket 13 (2026-11-06)

`setLfo` est idempotent, les paramètres hôte restent la vérité : pas de relecture à ajouter dans `Engine`. Retirer alors `Processor::engine()` : `edDrumsVst3HostTest` entendra les LFO suivre le transport. Un retarget automatisé est déjà couvert par la pente à la cible.

## État (2026-11-08)

Version réduite faite : 1 267 paramètres hôte (75 par piste, le reste partagé), identifiants figés dans
`edDrumsPlugin/edDrumsParameterIds.txt` et vérifiés par `edDrumsProcessorTest`, chaîne, LFO, envois, effets d'envoi et
master compris. État version 3, valeurs par identifiant, défaut pour un identifiant absent ; il garde aussi les 128
patterns et le sample de chaque piste. Pas faits : relais Push, compression de l'état.
