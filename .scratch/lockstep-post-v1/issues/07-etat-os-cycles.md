# État réel de l'OS du Model:Cycles et ordre de ses machines

Type: task
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 06

## Question

Le ticket 11 de la planification v1 a fait tourner le répartiteur des voix du Cycles hors OS, mais sans état réel :
pas de vecteurs musicaux, et l'ordre SNARE/METAL/PERC des indices 1-3 n'est qu'inféré. Enregistrer un état réel de
l'OS (digiemu ou le banc Unicorn, sous garde de durée), confirmer l'ordre des six machines, et enregistrer des vecteurs
de chacune à des réglages connus, hors du dépôt. Résultat : l'ordre confirmé, les vecteurs, le coût mesuré par voix.

## Answer

Fait le 2026-11-16 : [research/07-etat-os-cycles.md](../research/07-etat-os-cycles.md) (section 3 de l'OS 1.13
exécutée par morceaux, Unicorn stock plus l'EMAC de SyntHack ; scripts et vecteurs hors dépôt).

- **Ordre confirmé : 0 KICK, 1 SNARE, 2 METAL, 3 PERC, 4 TONE, 5 CHORD**, celui du manuel, par trois preuves qui ne
  dépendent pas de l'indice deviné : les fiches de paramètres de l'OS (`0x4010dce0`) portent le numéro de machine à
  côté du nom ; le répartiteur lit l'octet haut du mot ALG que ces fiches décrivent, et l'indice de rendu exécuté au
  banc est celui d'ALG ; le son de chaque indice est celui de son nom (KICK sinus à 70 Hz, SNARE bruit et corps, METAL
  grappe inharmonique à 3,4-5,3 kHz, PERC partiels inharmoniques, TONE harmoniques impairs, CHORD trois hauteurs).
- **État :** pas un boot complet. digiemu exige son Unicorn patché, dont la construction demande de télécharger les
  sources d'Unicorn, ce qui n'a pas été fait. L'état vient de l'OS lui-même : chaque son bâti avec les valeurs par
  défaut de ses fiches, chargé par les routines de l'OS, puis chaque bloc passe par le lissage, la modulation de
  vélocité, le LFO, le répartiteur, le gain de piste et VOL+DIST de l'OS, dans l'ordre de sa tâche audio. Hypothèses
  marquées [INFER] dans la recherche : note du trig 60, modulation de vélocité neutre, LFO par défaut.
- **Vecteurs :** 108 cas (6 machines × 18 : défaut, PTCH ±12, DEC, PUNCH, COLR/SHPE/SWEP/CONT aux bornes, trois
  vélocités, gate), 1 000 blocs chacun, dans `elektron-work\vectors-cycles` avec un `README.md` (format, régénération).
- **Coût mesuré** (instructions ColdFire, update + render d'une voix, par bloc de 32) : KICK 7 444, SNARE 7 166,
  METAL 6 439, PERC 6 087, TONE 5 305, CHORD 8 303 ; le même que la voix sonne ou non, à 1 % près. Hors voix, pour six
  pistes : répartiteur 1 628, contrôle 2 873, gain et VOL+DIST 6 697.
- **Faits du banc :** un trig sonne un bloc plus tard ; la vélocité n'entre pas dans la voix, elle agit après, en gain
  linéaire de piste ; une voix ne dépend pas de sa piste ; la note du trig fixe la hauteur absolue.
- **Reste :** un état démarré avec le Kit d'usine (lèverait les hypothèses, demande le téléchargement ci-dessus, donc
  l'accord de l'utilisateur) ; mix, delay, reverb et étage de sortie sans vecteurs ; second masque du répartiteur et
  LFO actif non couverts.
