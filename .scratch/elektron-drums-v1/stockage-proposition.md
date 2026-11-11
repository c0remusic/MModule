# Extension de stockage : proposition

Demande de l'utilisateur, 2026-11-11 : « Pour elektron content et uw bank il faut une extension un peu comme "+drive"
qui contrôle tous les samples et les banques. » Ticket 27 (décisions du 2026-11-11) et ticket 20 (état « machines ROM
retirées », dernier « Pas fait »). Proposition, rien n'est encore codé.

## 1. Ce que c'est pour l'utilisateur

Un seul endroit, dans l'instrument, qui tient tout ce qu'il peut jouer comme sample : les dossiers de l'utilisateur,
les packs et projets qu'il y a importés, la banque d'usine lue dans sa propre flash image, et les samples qu'un set ou
un Kit apporte d'ailleurs. On y voit chaque source, ce qu'elle contient et ce qu'elle occupe ; on y ajoute, retire,
renomme, trie par type, écoute et charge sur une piste. Il remplace les morceaux éparpillés d'aujourd'hui : le bouton
IMPORT MY ELEKTRON CONTENT, le bloc ELEKTRON CONTENT du Menu (UW BANK, FACTORY PACKS, TRANSFERS) et le segment FACTORY
de Browse. Comme le disque de samples intégré d'une machine, il fait partie de l'instrument et non d'un menu
d'import.

Trois noms, aucun de marque :

- **LOCKER** : le casier où l'instrument range ses sons ; même racine que Lockstep, court sur un écran.
- **CRATE** : le bac à disques qu'on fouille ; dit « samples à fouiller » plus que « disque ».
- **VAULT** : la réserve ; dit la garde et la durée, un peu plus froid.

Recommandation : LOCKER. Le reste du texte l'emploie.

## 2. Ce qu'il tient, et comment

Le LOCKER est une liste de **sources**. Chaque source a un genre, un nom montré (renommable), et ses samples.

| Genre | Ce que c'est | Sur le disque | Aujourd'hui |
|---|---|---|---|
| LINKED | un dossier de l'utilisateur, lu où il est | rien de copié ; le chemin dans la liste des sources | `Samples/folders.txt`, `SampleLibrary` |
| IMPORTED | un pack ou un dossier de projet importé | ses WAV, AIFF, FLAC **copiés** dans `Locker/Imported/<nom>/` | n'existe pas : l'import ajoute le dossier par référence |
| FACTORY | la banque d'usine de la flash image de l'utilisateur | les seuls mots échantillonnés, déjà extraits ; jamais l'image, jamais son code | `Elektron/Machinedrum.words`, `ElektronImport`, `m_factorySamples` |
| IN THIS SET | les samples qu'un set ou un Kit porte et que le LOCKER n'a pas, par empreinte | rien tant que l'utilisateur ne les garde pas ; KEEP les copie dans `Locker/Imported/From sets/` | n'existe pas : la copie vit seulement dans l'état |

Index : un fichier `Locker/sources.txt` remplace `folders.txt` (une ligne par source : genre, chemin, nom montré), relu
avant chaque écriture et suivi par les autres instances comme `folders.txt` l'est aujourd'hui. `types.txt` reste tel
quel, par empreinte. Un nom de sample renommé se garde de la même façon (`names.txt`, par empreinte) : aucun fichier
n'est renommé sur le disque. Le dossier `Locker/` naît dans le dossier de données de Lockstep ; la migration de
l'ancien dossier (ticket 27) y déplace `Samples/` et `Elektron/Machinedrum.words`, l'ancien chemin restant lu en
attendant.

La source FACTORY est unique : la banque de 32 emplacements, nommés FACTORY 01 à 32 faute de noms dans la banque. Elle
donne aussi les formes d'onde des machines (TRX, P-I, E12) : sa fiche le dit, et la retirer rend ces machines à leurs
données maison au chargement suivant. Elle naît quand une source ajoutée contient une flash image reconnue ; le walk
actuel d'`ElektronImport` fait déjà ce travail.

