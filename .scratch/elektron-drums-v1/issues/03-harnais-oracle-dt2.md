# Digitakt II : harnais d'oracle qui tourne

Type: AFK
Status: resolved
Assignee: 
Blocked by: 01

## Quoi

Le compresseur et l'overdrive du master v1 se vérifient contre le firmware du DT2, mais sharcvm et digikit ne
tournent pas encore (compilateur C et chaîne Rust à installer ; coupled-run sans entrées). Faire tourner l'un des
deux sur la routine master `0x1c207b` avec une entrée audio fournie par le test et ses réglages lus, et capturer sa
sortie comme vecteurs de référence, hors du dépôt comme tout ce qui vient d'un firmware.

## Critère de fin

- Un sinus et un Hit de batterie traversent le master émulé ; la sortie capturée change avec le seuil du
  compresseur et le drive.
- La commande qui régénère les vecteurs est documentée.

## Références

Spécification §6, §14. Tickets « Effets master », « Cartographie du firmware du Digitakt II ».

## Answer

Fait le 2026-10-26, sans rien installer : le cœur SHARC+ concret en Python de digikit, depuis son `.venv` déjà là, fait
tourner l'init du graphe audio `0x1c15e3` une fois (instantané gardé hors du dépôt, avec la fiche de ce qui l'a produit),
puis le master `0x1c207b` bloc après bloc, l'entrée sur la piste 1 et les réglages écrits dans leurs mots flottants.
Aucune ColdFire nécessaire. Ratio 16:1 vérifié par la lecture de la table `0x2c9a18`.

- Sinus 220 Hz à −15 dB RMS, ratio 16:1 : −5,03 / −6,69 / −30,27 dB RMS en sortie aux seuils 0, −30, −60 dB.
- Hit : −2,49 / −9,42 / −29,98 dB RMS ; réduction de gain −36 dB au seuil −60 dB, établie en 3 blocs.
- Drive 0 / 0,5 / 1 sur le sinus : −5,03 / −2,29 / −0,62 dB RMS. Compresseur et drive ensemble : même réduction de gain
  que sans drive, le drive vient après le compresseur.
- Vitesse : 33-41 échantillons par seconde, environ 1 400 fois plus lent que le temps réel ; dix cas en 13 min.
- Commande : `pwsh -NoProfile -File C:\dev\elektron-work\tools\dt2\regen.ps1` ; scripts versionnés dans
  `../tools/dt2/`.

Détails, format des vecteurs et inconnues (graine `RECIPS`, rôle exact de cinq réglages, bus de contournement et
sources de sidechain non exercés) : [research/03-oracle-dt2.md](../research/03-oracle-dt2.md).
