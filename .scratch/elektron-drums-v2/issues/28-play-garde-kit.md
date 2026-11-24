# Play garde le Kit joué

Type: AFK
Status: done
Assignee: Claude (session 2026-11-24)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : les kits se réinitialisent quand on relance la lecture, et aucun paramètre
édité ne tient au Play suivant.

Cause : le séquenceur signalait un départ de pattern à chaque départ du transport (`Sequencer::process`, `started`),
et le Processor chargeait alors le Kit assigné au pattern. Les patterns d'usine portent chacun leur Kit (pattern n,
Slot n) : chaque Play rechargeait le Slot 1 par-dessus le Kit joué et ses éditions.

Correction : seul un autre pattern charge son Kit (une demande faite à l'arrêt comprise). Le même pattern relancé, ou
un saut de l'hôte, garde le Kit joué tel qu'édité, comme les machines Elektron.

## Critère de fin

- [x] `edDrumsProcessorTest` : Kit édité après chargement, transport arrêté puis relancé, la valeur éditée tient
  (échouait avant la correction).
- [x] Le changement de pattern charge toujours son Kit, sans allocation (test existant).
- [ ] Écoute dans Live : éditer, Stop, Play, la valeur tient.

## Point ouvert

Une assignation de Kit faite à l'arrêt sur le pattern en cours ne se charge plus au Play : elle se charge au prochain
départ de ce pattern depuis un autre.
