# Paramètres hôte, relais Push, état

Type: AFK
Status: resolved
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

## État (2026-11-12)

Relais Push faits. 64 paramètres hôte ajoutés en fin de `edDrumsParameterIds.txt` (`push_*`, 1 594 identifiants en tout), en
8 pages de 8 dans l'ordre des banques de Push, chacun visant le paramètre du même nom sur la piste affichée par l'éditeur
(`parameters::relaySpecs`, `Processor::takeRelays`, `syncRelays`). Le choix suit ce que la main cherche en jouant : page 1
Machine 1-8 ; page 2 la piste elle-même (relais Track 1-16 qui choisit la piste affichée, comme le relais 0 de MoDule,
puis Level, Pan, Note, les quatre envois) ; page 3 le filtre et son enveloppe (type, mode, coupure, résonance, largeur,
attaque, déclin, profondeur) ; page 4 les deux drives (type, quantité, mélange, place) ; page 5 l'EQ (type, place, six
bandes) ; page 6 l'ampli et l'accent ; pages 7 et 8 chaque LFO (cible, profondeur, onde, déclenchement, sync, note,
fréquence libre, feel). Restent aux paramètres propres de chaque piste : Machine 9-24, la vélocité, retard, sustain,
release et retrig de l'enveloppe de filtre, le suivi de clavier, la piste d'un LFO, la sortie, la machine et les Chokes.
Les noms tiennent dans les dix caractères de Push.

Comportement repris de MoDule (ticket 34) : un relais réglé par l'hôte va au paramètre de la piste affichée au bloc
suivant, sans rien dire à l'hôte et sans Lock ; le minuteur donne aux relais les valeurs de la piste affichée, en
éditions sans geste quand la piste affichée change, en silence sinon (et avant chaque relecture de l'hôte : état, Kit,
son de piste, défauts d'une machine) ; rien au chargement ; un relais tourné que le fil audio n'a pas encore pris
attend. Ni l'état ni un Kit ne gardent de relais. La piste affichée n'est pas gardée dans l'état.

État compressé fait : le XML (toujours version 6) passe par le zlib de JUCE derrière un en-tête à lui (« LKSZ », format
1, taille du XML) ; `Processor::readState` lit aussi le XML non compressé d'avant. Mesuré : set par défaut 28 752 octets
avant, 4 672 après (16,2 %) ; set plein (128 patterns, 16 pistes de 64 trigs, 4 096 Locks par pattern) 26 499 954 octets
avant, 999 694 après (3,8 %).

Vérifié : `edDrumsVst3HostTest` (rien dit au chargement ; chaque relais règle sa cible sur la piste 1, relue par les
relais en revenant sur la piste 1 et par l'état dans une autre instance ; piste 2 montrée, 63 éditions et 0 geste) ;
`edDrumsProcessorTest` (liste figée, relais en process, état compressé et état version 6 non compressé rendant les mêmes
échantillons que le set) ; `edDrumsEditorTest`. Reste du ticket : rien de ce qui précède ; le comportement dans Live et
sur un Push réel n'est pas mesuré.

Les cibles des relais vivent sur le tas : 9 Ko de plus par processeur faisaient déborder la pile de `kits()` dans `edDrumsProcessorTest`, qui en tient six sur la pile.
