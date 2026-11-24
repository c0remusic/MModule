# L'écran Master sans superpositions

Type: AFK
Status: done (vue de l'utilisateur à venir)
Assignee: Claude (session 2026-11-24)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : plusieurs éléments se superposent dans l'écran Master.

Relevé sur un rendu aux valeurs les plus larges (`master-widest.png` du test de l'éditeur : sorties sur OUT 15,
compresseur au bout de ses plages) : les étiquettes de sortie des 16 pistes et des 4 retours (« OUT 15 ») se
chevauchaient, au pas de 13,5 et 9,5 mm. Le reste ne chevauche pas.

Correction : sur l'écran Master, les sorties s'écrivent MAIN et O1 à O15. Les noms de machine en tête de tranche,
coupés à 12,5 mm sur un pas de 13,5, se touchaient (« ANA SN ANA TO » se lisait d'un bloc) : coupés à 11 mm.

## Critère de fin

- [x] Rendu `master-widest.png` : aucune étiquette de sortie n'en touche une autre.
- [ ] Vu par l'utilisateur ; si d'autres superpositions restent, lesquelles (capture).

## Point ouvert

Dans la police Inter, « O15 » se lit presque « 015 ».
