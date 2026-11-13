# Mesures d'acceptation de la v1

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 06, 07, 09, 16, 18, 21, 23, 24

## Quoi

Passer les douze critères de la spécification §12 sur le plug-in entier, sur machine calme : oracles et tolérances,
« sonne pareil », Q5-Q7, Q9, Q12-Q16, pire cas CPU à 44,1, 48, 88,2 et 96 kHz, gigue et latence, critères
fonctionnels, hébergement VST3, aucun firmware nécessaire, garde-fou vert sans saut. Chaque mesure manquante entre
au garde-fou.

## Critère de fin

- Un rapport par critère, avec ses chiffres ; tout vert.

## Références

Spécification §11, §12.

## Noté par le ticket 12 (2026-11-06)

Refaire au calme le tableau de coût du ticket 12. Le pire cas est le kit alterné (+3,67 à 2x, +5,98 à 4x) : le guide de 2 % pour la chaîne
est à revoir ou à tenir, avec la table d'emplacements écartée. Revérifier le décalage MD de -2 sur toutes les machines.

## État (2026-11-13)

Mesuré sur `46ac971b` plus les tests de ce ticket, tests lancés un par un. **Machine pas calme** : Chrome (~1,3 cœur),
d'autres sessions Claude (jusqu'à 1,7 cœur) et Live (0,3) pendant les mesures ; les coûts ci-dessous en portent la
marque, à refaire sur machine vraiment calme avant de conclure le critère 4. Garde-fou du dépôt non lancé (consigne) :
le dernier vert est celui de `00e4ca7a`, 72 sur 72, le 2026-11-13 à 07:02.

| # | Critère | Mesure | Résultat | État |
|---|---|---|---|---|
| 1 | Q1 exact ; Q2-Q4 à tolérance, toutes fréquences | `mdVoicePortTest`, `mdEngineFirmwareTest`, `mdFastTrackFxTest`, `mdFastSendsTest`, `stFast*Test`, `edDrumsEngineTest` (Q1 dans la chaîne), `edDrumsChainTest` (Q3), `edDrumsRateTest` (Q15 contre la fréquence de la famille) | Q1 : 10 machines MD dans leur chaîne, 0 LSB ; Q3 au pire SNR 85,0 dB, fenêtre 69,0 (seuils 70/60), tenues 58,7 (borne 58) ; aux autres fréquences, au pire 0,002 dB d'enveloppe, 0,006 dB par tiers d'octave | vert |
| 2 | « Sonne pareil » à 44,1 et 48 kHz | aucune mesure automatique : critère de l'utilisateur | brouillon d'usine prêt à l'écoute : `C:\dev\elektron\elektron-work\factory-draft\` (kits, patterns, un WAV par kit) | en attente de l'utilisateur |
| 3 | Q5-Q7, Q9, Q11-Q16 | `edDrumsChainTest`, `edDrumsEngineTest`, `edDrumsLfoTest`, `stFastResampleTest`, `mdFastTrackFxTest`, `stFastSyBitsTest`, `edDrumsRateTest`, les tests d'envoi | Q5 -inf dBFS ; Q6 drive Syntakt -67,0 à 2x, -83,3 à 4x (DIST -49,6 et -61,0, écarté par l'utilisateur au ticket 12) ; Q7 0,0000 dB, THD+N -121,2 au pire ; Q9 0,000 dB, aucune valeur non finie ; Q11 +0,0004 dB, THD+N -91,4, rejet -70,3 dB ; Q12 -50,7 au pire (chaîne), -51,8 (LFO), -52,6 (Choke) ; Q13 SRR du MD au Q2, bit reducer de SY BITS au Q3 seulement ; Q14 tenu (moteur, envois, TrackFx) ; Q15 0,002 dB, Syntakt à 44,1 sous ses bornes par machine ; **Q16 nouveau** : 16 BD en phase, Main +19,6 dBFS (BD MODERN) et +27,0 dBFS (TRX-BD), à -134 dB de 16 Hits seuls, donc aucun écrêtage caché, tout fini, le master sort à 1,000 | vert |
| 4 | Pire cas sous 15 % à 44,1, 48, 88,2, 96 kHz | `edDrumsRateTest` (étendu : machine la plus chère de toutes les familles hors Syntakt, sur 7 et sur 16 pistes, exigé aux quatre fréquences) ; `edDrumsCost` à la main | drives 2x, trois passes : 44,1 de 11,57 à **15,59** (une passe sur trois au-dessus), 48 de 12,32 à 13,92, 88,2 de 13,06 à 14,52, 96 de 13,69 à 14,49. La plus chère est SOPHIE, pas une machine MD. Drives 4x, kit alterné MD/Syntakt (SOPHIE non mesurée) : 14,97 et 15,67 | **rouge ou à la limite** |
| 5 | Gigue 0, latence publiée | `edDrumsEngineTest`, `edDrumsRateTest`, `edDrumsAnalogTest`, `edDrumsSequencerTest`, `edDrumsVst3HostTest` | gigue 0 partout ; `HitDelay` 81 (1,84 ms) à 44,1, 117 à 48, 213 à 88,2, 233 à 96, rapporté à l'hôte | vert |
| 6 | Séquenceur | `edDrumsSequencerTest`, `edDrumsProcessorTest` | 54 Hits sur 54 à l'échantillon ; pattern du critère 6 rendu deux fois et en blocs de 37 et 512, mêmes échantillons ; changement à la fin du pattern ; MIDI relu par JUCE | vert |
| 7 | Kits et état | `edDrumsProcessorTest`, `edDrumsSampleLibraryTest` | kit et état restitués : mêmes échantillons ; deux instances gardent les emplacements l'une de l'autre ; identifiant manquant à son défaut ; l'import compte ce qu'il trouve et refuse. « Ce qui manque » n'est pas vérifié par un test | vert, sauf ce dernier point |
| 8 | Paramètres et sorties | `edDrumsProcessorTest`, `edDrumsVst3HostTest` | 1530 paramètres réglés et relus, IDs figés, relais, chaque piste sur sa sortie | vert |
| 9 | Interface | `edDrumsEditorTest` | 1530 paramètres sur 1594 ont un contrôle (moins les 64 relais), gestes du ticket 23 relus ; la conformité bloc par bloc à la maquette n'est pas automatisée | vert pour les contrôles, regard de l'utilisateur en attente |
| 10 | Hébergement VST3 | `edDrumsVst3HostTest` (**hors ligne ajouté**) | blocs de 128, 32, aléatoires et hors ligne : écart 0 ; pire bloc 0,356 ms | vert |
| 11 | Sans firmware | lancement sans aucune image ni variable (LOCALAPPDATA et USERPROFILE vides) | les 11 tests qui n'en demandent pas passent, les 9 qui en demandent sortent en 77 ; `edDrumsNoFlashTest` : 32 machines entières, 10 sur données maison, 17 muettes et signalées, gigue 0 | vert |
| 12 | Garde-fou vert sans saut | `scripts/windows/gate.ps1` | trou trouvé : trois tests sautent une partie sans leurs données et passent (`edDrumsSyntaktSlotsTest`, `edDrumsSampleLibraryTest`, `mdX20OsTest`). **Fermé** : donnée manquante = rouge avant la construction | vert au dernier passage, à reprendre avec ce changement |

