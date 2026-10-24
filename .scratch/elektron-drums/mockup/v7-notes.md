# Maquette v7 — passe de critique et de finition

`v7.html` (1600x1000) part de v6. v1 à v6 restent intacts. Deux passes : la critique de v6 (huit points, dans
l'ordre de priorité), puis les règles de [jakubkrehel/make-interfaces-feel-better](https://github.com/jakubkrehel/make-interfaces-feel-better)
(`SKILL.md`, `surfaces.md`, `typography.md`, `animations.md`, lus sans rien installer ni lancer). Pendant le travail,
cinq décisions de l'utilisateur sont arrivées et priment sur la critique : rien d'inutile ni de redondant, la bande
de chaîne supprimée, accent et slide dessinés sur les pas, chokes par pistes cibles, FM DRUM du Digitone II retiré.

Vérifiée en Chrome headless (CDP, sans dépendance) à 1600x1000 sur quinze états : piste Syntakt, menu de machine,
choke, piste MD, piste à chaîne mixte, piste DT2 avec BAND et drive B en POST, Sample layer ouvert, prompt de
famille, menu du knob, FX, MASTER, SEQ, KIT, Sound browser, navigateur par type avec sélection multiple. Résultat :
**0 erreur JS, 0 débordement, 0 texte sous 11 px, 0 texte sous 4,5:1, 0 cible sous 24 px**. Tailles rendues :
11, 12, 14, 16, 20 px. Captures dans `v7-shots/`.

Le script de contrôle compose les fonds de la racine jusqu'au texte et applique les opacités héritées (un texte
grisé par `opacity` compte), mesure chaque bouton, select, champ, knob, pas, ligne et poignée visible, et vérifie
que chaque élément d'une boîte reste dans sa boîte (ou dans son conteneur défilant, sans défilement horizontal).

## Critique de v6, point par point

| # | Critique | Correction |
|---|---|---|
| 1 | Hiérarchie : aplats de la forme d'onde et du LFO plus brillants que l'identité de la piste | Bande de tête refaite : nom du son en 20 px semi-gras, puis le bouton de machine (famille 14 px gris, machine 20 px semi-gras). Ce sont les deux premières lectures. Graphes : contour 1,5 px + aplat 16-18 % ; forme d'onde en contour (plus de barres pleines), seule la partie jouée (début → fin) prend la couleur, le reste en gris ; LFO : contour à 80 %, le point de lecture reste plein |
| 2 | Contrastes ratés (numéros de pas 2,07:1, « 33-48 » 3,42:1, « 1:2 » 3,71:1, « (empty) » 3,38:1) | Numéros et conditions des pas trig en encre sur le rouge ; pas verrouillés gardent le rouge (plus d'ambre), un point blanc dit le lock. Gris inactif `--dim` passé de `#7a7e85` à `#999ca2`, gris secondaire `--mut` de `#a2a6ac` à `#a9adb3`. L'inactif se lit par le poids (400) et un contour pointillé ou un contour plein, plus par un gris illisible : onglets de page hors longueur, sources futures (DT2), emplacements de kit vides, choix désactivés du menu |
| 3 | Cibles de 20-22 px | Tout passe à 24 px au moins : boutons de pied, segments, types de filtre et de forme, M/S et selects du mixer, lignes de paramètres SEQ, poignées LFO, LOCK REC, ASSIGN. Le « + » du knob et le ▶ des onglets ont une zone de 24 px pour un dessin de 16 px ; les poignées de l'enveloppe d'ampli un cercle de 24 px pour un point de 12 |
| 4 | Espace perdu : drive Off sur un slot entier, grille de machines sur un tiers de la boîte | Un drive Off n'est plus qu'une ligne d'en-tête (nom, menu de type, PRE/POST), empilée sur la boîte suivante sans prendre de colonne. La grille de machines quitte la boîte : le nom de machine de la bande de tête ouvre un popover (familles à gauche, grille à droite). Les 8 knobs prennent toute la boîte (52 px) |
| 5 | État de démo incohérent (piste 1 Syntakt avec BAND DT2, LP4, SRR MD) | Piste 1 : la chaîne de sa famille (drive Syntakt pré, BASE/WIDTH, LP, drive B Off). La chaîne mixte passe sur la piste 13 (PC CARBON : B-W DT2, LP4, SRR MD post). Le tableau du kit lit maintenant les chaînes réelles. Pas de lecture « firmware-exact » (voir plus bas) : l'absence de « + » le dit |
| 6 | Navigateur : l'onglet « Kits » listait des machines | Refait d'après le ticket 21 (Q12-Q14), voir plus bas |
| 7 | SAMPLE LAYER au jalon DT2 | Boîte repliée par défaut sous Machine, en-tête seul avec une pastille « DT2 milestone ». Un clic l'ouvre par-dessus ses voisines (même mécanisme que l'agrandissement de boîte) ; Esc la referme |
| 8 | Bande de tête trop petite et dense | Une rangée de 48 px, identité seule. La bande de chaîne a ensuite été supprimée (décision utilisateur) |

