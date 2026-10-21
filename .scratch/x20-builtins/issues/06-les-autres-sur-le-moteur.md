# GND-SN-PRO, GND-PU, NFX-EV, NFX-CO et NFX-UC sur le moteur

Type: task
Status: open
Assignee:
Blocked by: 05

## Question

Comme GND-SW (« Premier son : GND-SW sur une Track ») : les cinq autres installées depuis l'OS X.20
(`EngineT::installX20Builtins` avec leurs bits), leurs mots par `x20::compute`, chauffées. GND-SN-PRO au numéro 1 à la
place de GND-SN quand l'OS X.20 est là (décision du cadrage). Les trois NFX entendent les Tracks que leur descripteur
nomme (research/01 : t-1 et t-2 par défaut, le menu « hears » tel quel), sur le délai de leur première source comme
les NEIGHBOUR ; NX-EV lit le tempo (`kX20Tempo24`, celui de l'hôte s'il arrive jusqu'au moteur). NFX-UC attend MACRI
(« MACRI dans l'émulateur DSP56300 »). Tests : chacune sonne ; les 105 de 1.63 inchangées ; un WAV par Machine pour
l'oreille de l'utilisateur ; ANTI-CLICK (X.20 adoucit GND-SN-PRO et GND-PU, pas les trois NFX : `HostModel::fades`).
