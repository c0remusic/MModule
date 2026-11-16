# Compagnon Push

Type: grilling
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by:

## Question

La v1 laisse Push aux 64 relais (huit pages de huit qui suivent les boxes de la piste affichée) et renvoie le
séquenceur sur Push à un compagnon Max for Live (`../../elektron-drums/issues/08-sequenceur-interne.md`, `grab_control`
à vérifier). Que fait ce compagnon (pas, pistes, patterns, locks, mode LIVE), sur quels Push, par quel mécanisme il
prend les contrôles de Push, comment il parle au séquenceur interne de Lockstep, et ce qu'il laisse à Live.

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling), sur les faits de
[research/16-push-max-for-live.md](../research/16-push-max-for-live.md).

- **Rôle :** le compagnon, un device Max for Live, fait de Push la façade du séquenceur interne. Pads : les 16 pas de
  la piste affichée sur deux rangées de 8 (appui : trig posé ou retiré, à la vélocité du pad) ; la rangée suivante :
  les 16 pistes, sélection et mute. Un pas tenu arme un lock que les encodeurs règlent comme des relais ; sans pas
  tenu, les encodeurs restent les relais (le compagnon, qui les a pris, les relaie lui-même aux paramètres relais de
  Lockstep). Mode LIVE et choix de pattern en second temps.
- **Push :** Push 2 et Push 3 en mode contrôleur. Ni Push 1, ni Push 3 autonome (pas de VST3 à piloter).
- **Liaison :** le compagnon se pose devant Lockstep sur la même piste et se présente par des notes MIDI, comme MoDule
  Seq (notes 124-127) ; ensuite lecture et écriture des patterns par UDP en local, Lockstep écoutant sur un port propre
  à l'instance. Ni fichier ni SysEx. Le compagnon ne joue pas les notes : il écrit le pattern, que le séquenceur
  interne joue à l'échantillon près (le MIDI de Max a quelques ms de gigue).
- **Ce qui reste à Live :** transport, automation, clips, arrangement. Les pas vivent dans le pattern de Lockstep,
  sauvé avec le set.
- **Écran :** aucun dessin (ni `imp.push` ni `p2d`). Les pas tenus et la piste affichée se lisent par la couleur des
  pads ; les noms des réglages restent ceux du mode Device, les relais.
- **Prise :** armé par l'utilisateur (bouton du device, ou User), le compagnon prend `Button_Matrix`, `Track_Controls`
  et `Track_Select_Buttons`, les lâche désarmé, suit `pad_layout` et repeint si Live reprend les pads. Un seul
  compagnon arme Push à la fois : le dernier armé gagne.