## Décisions de l'utilisateur pendant la passe

- **Rien d'inutile, rien de répété** (règle au-dessus de toutes). Liste complète des retraits plus bas.
- **Plus de bande de chaîne.** L'ordre du signal est l'ordre des boîtes, de gauche à droite puis de haut en bas :
  rangée 1 = Machine (+ Sample), drives et BAND placés avant le filtre, Filter ; rangée 2 = BAND et drives placés
  après le filtre, Amp, Sends, puis LFO (hors flux). Steps en bas, hors flux. Chaque drive est sa propre boîte
  (Drive A, Drive B) ; basculer PRE/POST d'un drive ou de la BAND déplace sa boîte de l'autre côté du filtre.
  Deux drives actifs du même côté : une icône ⇄ dans l'en-tête du premier les inverse (plus de bouton « Swap A / B »
  ni de phrase d'état). Les poignées LFO 1 / LFO 2 de la bande sont devenues les onglets de la boîte LFO : un clic
  choisit le LFO, un glisser de 4 px ou plus le porte sur un knob (pas de doublon onglet + poignée).
- **Accent et slide sur les pas.** Les rangées ACCENT et SLIDE et leurs libellés sont supprimées, dans la boîte
  Steps de la piste comme sur l'onglet SEQ. Accent = barre jaune de 3 px au bord haut du pas ; slide = lien violet
  du pas vers le suivant, dans la gouttière. Édition : bascule TRIG | ACC | SLIDE dans l'en-tête de Steps, ou
  Alt-clic (accent) et Maj-clic (slide) ; Ctrl-clic garde la sélection multiple. Tout ce qui est sur la grille tient
  dans les 16 colonnes. La boîte Steps de la piste rend ses deux rangées aux pas, plus hauts.
- **Chokes par cibles, plus de groupe.** Le select « Choke : None / Group A » de la page Sound disparaît. Un bouton
  « Choke » dans la bande de tête ouvre une grille de 16 pistes (la piste elle-même désactivée, cibles allumées) ;
  les numéros des cibles s'affichent dans le bouton, rien quand il n'y en a pas. Démo : piste 9 (CH) coupe la 10 (OH).
  Colonne Choke ajoutée au tableau du kit.
- **FM DRUM du Digitone II hors périmètre** : retiré des familles, des libellés et des paramètres provisoires.

## Textes et éléments retirés (redondants ou explicatifs)

