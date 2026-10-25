# Digitakt II : harnais d'oracle qui tourne

Type: AFK
Status: ready-for-agent
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
