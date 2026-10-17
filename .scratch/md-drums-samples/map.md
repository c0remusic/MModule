# Map : Samples de l'utilisateur dans MD Drums

Label: wayfinder:map

## Destination

Un fichier audio lâché sur la fenêtre de MD Drums, depuis le navigateur de Live ou l'Explorer, joue sur une Machine
ROM. Fini quand : garde-fou vert avec les nouveaux tests, poussé sur `main`, joué par l'utilisateur dans Live sous
Windows (macOS construit par la CI, pas vérifié à la main). Pas de tag.

## Notes

- **Exécution dans la carte** (décidée au cadrage du 2026-10-16, comme la carte de l'éditeur) : les tickets `task`
  construisent. Un ticket par session, sauf la recherche.
- Domaine : `CONTEXT.md`. **Sample** : l'audio que joue une des 48 Machines ROM (ROM-01 à ROM-48), nommé par sa
  Machine (« le Sample de ROM-33 ») ; jamais « slot », qui est la place d'un Kit dans le Bank.
- Skills : `grilling` + `domain-modeling` par défaut ; `prototype` pour le ticket du lâcher ; `codebase-design` pour
  un ticket de design ; `tdd` ou `implement` pour les tranches ; `research` pour la recherche.
- Garde-fou du `CLAUDE.md` avant chaque commit et push ; chaque tranche ajoute ses tests au garde-fou. Push sans
  demander une fois vert. Jamais de tag.
- **Latence** et **fluidité** comme sur la carte de l'éditeur : tout changement du chemin de rendu rapporte latence
  note→son et gigue avant et après ; un lâcher et ce qu'il affiche se mesurent événement→pixel ; jamais
  `Plugin::withDeviceLocked` par frame ou par clic.
- Jamais de ROM ni de Sample d'usine dans un dépôt, une release ou un test commité : le flash image reste celui de
  l'utilisateur.
- Recherches : un doc par ticket dans `research/` à côté de cette carte.
- Faits établis au cadrage :
  - la pile route déjà les fichiers lâchés vers l'élément RmlUi sous le curseur (`juceRmlUi/juceRmlDrag.*`, un
    `juceRmlUi::DragTarget` les prend ; le Patch Manager et Xenia s'en servent, MD Drums pas encore) ;
  - la banque d'usine entre dans le moteur une fois, à sa construction, depuis le flash image, à deux endroits : le
    moteur du Device et celui de la prévisualisation du Hit (`mdDrumsHitPreview.cpp`) ;
  - le DSP voix lit son répertoire à P:$147e00 et ses Samples à partir de P:$150000, deux codes de 12 bits par mot,
    jusqu'à $200000 (`mdEngine/tools/mdfw/Firmware.*`).
  - mesuré le 2026-10-17 pour la carte des Machines utilisateur
    ([research/04](../user-machines/research/04-voice-dsp-memory.md)) : les Machines E12 lisent `$1F85B5-$1FFFFF`,
    dans la place que cette carte comptait libre pour les Samples ; à reprendre par « Écrire un Sample dans le DSP
    voix en marche ».

## Decisions so far

<!-- une ligne par ticket résolu : [titre](issues/NN-slug.md) : gist -->

- [Ce que Live donne en glissant, et la place libre](issues/01-glisser-depuis-live.md) : le navigateur de Live et
  l'Explorer arrivent au plug-in comme un glisser de fichiers ; un clip, un pad de Simpler ou de Drum Rack : rien de
  documenté, échecs rapportés, à mesurer ; JUCE ne lit que WAV et AIFF sous Windows tel que construit ; 20,85 s libres
  à 44,1 kHz pour tous les Samples ensemble ; le vrai UW prend 4 à 48 kHz, mono, 12 bits, boucles d'au moins 151
  échantillons.
- [Quelles Machines reçoivent un Sample](issues/02-machines-qui-recoivent.md) : les 48 ; retirer remet celui d'usine
  ou rend muette.
- [Où vivent les Samples](issues/03-ou-vivent-les-samples.md) : bibliothèque partagée dans le dossier de données,
  l'état du set embarque ceux de son Kit, chaque instance sa copie.
- [Ce qu'un fichier devient](issues/04-conversion-d-un-fichier.md) : WAV, AIFF, FLAC ; mono par somme ; rate natif
  de 4 à 48 kHz, au-dessus 44,1 ; boucle `smpl` d'au moins 151 ; gain tel quel ; trop long refusé.
- [Écrire un Sample dans le DSP voix en marche](issues/05-ecrire-un-sample-dans-le-dsp.md) : par tranches de 4 096
  mots entre deux blocs (13 µs), l'entrée de répertoire en dernier ; le JIT ne gêne pas ; la place sûre s'arrête sous
  ce que lisent les E12 (19,4 s).
- [Un Sample joué par le moteur](issues/08-charger-un-sample-dans-le-moteur.md) : construit ; les Samples de
  l'utilisateur à la suite au-dessus de la banque d'usine, écrits 4 096 mots par bloc, l'entrée de répertoire en
  dernier.
- [Un fichier converti en Sample](issues/09-convertir-un-fichier.md) : construit ; WAV, AIFF et FLAC, mono par la
  moyenne des canaux, rate du fichier jusqu'à 48 kHz, boucle `smpl`, codes de 12 bits par la table de l'OS.
- [La bibliothèque des Samples et l'état du set](issues/10-bibliotheque-et-etat.md) : construit ; `ROM-NN.smp`
  partagés dans le dossier de données, copie par instance, l'état du set porte les Samples de son Kit.
- [Où lâcher](issues/06-ou-lacher.md) (prototype construit, jugement dans Live en attente) : sur le nom de la Machine et
  l'écran Hit ; la ROM de la Track, sinon la première ROM-33..48 libre ; la ligne d'état dit ce qui s'est passé.

## Not yet specified

- Ce que voit l'hôte quand un Sample change : set marqué modifié, undo de Live.
- Le nom d'un Sample dans le fichier `.kit` de md-drums-seq et sur Push.

## Out of scope

- RAM-R et RAM-P (enregistrer l'audio en direct).
- SDS vers ou depuis une vraie Machinedrum.
- Éditer un Sample (coupe, boucle, normalisation) au-delà de ce qu'un lâcher exige.
- La vérification à la main sous macOS.