Ce qu'un set ou un Kit garde : pour un sample de l'utilisateur, son empreinte, son nom et sa copie (spec 8 : « copie
des samples utilisés », pour qu'un set ouvert ailleurs sonne) ; pour un sample d'usine, la seule référence
`factory="n"`, jamais d'audio, comme aujourd'hui. Les formats propres aux machines d'origine (projets, sons, SysEx)
restent comptés et non lus : hors de ce chantier.

## 3. Son écran

Il n'y a que sept touches de mode, fixes sur la plaque : pas de page à lui. Le LOCKER prend la place de SAMPLES, BY
SOURCE dans l'arbre LIBRARY de Browse (même rang, même vert), sur les trois colonnes de Browse (6, 10, 8). Les segments
de la liste deviennent les sources ; la colonne de droite montre la fiche de la source choisie quand aucun sample ne
l'est, celle du sample sinon. SAMPLES, BY TYPE reste l'autre vue des mêmes samples, sources FACTORY comprises.

```
 x16          LIBRARY (6)        | LOCKER (10)                            | SOURCE (8)
 y23  ───────────────────────────┼────────────────────────────────────────┼────────────────────────────
      KITS                       | SEARCH ______________                  | DRUMS 2024        [LINKED]
      TRACK SOUNDS               | ALL | DRUMS 2024 | PACK A | FACTORY | +|  D:\Samples\Drums 2024
    ▌ LOCKER                     |────────────────────────────────────────|  SAMPLES      412
      MACHINES                   | KICK  BD Deep 01.wav         0.42 s    |  SPACE        LINKED, 96 MB
      SAMPLES, BY TYPE           | SNARE SD Tight.wav           0.31 s    |  TYPES        KICK 88  SNARE 64 ...
                                 | HAT   CH 808ish.aif          0.12 s    |
                                 | ...                                    |  RENAME
                                 |                                        |  REMOVE SOURCE
                                 |                                        |
      LOCKER  1.9 MB IMPORTED,   |                                        |
      3 SOURCES                  |                                        |
      ADD FOLDER                 |                                        |
      IMPORT PACK                |                                        |
 y222 CLICK: LISTEN ...          |                                        | LOAD ON TRACK 3
```

Segments : ALL, puis une source chacune, FACTORY après les dossiers, IN THIS SET seulement quand le set en porte ; `+`
au bout ouvre le même choix de dossier que ADD FOLDER. Ce que fait l'utilisateur, souris seule, sans clic droit ni
modificateur :

- **Ajouter** : ADD FOLDER (lien) ou IMPORT PACK (copie) ouvrent le choix de dossier ; un dossier glissé sur Browse
  fait ADD FOLDER. Une flash image trouvée dans l'un ou l'autre crée la source FACTORY ; un toast dit ce qui a été
  trouvé (samples, déjà connus, image, fichiers non lus), comme le rapport d'aujourd'hui.
- **Retirer** : REMOVE SOURCE dans la fiche. Une source LINKED ne quitte que la liste. Une source IMPORTED ou FACTORY
  efface sa copie : deuxième clic de confirmation, comme DELETE de Kits, et la fiche dit ce qui sera effacé.
- **Voir la place** : par source, SPACE (copié, ou LINKED avec la taille lue) ; en bas de LIBRARY, le total copié.
- **Renommer** : RENAME dans la fiche, champ de texte en place (celui de SEARCH existe déjà en JUCE) ; sur une source
  ou un sample.
- **Typer** : la ligne TYPE de la fiche d'un sample ouvre les huit types, comme aujourd'hui ; GUESSED tant que
  l'utilisateur n'a pas choisi.
- **Écouter, charger** : inchangés, spec 8 : clic écoute, double clic charge sur la piste choisie, glisser sur une des
  16 pistes ; LOAD ON TRACK en bas de la fiche.
- **Garder** : sous IN THIS SET, KEEP IN LOCKER copie le sample du set dans le LOCKER.

## 4. Ce qui change dans le code

Gardé et étendu : `SampleLibrary` (devient la liste des sources : genre, nom montré, taille ; `addFolder` et
`removeFolder` deviennent `addSource` et `removeSource`), son scan sur un fil, les empreintes, les types, le walk
d'`ElektronImport::run` (sert ADD et IMPORT, et copie pour IMPORT), `Library(ImageWords)`, `factorySample`, les
références `factory:n` et `takeRoms`.

Retiré :

- Menu : le bloc ELEKTRON CONTENT, ses trois cellules et IMPORT MY CONTENT (`drawMenu`, le `Hit` et le `watch` du
  menu, `Editor::elektronContent`). Le Menu garde WINDOW et AUDIO.
- Browse : le bouton IMPORT MY ELEKTRON CONTENT en y 222, remplacé par ADD FOLDER et IMPORT PACK ; le segment FACTORY à
  part (`Editor::factorySegment`, la branche de `browseList` et `browseEntry` qui ajoute `m_factorySamples` après les
  entrées) : FACTORY devient une source comme les autres.
- Le rapport du Menu (`Processor::lastImport`, `imports`) : le toast et la fiche le remplacent.
- Les textes montrés qui nomment la machine d'origine (« The factory samples need the Machinedrum's flash image »,
  la ligne FOLDER à FACTORY, « IMPORT MY ELEKTRON CONTENT ») : « FACTORY BANK NOT IN THE LOCKER », règle du ticket 27.