| Retiré | Pourquoi |
|---|---|
| Bande de chaîne entière (puces Machine ▸ Drive ▸ Band ▸ Filter ▸ Amp ▸ Sends, libellés « Signal » et « Mod ») | L'ordre des boîtes dit l'ordre du signal |
| « Chain: firmware-exact » / « n additions + » | Les badges « + » sur les types et les knobs le disent déjà |
| Sous-titre de Machine « BD MODERN · Syntakt digital » | Machine et famille sont dans la bande de tête, une seule fois |
| Rangée des familles et grille des machines dans la boîte | Dans le popover du nom de machine |
| Note « Syntakt OS port, exact… Families marked + are approximations » (page Sound) | Infobulle du bouton de machine ; le « + » se lit seul |
| Drive : sous-titre « 2 slots », « Off: the slot passes the signal », « A before the filter, B after. Same side: SWAP », « Both pre: A, then B », « Oversampling: global, in the footer » | État montré par la position des boîtes, le menu de type, l'icône ⇄ ; le réglage global est dans le pied |
| BAND : sous-titre « before / after the filter », légende du graphe « BASE / WIDTH (Syntakt) » | PRE/POST et le type choisi sont allumés juste à côté |
| Filter : sous-titre « (LP · Syntakt digital · its envelope moves the whole band) », légende « all types » sous l'enveloppe | Le type est allumé ; la source se lit au « + » ; infobulle des types |
| Amp : sous-titre « end of chain », « Amp envelope (MODE) », légende « ADSR » dans le graphe, encart et note MD « the envelope stays in the voice… » | Le mode est le segment AHD/ADSR ; sur une machine MD, l'enveloppe disparaît et Amp ne montre que Level, Pan, Accent (infobulle de l'en-tête) |
| Knobs grisés (Sustain/Release en AHD, Hold en ADSR, knobs vides des machines MD) | Masqués : un knob qui ne sert pas n'apparaît pas |
| « LFO 1 ASSIGN: touch a knob » | ASSIGN allumé dit l'armement ; infobulle |
| Steps : sous-titre « Track 1 · BD 1 · pattern A01 · page 1 of 2 », bannière « LOCK: step 5 », « Live, playing », « STEP 5 · 3 LOCKS », « Turn a knob: it locks here. Esc releases » | Piste dans l'onglet, pattern dans son menu, page dans ses onglets, sélection par le contour blanc, locks par la liste elle-même |
| Menu du knob : titre (nom du paramètre) et note « Then drag the coloured ring… » ; « Lock: select steps first » | Le menu s'ouvre sur le knob ; le choix désactivé reste « Lock » avec infobulle |
| Pied : « Mockup v6 · layout only, no sound », mot-symbole « elektron » | Déjà dans la barre du haut ; la mention maquette passe en infobulle de la version |
| FX : quatre sous-titres « send FX », quatre notes de source, légende du delay (temps), « Drag a cell to set a send… » | La page s'appelle FX ; la source est l'onglet allumé ; le temps est sur son knob |
| FX : sous-titres « all 16 tracks, per effect », « level and output » | Le contenu le montre |
| MASTER : « Digitakt II · first on Main », « Digitakt II · then clip », note de l'overdrive, « IN LR / L / R: the host's sidechain input ? », « Out: Main + 3 Outs », « Master: Comp ▸ OD ▸ Clip », légende « transfer curve (drawn) », sous-titre « 16 channels » | Ordre lu de gauche à droite et en infobulle ; les sorties sont dans les selects du mixer |
| SEQ : « A01 · queued A02 », « 24 ppqn », sous-titres de Steps, Parameters et Lock lanes, note des lanes | Pattern joué et en attente sont allumés dans la grille ; le reste en infobulle |
| KIT : « slot 07 », « Changed since saved (…) », « Ctrl+S saves », ligne « Tracks 16 · Samples 3 · Families … · CPU est. », « (empty) », « 16 tracks » sous chaque kit, colonne Sample du tableau | Emplacement allumé avec son point « modifié » ; Ctrl+S en infobulle de Save ; le tableau dit familles et pistes ; tout kit a 16 pistes ; le layer est au jalon DT2 |
| Navigateur : titre « Sound browser » | C'est l'onglet du haut |

Gardés malgré la règle, et pourquoi : le nom court de piste dans les onglets (« BD 1 », navigation sur 16 pistes)
alors que la bande de tête montre le nom du son ; le nom de machine sous chaque tranche du mixer (page MASTER, où
la bande de tête n'existe pas) ; le texte du prompt de changement de famille (décision irréversible, il montre les
deux chaînes) ; le libellé « Drive oversampling » (sans lui le segment Off/2x/4x ne dit rien).

## Navigateur de sons (ticket 21, Q12-Q14)

