# OS du Model:Cycles

Type: task
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 

## Question

Ce PC n'a pas l'OS du Model:Cycles. Le télécharger depuis la page de téléchargements d'Elektron (dernière version),
avec l'accord de l'utilisateur, dans un dossier à lui sous `C:\dev\elektron\elektron-work` ; en extraire la section 3
(MAIN OS) comme celles du Syntakt et du Rytm, hors du dépôt. Résultat : la version, le chemin, le hachage de la
section 3, et que les adresses du ticket 11 de la planification v1 s'y retrouvent.

## Answer

Fait le 2026-11-16, sans téléchargement : l'OS était déjà sur ce PC (l'utilisateur), là où l'avait trouvé la
recherche du ticket 11 de la planification v1.

- **Version et source :** `model-cycles_OS1.13.syx` (dernière version), 890 016 octets, SHA-256
  `44fe5862…800640c`, dans `%APPDATA%\Elektron Emu\Model Cycles\Firmware\` ; le même hachage que digiemu
  (`devices/model-cycles.toml`).
- **Extraction :** digiemu `python -m emu.extract … -o`, lancé avec le venv d'`elektron-work` (Unicorn 2.1.4), sous
  garde de 300 s. Sections dans `C:\dev\elektron\elektron-work\model-cycles-1.13\`, hors du dépôt : section 3
  MAIN OS, 1 744 192 octets, chargée à `0x40000400`, SHA-256 `cc99d4f0…cb99ee98`, le hachage relevé au ticket 11 ;
  sections 2, 4 et 5 à côté, pas de section 7.
- **Adresses du ticket 11 retrouvées :** la table render `0x40118610` tient KICK `0x400aa3c4` à l'indice 0, TONE
  `0x400aa930` à 4, CHORD `0x400ab24c` à 5 ; la table update `0x40118628` six pointeurs dans le même bloc ; le
  répartiteur `0x400a7d4a` et les trois machines commencent par un prologue de fonction. Les indices 1-3 pointent
  vers `0x400ab6e8`, `0x400aa712`, `0x400aacc2`, dont l'ordre SNARE/METAL/PERC reste à confirmer par « État réel de
  l'OS du Model:Cycles et ordre de ses machines ».
