# Références DSP utiles (tirées d'awesome-musicdsp, 2026-10-24)

Source : https://github.com/olilarkin/awesome-musicdsp (liste ; licences des entrées à vérifier une par une avant
tout emprunt de code).

- Machines analogiques (ticket 14) : DaisySP (MIT, https://electro-smith.github.io/DaisySP/) et Mutable Instruments
  (https://github.com/pichenettes/eurorack) ont des modèles de grosse caisse, caisse claire et charleston analogiques
  [INFER : Plaits et Peaks ; licence MIT à confirmer dans le dépôt]. Point de départ pour les modèles réglés à
  l'oreille.
- Filtres en flottant, indépendants de la fréquence (tickets 02, 16) : papiers techniques d'Andy Simper (Cytomic,
  SVF/TPT), « The Art of VA Filter Design » de Vadim Zavalishin (NI), RBJ Audio EQ Cookbook pour l'EQ 3 bandes.
- Suréchantillonnage des drives (ticket 01 : +8,6 points pour un 2x naïf) : HIIR de Laurent de Soras, demi-bande IIR
  polyphasée à faible latence, optimisée SIMD, WTFPL.
- Effets maison (chorus, phaser, flanger, reverb) : Signalsmith DSP et son article « Let's write a reverb »,
  conférence de Sean Costello (Valhalla) sur la reverb.
- SIMD : MadronaLib (Randy Jones), bibliothèque pensée pour le SIMD.
- Absent de la liste : filtres d'onde numériques (WDF), modèles de circuits de batterie, conception indépendante de la
  fréquence d'échantillonnage.
