# Format des kits et de l'état

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 

## Question

Que garde un kit, et que garde l'état du plug-in dans le projet de l'hôte ? Un kit : machine et famille de chaque
piste, chaîne (emplacements et réglages), LFO, envois, routage, chokes, effets et master ? Les patterns du séquenceur
interne vont-ils dans le kit, à part (comme les patterns Elektron), ou dans l'état seulement ? Banque partagée entre
instances (comme les Kits de MoDule : 64 emplacements, relus avant écriture), format de fichier, versionnage, import
d'un kit d'une autre version ; ce que l'état garde de plus (kit joué, emplacement, réglages globaux comme le
suréchantillonnage).

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q14). La question de la bibliothèque d'échantillons, dans le
flou de la carte, est réglée ici aussi.

- Un kit garde tout le son : par piste machine et famille, paramètres, chaîne, ampli, LFO et cibles, envois, sortie,
  chokes ; plus les effets d'envoi (réglages et sources) et le master. Rien du séquenceur.
- Patterns séparés, comme sur le MD : trigs et locks, plus un numéro de kit à charger au démarrage du pattern ou
  « garder le kit en cours » (par défaut).
- Banque de 64 kits dans le dossier de données, partagée entre instances, relue avant chaque écriture (comme MoDule) ;
  les 128 patterns vivent dans le projet de l'hôte, avec export et import en fichier.
- L'état garde une copie complète du kit joué, son emplacement, les 128 patterns, les réglages globaux
  (suréchantillonnage, mode 96 kHz) et une copie des samples utilisés.
- Format versionné et compressé, valeurs rangées par identifiant de paramètre figé ; un paramètre inconnu d'un kit
  ancien prend son défaut.
- Sons de piste (machine, chaîne, ampli, LFO) sauvés et rechargés sur n'importe quelle piste, en bibliothèque partagée.
- Banque de départ maison : ~16 kits et quelques dizaines de sons ; rien d'Elektron n'est livré.
- Samples : bibliothèque de l'utilisateur (WAV, AIFF, FLAC, fichiers ou dossiers), dans le dossier de données,
  partagée. Le contenu Elektron du propriétaire (banque UW du MD lue dans sa flash image, packs d'usine DT2 et Rytm
  téléchargés par lui, exports Transfer) s'importe et se lit chez lui, jamais livré ; sans lui, les machines qui en ont
  besoin jouent nos samples maison (petit jeu fait par nous ou CC0) ou le silence. Un kit référence ses samples par
  empreinte et nom ; le projet en garde une copie.
- Un dossier « Elektron » dans le dossier de données et un bouton « Importer mon contenu Elektron » (ou un dossier
  glissé sur la fenêtre) : chaque fichier reconnu par empreinte et format, firmwares pour les tests seulement, banque
  UW, samples rangés par machine d'origine ; l'import dit ce qu'il a trouvé et ce qui manque.
- Explorateur (onglet Sound Browser) en trois colonnes : arbre (Kits, Sons de piste, Samples : Maison, Mes samples,
  Elektron par machine) avec une bascule « Par source » / « Par type », liste (nom, type, tags, durée, origine,
  recherche et filtres), aperçu. Clic = écoute, double-clic = charge, glisser sur un onglet de piste, la box SAMPLE ou
  un emplacement de kit.
- Types de samples : Kick, Snare, Clap, Hat fermé, Hat ouvert, Cymbale, Tom, Perc, Chord, Tonal, FX, Boucle, Autre ;
  devinés à l'import par le nom du fichier, sinon par une analyse simple du son (durée, centre spectral, hauteur,
  attaque, sans IA), corrigeables à la main un par un ou en groupe ; le choix manuel prime. Sons et kits se rangent
  aussi par type.
