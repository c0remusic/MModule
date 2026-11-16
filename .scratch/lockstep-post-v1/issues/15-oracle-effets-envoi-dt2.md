# Oracle des effets d'envoi du Digitakt II

Type: task
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 14

## Question

« Les effets du Digitakt II en envois » attend de savoir si le delay et la reverb du DT2 sont ceux du Syntakt. Le
pilote d'envois `0x1c18a6` tourne hors ligne depuis l'instantané du master, mais ses retours restent à zéro
(`research/14-effets-dt2.md`). Tracer sa sortie jusqu'aux retours (ou l'appeler avec le master dans le même bloc,
comme `0x1c2b24`), mapper ses 19 mots de réglages aux 23 réglages, identifier les tableaux d'envoi CHO, DEL et REV,
poser le tempo, sous garde de durée, hors dépôt. Enregistrer des vecteurs courts de chaque effet et comparer delay et
reverb à ceux du Syntakt portés dans Lockstep. Résultat : identiques ou non, les vecteurs, le coût mesuré.

## Answer

Fait le 2026-11-16 : [research/15-oracle-effets-envoi-dt2.md](../research/15-oracle-effets-envoi-dt2.md) (tout hors
dépôt ; le port de Lockstep compilé hors du dépôt, sources lues seulement).

- **L'oracle marche :** `elektron-work\tools\dt2\dt2_send_oracle.py`, garde de durée intégrée, en mode unité (un
  effet appelé comme le pilote `0x1c18a6` l'appelle, identique au bit près à l'effet dans le pilote) ou pilote entier.
  Les retours restaient à zéro parce que VOL et les envois entre effets valent 0 dans l'instantané (l'orchestrateur
  les écrit à chaque bloc) ; deux états lissés partent loin du repos. Tableaux d'envoi : CHO `0x252658`, DEL
  `0x2525cc`, REV `0x252540` ; réglages mappés aux 24 mots de la trame d'effets.
- **Vecteurs :** 29 cas courts dans `elektron-work\vectors-dt2\sends` (13 delay, 8 reverb, 6 chorus, 2 pilote) avec
  un `README.md`.
- **Coût de l'original** (instructions SHARC par bloc de 32) : chorus ~14 400, delay ~5 400, reverb ~14 560.
- **Delay : celui du Syntakt.** Même loi de temps (mêmes échos à l'échantillon), mêmes prises, mêmes pôles de filtre,
  même lissage. Écarts : retour × 0,5, FDBK sur 32 768 contre 66 053/2³¹ (0,8 %), lissage du temps flottant (un
  échantillon d'écart possible). Ces deux constantes appliquées, l'écart au port vaut −97 à −128 dB.
- **Reverb : un autre algorithme.** Même loi de pré-delay, réseau différent (corrélation ≤ 0,015 avec le port, écho
  fort toutes les ~1 022 échantillons, sortie clairsemée, déclin plus lent).
- **Reste :** où va le retour du chorus ; lois des réglages de reverb et chorus et longueurs du réseau de reverb ; loi
  du mot sec/mouillé ; aucune comparaison au matériel.