- Boîte « Elektron content » en haut : le dossier Elektron (zone de dépôt, chemin), « Import my Elektron
  content… » et « Open folder », puis cinq cartes de ce qui a été reconnu : firmware Syntakt OS 1.41 (samples du
  TWINSHOT), image flash du Machinedrum (banque UW ROM-01 à -32), pack d'usine du Rytm, pack d'usine du DT2
  (**Missing**, carte en contour, l'infobulle dit où le prendre), exports Transfer (2 dossiers).
- Trois colonnes. **Library** : arbre `By source` (Kits n / 64, Track sounds, Samples : House, My samples par dossier,
  Elektron par machine) ou `By type` (Kick, Snare, Clap, Closed hat, Open hat, Cymbal, Tom, Perc, Chord, Tonal, FX,
  Loop, Other, avec leurs comptes). **Liste** : recherche, filtres par tag, colonnes Name, Type, Tags, Length, Origin.
  **Preview** : forme d'onde, lecture, boucle, audition au clic, Level, Tune, fréquence et canaux, « Load on <son> ».
- Clic = audition (barre teal sur la ligne), double-clic = chargement sur la piste montrée, glisser vers un onglet
  de piste, l'onglet du kit, la boîte Sample ou un emplacement de kit (l'onglet visé s'encadre).
- Type deviné : en italique tant qu'il est deviné, infobulle « Guessed from the name » ou « from the audio » ;
  modifiable dans la ligne (il passe en romain). Ctrl/Maj-clic : sélection multiple, une barre « n selected ·
  Retype… · Tag… » apparaît.

## make-interfaces-feel-better : appliqué

| Règle | Application |
|---|---|
| Rayons concentriques (extérieur = intérieur + marge) | Boîte 10 px = contrôle 2 px + marge 8 ; popovers 8 px = élément 4 + marge 4 ; prompt 14 px = bouton 2 + marge 12 |
| Ombres pour l'élévation, bordures pour la structure | Popovers, menu du knob, prompt, boîte agrandie : anneau blanc 8 % + ombre portée. Champs de saisie gardent leur bordure (accessibilité), tableaux leur filet |
| Transitions interruptibles, propriétés nommées, ≤ 150 ms | `background-color, color, box-shadow` 120 ms, `scale` 150 ms ease-out ; jamais `transition: all` |
| Pression à `scale(0.96)` | Boutons d'action (Save, Load, ASSIGN, LOCK REC, ⇄, bouton de machine, transport du navigateur). Pas sur les pas, segments, onglets ni knobs : interactions fréquentes |
| Sorties plus douces que les entrées | Popovers : entrée 150 ms (opacité + 4 px), sortie instantanée |
| Mouvement réduit | `prefers-reduced-motion` coupe animations et transitions |
| Lissage macOS à la racine | `-webkit-font-smoothing: antialiased` sur `:root` seulement |
| Chiffres tabulaires | Déjà dans v6, gardés sur tout le document |
| `text-wrap` | `balance` sur le titre du prompt, `pretty` sur son texte |
| Alignement optique | Triangle ▶ des onglets décalé de 2 px à droite |
| Icônes : un SVG, `currentColor`, trait 1,5 px près d'un texte 400-500 | Icônes de la barre du haut passées de 1,6 à 1,5 ; chevrons et ⇄ en 1,5 ; contour par défaut |
| Focus | `:focus-visible` : anneau teal de 2 px ; les knobs prennent le focus clavier |
| Ne pas compter sur le mouvement seul | Chaque état animé a un repère fixe (couleur, contour, numéro) |

## make-interfaces-feel-better : écarté ou adapté

- **Cibles de 40x40 en desktop dense** (44 au toucher) : non. Une page piste de plug-in à 1600x1000 ne tient pas
  à 40 px par cible ; la consigne du projet est 24 px, tenue partout, zone de clic élargie quand le dessin est plus
  petit, sans chevauchement.
- **Entrées échelonnées** (100 ms par bloc, mots à 80 ms), **échange d'icônes** (scale 0,25 → 1 avec flou),
  **`AnimatePresence initial={false}`**, **`will-change`** : propres au web et à React, ou à des entrées rares ; un
  instrument s'ouvre et se manipule sans mise en scène.
- **Contour des images** (1 px à 10 %) : aucune image dans l'interface.
- **Remplacer les bordures décoratives par des ombres sur les cartes** : les boîtes n'ont ni bordure ni ombre au
  repos depuis v6 (pas de ton) ; l'ombre est réservée à ce qui flotte.
- **Police système** : Barlow Semi Condensed gardée (choix approuvé, gratuite, chiffres tabulaires).

## Questions ouvertes

1. **Largeur du flux** : quand les deux drives et la BAND sont du même côté, la rangée a quatre ou cinq boîtes ;
   Machine et Filter se resserrent (minimums 400 px). À voir si l'utilisateur préfère empiler drive et BAND en une
   colonne.
2. **Drive actif** : une boîte de 200 px pour deux knobs laisse du vide en hauteur ; une rangée plus basse ou des
   knobs de réglages d'origine (si un type en a plus) le rempliraient.
3. **Sample layer replié** : ouvert, il passe par-dessus Machine et Drive B. Au jalon DT2, faut-il qu'il prenne une
   place dans le flux (avant les drives pré) ?
4. **Origin dans la liste du navigateur** répète l'arbre quand un dossier précis est choisi ; utile seulement sur
   les nœuds agrégés (Samples, By type). La masquer selon le nœud ?
5. **Drop sur un emplacement de kit** : codé, mais l'emplacement n'est pas visible depuis le navigateur ; faut-il
   un tiroir de kit dans le navigateur ?
6. **Overdrive master** : On/Bypass est un segment dans la boîte, celui du compresseur un onglet d'en-tête ;
   harmoniser.
7. Rien ne dit plus, hors infobulle, qu'une piste est fidèle à son firmware : si l'utilisateur veut un repère
   permanent, une pastille sur l'onglet de piste plutôt qu'une phrase.
8. Le skill plugin-gui-design demande aussi `v7-ui-spec.md` et `v7-style-guide.md` et un `index.html` ; le dossier
   suit la convention `vN-notes.md` des versions précédentes, donc pas faits.
