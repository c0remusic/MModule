# 16 — Push depuis Max for Live

Recherche du 2026-11-16, ticket [16](../issues/16-compagnon-push.md). [V] vérifié dans une source primaire, [INFER]
déduit ou rapporté par la communauté.

Sources : référence LOM `ControlSurface` et `PluginDevice` (docs.cycling74.com/apiref/lom/) ; notes de version de Push
Standalone 1.2.5, 2.3 et 2.4 (ableton.com/en/release-notes/) ; scripts Push de Live 12 Suite sur ce PC
(`MIDI Remote Scripts\pushbase`, `Push2`, noms des contrôles) ; forums Cycling '74 et Ableton (2017, 2023).

- **Prise des contrôles** [V] : `ControlSurface` offre `get_control_names`, `get_control`, `grab_control` (le contrôle
  perd sa fonction dans Live), `release_control`, `grab_midi`, `send_midi`, `pad_layout` (observable, Push 2 et 3).
  Noms utiles : `Button_Matrix` (pads 8 × 8), `Track_Controls` (8 encodeurs), `Track_Control_Touches`,
  `Track_Select_Buttons`, `Track_State_Buttons`, `Scene_Launch_Buttons`, et les boutons de mode. Push 3 répond à
  `get_control_names` (noms corrigés en 2.4). Orthographe à revérifier par `get_control_names` à l'exécution.
- **Pads** [V] : couleur par `send_value colonne rangée couleur` sur la matrice prise ; une couleur envoyée juste après
  la prise peut se perdre (petit délai). La vélocité arrive avec l'appui ; la pression par pad demande du MIDI brut
  [INFER].
- **Écran** [V] : aucune API LOM. En mode Device, Push montre les paramètres de la liste Push d'un plug-in et ceux d'un
  device M4L rangés en `live.banks`. Des externes tiers (`imp.push`, `p2d`) dessinent sur l'écran par USB, en le
  disputant à Live [INFER].
- **Modes** [INFER] : la prise appartient au device, pas à la piste sélectionnée ; un changement de mode ne la lâche pas
  mais Live peut repeindre les pads : suivre `pad_layout`. Deux devices qui prennent le même contrôle se gênent.
- **Push 1** [V] : même LOM, même script de base. **Push 3 autonome** : M4L y tourne [V] ; pas de VST3 tiers [INFER,
  forte confiance], donc rien à piloter.
- **Vers le plug-in** : un effet MIDI M4L devant l'instrument lui envoie du MIDI [INFER] ; le MIDI planifié par Max a
  une gigue de quelques ms [INFER]. Les paramètres d'un plug-in se lisent et s'écrivent par `live.object`
  (`Device.parameters`) [V], mais Live n'en expose que la liste configurée, 128 au plus [INFER]. `udpsend` et
  `udpreceive` en local marchent sur ordinateur [INFER].
