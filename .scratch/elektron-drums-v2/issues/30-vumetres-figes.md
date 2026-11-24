# Les vumètres suivent le son

Type: AFK
Status: done (écoute dans Live à venir)
Assignee: Claude (session 2026-11-24)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : certains vumètres ne s'actualisent pas du tout.

Relevé : les vumètres de piste (écran Master et LCD au-dessus des touches) ne mesuraient pas le son. Ils allumaient
« voix vivante » (`liveTracks`) × position du fader : une piste qui sonne sans être vue vivante restait éteinte, une
piste vivante montait au fader quel que soit son niveau réel. Retours et Main lisaient déjà de vrais pics.

Correction : chaque piste mesure son pic dans le mix du moteur (après niveau, avant pan, `Engine::takePeak`), le
Processor le fait retomber de 20 dB/s comme les sorties (`Processor::trackPeak`), et les deux vumètres de piste
l'affichent à l'échelle des retours.

## Critère de fin

- [x] `edDrumsProcessorTest` : les 16 pistes d'un kit à machines de toutes familles donnent un pic, une piste à
  niveau 0 aucun, les pics retombent sous -60 dBFS en 4 s.
- [ ] Vu dans Live par l'utilisateur.
