# Installer une Machine utilisateur par le Menu

Type: task
Status:
Blocked by: 

## Question

L'entrée « Install User Machine… » du Menu, comme l'utilisateur l'a décidé le 2026-10-17 : elle ouvre un sélecteur de
fichier (`.syx` ou `.mds`), installe le paquet au premier numéro libre de la Library (refusé si les 16 sont pris), et
la ligne d'état dit le numéro pris ou l'erreur ; l'instance prend sa copie. Choisir un USR dans le navigateur prend la
copie de la Library (choix explicite), même si l'instance en a une autre. Tests : l'installation par le Processor au
premier numéro libre, le refus quand tout est pris, un fichier qui n'est pas une Machine utilisateur refusé avec son
message, et le choix d'un USR qui remplace la copie de l'instance.
