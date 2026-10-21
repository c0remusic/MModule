# GND-SN-PRO, GND-PU, NFX-EV, NFX-CO et NFX-UC sur le moteur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by: 05

## Question

Comme GND-SW (« Premier son : GND-SW sur une Track ») : les cinq autres installées depuis l'OS X.20
(`EngineT::installX20Builtins` avec leurs bits), leurs mots par `x20::compute`, chauffées. GND-SN-PRO au numéro 1 à la
place de GND-SN quand l'OS X.20 est là (décision du cadrage). Les trois NFX entendent les Tracks que leur descripteur
nomme (research/01 : t-1 et t-2 par défaut, le menu « hears » tel quel), sur le délai de leur première source comme
les NEIGHBOUR ; NX-EV lit le tempo (`kX20Tempo24`, celui de l'hôte s'il arrive jusqu'au moteur). NFX-UC attend MACRI
(« MACRI dans l'émulateur DSP56300 »). Tests : chacune sonne ; les 105 de 1.63 inchangées ; un WAV par Machine pour
l'oreille de l'utilisateur ; ANTI-CLICK (X.20 adoucit GND-SN-PRO et GND-PU, pas les trois NFX : `HostModel::fades`).

## Answer

Construit le 2026-10-21 :

- `x20::kAll` installe les six (`EngineT::installX20Builtins`) ; GND-SN-PRO joue au numéro 1 à la place de GND-SN.
- `mdDrums::Engine::installX20` : les six, chauffés ; `isNeighbour` vrai pour les trois NFX (`x20::hearsTracks`), qui
  démarrent sans attendre un Hit et jouent sur le délai de leur première source, comme les NEIGHBOUR ;
  `machineDefaults` donne les défauts de la table d'X.20 ; `playsX20`.
- `mdX20BuiltinsTest` : crêtes GND-SN-PRO 0,250, GND-SW 0,240, GND-PU 0,250 seules sur la Track 1 ; NFX-EV 0,231,
  NFX-CO 0,240, NFX-UC 0,103 sur la Track 2, entendant GND-SW sur la Track 1 ; les 104 autres Machines audio de 1.63
  inchangées (0 bloc sur 420) ; `mdDrums::Engine` offre les six avec les défauts d'X.20. Un WAV par Machine pour
  l'oreille (`MD_X20_WAV`), envoyés à l'utilisateur.
- Pas encore : le tempo de l'hôte pour NFX-EV (125 BPM fixes, `kX20Tempo24`) ; le début de voix de chacune
  (`Engine::voiceStart`, mesuré pour 1.63 seul) ; les aperçus (`HitPreview`) ; tout ce qui est dans le plug-in
  (« Les 6 dans le plug-in »).