**Critère 4, cause.** Pas une petite correction : la chaîne à 4x (+6,9 points sur le kit alterné, les deux chaînes
calculant les quatre groupes, la table d'emplacements écartée au ticket 12) et SOPHIE, la voix la plus chère, que
`edDrumsCost` ne connaît pas. Décision à prendre : 4x dans le budget ou non (comme 192 kHz, « marche sans être
budgété »), et la marge de 2x à 44,1 kHz à remesurer au calme. Le test exige 2x seulement, la borne de la spécification.

**Tableau du ticket 12 refait** (`edDrumsCost`, 5 passes de 5 s, minimum ; deux séries, machine chargée) :

| Kit | Nu | Famille | Le plus cher 2x | Le plus cher 4x |
|---|---|---|---|---|
| Mixte | 8,22 | 8,36 à 9,50 | 9,42 à 10,95 (+1,20 à +2,73) | 12,50 à 12,71 (+4,28 à +4,49) |
| 16 MD | 8,62 à 9,42 | 10,03 à 10,54 | 11,17 à 11,58 (+2,16 à +2,55) | |
| Syntakt | 4,78 à 4,85 | 5,42 à 5,46 | 6,67 à 6,83 (+1,89 à +1,98) | |
| Alterné | 8,03 à 8,14 | 10,13 à 10,15 | 12,25 à 12,33 (+4,11 à +4,30) | 14,97 à 15,67 (+6,94 à +7,53) |

Le guide de 2 % pour la chaîne ne tient pas : seul le kit Syntakt y reste.

**Décalage MD de -2.** Revérifié sur les 82 machines MD qui sonnent (`edDrumsEngineTest`, désormais exigé) : le
début à 1 % du pic recule de -2 sur 48 machines (52 à 4x), -1 sur 27 (24), 0 ou +2 sur les autres ; mais le retard qui
aligne le Hit relevé sur le Hit nu vaut 0 sur toutes, sauf deux machines bruitées (38 et 72, corrélation 0,35 et 0,62,
sans signification). Le -2 est la sonnerie des demi-bandes avant le front, pas un déplacement du Hit : rien à compenser.

Ajouts : `edDrumsEngineTest` (Q16, retard d'un drive relevé sur chaque machine MD), `edDrumsRateTest` (pire cas sur
toutes les familles, aux quatre fréquences), `edDrumsVst3HostTest` (hors ligne), `scripts/windows/gate.ps1` (données
manquantes). Ticket laissé ouvert : critère 4, critères 2 et 9 de l'utilisateur.
