# Interface façon Overbridge

Type: prototype
Status: open
Assignee:
Blocked by: 

## Question

Réagir au mockup v1
(`.scratch/elektron-drums/mockup/v1.html`) : pages, organisation de la piste, chaîne visible, navigateur de machines,
mix, FX, kits. Les itérations suivantes portent les décisions des autres tickets.

Exigences de l'utilisateur (2026-10-24), du mockup v2 :
- Une box par section, comme les écrans Overbridge (v1 refusé : trop proche de MoDule).
- La box AMP trop grande face aux boxes chargées : tailler chaque box à son contenu.
- Locks par pas sans tenir le pas : options sélection verrouillée (v3a) et lignes de locks (v3b), voir le ticket du séquenceur interne.
- LFO : les mapper facilement avec un ASSIGN, alors que le LFO et ses cibles sont sur des sous-pages différentes ; proposé ASSIGN qui reste armé d'une sous-page à l'autre, poignées LFO à glisser sur un knob depuis la bande de chaîne, menu de destinations groupé par box, profondeur réglée sur le knob ciblé.
- Changer d'onglet pour mapper est pénible : une seule page par piste, sans sous-pages, comme Overbridge Rytm (toutes les boxes visibles, compactes, une box qui s'agrandit au clic sur son en-tête), et assignation depuis le knob lui-même (pastille « + » au survol ou clic droit : LFO 1, LFO 2, lock sur les pas sélectionnés, profondeur sur l'anneau).
- Pas de rangée de pads ni de clavier en bas : un petit bouton play dans chaque onglet de piste (écoute sans sélectionner) et une LED qui s'allume quand la piste est déclenchée.
- Onglet SEQ (v4) : ne pas afficher toute la vue de piste en réduit, trop petite ; montrer seulement les paramètres verrouillables de la piste (liste groupée par box) pour choisir les lignes de locks.
- Règle de l'utilisateur (2026-10-24, après la critique du v6) : pas de texte inutile, pas de redondance ; jamais deux fois la même information, surtout en lignes de texte ; l'état se lit sur le contrôle, un badge ou une infobulle, pas dans une phrase. L'interface est déjà trop dense : dans le doute, retirer.
- Box de pas (critique du v6) : les rangées ACCENT et SLIDE n'étaient pas alignées sur les pas, et leur étiquette prenait une case ; accent et slide se dessinent sur les pas eux-mêmes, sur la même grille (barre d'accent en haut du pas, liaison de slide vers le pas suivant), édités par un mode TRIG / ACC / SLIDE dans l'en-tête. Tout ce qui est sur la grille s'aligne sur les 16 colonnes.
- Pas de bande de chaîne : l'ordre du signal se lit seulement par l'ordre des boxes (machine, étages avant le filtre, filtre, étages après, ampli, envois) ; basculer un emplacement avant/après déplace sa box de l'autre côté du filtre.
- Retours sur le v7 : coins carrés comme Overbridge (pas d'arrondis) ; disposition fixe, les boxes ne bougent jamais (le bouton avant/après est un état de l'emplacement, pas un déplacement) ; les deux drives dans une seule box, en série ; pas d'agrandissement de box ; forme d'onde du sample visible dans le layer, et dans la box machine pour une machine qui joue un sample (DT2, SP TWINSHOT, ROM du MD) ; onde de sortie de la voix pour une machine de synthèse ; une seule façon d'assigner une modulation, le menu « + » du knob ; le nom du son (preset nommé) ne s'affiche que s'il existe, la machine n'apparaît qu'une fois.
- Pas d'unité « % » dans les valeurs ; police plus caractérielle que Barlow, jugée trop générique (variantes IBM Plex Sans Condensed, Archivo Narrow, Saira Semi Condensed dans le v8).
- Assignation des LFO : seulement par le bouton ASSIGN de la box LFO (armé, puis toucher un knob) ; le menu « + » du knob garde seulement Vélocité et Lock ; plus de poignées à glisser.
- Alignements : texte des menus déroulants centré verticalement, même hauteur que les boutons voisins ; bouton LIVE placé dans l'en-tête de la box de pas, aligné avec TRIG/ACC/SLIDE et les pages ; chaque rangée de contrôles partage une ligne de base et une hauteur.
- Accent : la barre en haut du pas fait « trop IA » ; variantes dans le v8 : intensité de LED (trig normal à mi-intensité, accentué pleinement allumé, comme les touches Elektron), couleur de trig, hauteur selon la vélocité.
- Valeur au centre des knobs : jamais écrasée, tronquée ou chevauchée par l'anneau, vérifiée avec la valeur la plus longue de chaque paramètre.
- Choix du v8 : police Saira Semi Condensed (valeurs en IBM Plex Sans Condensed pour les chiffres fixes) ; accent par hauteur selon la vélocité ; trigs dans l'accent unique (teal) ; plus aucune barre ou bande colorée pour marquer une sélection : fond plus clair et texte plein, comme Overbridge.
- Valeurs toujours dans l'anneau du knob, jamais dessous : taille de texte unique par taille de knob, knobs agrandis au besoin (deux tailles au plus), formats raccourcis.
- Mêmes proportions pour tous les knobs : texte de valeur, épaisseur d'anneau et étiquette dans un rapport fixe au diamètre ; on ne réduit jamais le texte d'un seul knob pour faire tenir une valeur.
- Plus d'anneaux en pointillés ou en tirets autour des knobs : modulation = fin arc plein à la couleur du modulateur, de la valeur à valeur + profondeur ; lock = valeur à la couleur d'accent ; valeur jouée = petit repère sur l'anneau.
- Couleurs (retour sur le v11) : comme Overbridge, une couleur saturée par section qui hiérarchise : anneaux des knobs, écran teinté et contrôles sélectionnés de la box dans sa couleur ; en-têtes neutres ; teal global réservé à l'état global. La passe taste avait trop désaturé.
- CPU : en haut, à gauche de « Kit editor », en pourcentage (seule exception au « pas de % ») avec une petite barre (0-100 % d'un cœur, repère à 15 %, ambre au-dessus de 15 %, rouge au-dessus de 25 %).
- Pile de l'interface (décidé avec l'utilisateur le 2026-10-25) : JUCE seul, en C++. Composants JUCE dessinés dans
  paint, grille fixe mise à l'échelle, repeinte limitée aux zones qui changent. Pas de RmlUi, pas de WebView, pas de
  JavaScript : le HTML ne sert qu'aux mockups. Le dessin des knobs et de la vue Hit de MoDule se reprend dans paint.