- `Processor::importElektron`, renommé avec la classe (`ElektronImport` → nom maison, par exemple `LockerImport`).

Fait quand (tests, tous dans la gate) :

- `edDrumsSampleLibraryTest` : une source LINKED ajoutée puis retirée laisse ses fichiers intacts ; IMPORTED copie ses
  WAV, AIFF, FLAC dans `Locker/Imported/<nom>/`, en compte la place, et son retrait efface la copie seule ; un nom
  montré et un type choisis survivent à un rescan et à une deuxième instance ; un dossier avec une flash image
  (variable `GEARMULATOR_MD_FIRMWARE_BIN`) crée FACTORY, 32 emplacements, et rien de l'image n'est écrit hors des mots ;
  l'ancien `folders.txt` et l'ancien fichier de mots sont repris.
- `edDrumsProcessorTest` : un état et un Kit écrits avec un sample d'usine ne contiennent que `factory="n"` ; un état
  dont le sample n'est dans aucune source le montre sous IN THIS SET, et KEEP le rend source IMPORTED.
- `edDrumsNoFlashTest` : sans source FACTORY, une piste sur l'emplacement 5 se tait, garde sa référence, et l'éditeur le
  dit sans nom de marque.
- `edDrumsEditorTest`, à la souris : LOCKER dans l'arbre, une source choisie par son segment, ADD FOLDER, RENAME,
  REMOVE SOURCE en deux clics, LOAD ON TRACK ; le Menu n'a plus de bloc ELEKTRON CONTENT ; aucun texte dessiné ne
  contient « ELEKTRON », « UW » ni « MACHINEDRUM » (le test de noms du ticket 27 l'étend à Browse et au Menu).

## 5. Questions pour l'utilisateur

1. **Le nom** : LOCKER, CRATE ou VAULT ? Recommandé : LOCKER.
2. **Importer copie-t-il ?** Deux gestes, ADD FOLDER (lien, rien de copié) et IMPORT PACK (copie dans le dossier de
   données, survit au disque débranché), ou un seul geste qui lie toujours ? Recommandé : les deux ; le lien reste le
   geste par défaut, celui du glisser.
3. **Un set garde-t-il la copie des samples de l'utilisateur, ou la seule référence ?** Recommandé : la copie et
   l'empreinte, comme aujourd'hui et comme la spec 8 le dit ; seule la banque d'usine n'est qu'une référence. Une
   référence seule ferait taire un set ouvert sur un autre ordinateur.
4. **Le Menu garde-t-il un résumé du LOCKER** (SOURCES, SPACE, et un bouton qui ouvre Browse dessus) **ou rien ?**
   Recommandé : rien ; tout est dans Browse, le Menu reste WINDOW et AUDIO.
