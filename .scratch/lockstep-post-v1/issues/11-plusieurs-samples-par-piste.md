# Plusieurs samples par piste

Type: grilling
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 05

## Question

Le DT2 verrouille le sample par pas (SAMP, 1 016 emplacements par projet) ; Lockstep tient un sample par piste
(`Engine::setSample`), et sa spec v1 laissait ouverte la question « combien de samples une piste lit ». Le lecteur DT2
du Sampler (« Place du Digitakt II dans Lockstep ») doit-il changer de sample au trig ? Si oui : combien de samples une
piste tient chargés (ceux que ses patterns nomment ? une banque par Kit ?), où ils vivent dans l'état et le Kit, comment
un changement au trig se fait sans allocation sur le fil audio, ce que coûte la mémoire à 16 instances, et si les LFO
peuvent viser le choix du sample.

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling).

- Oui, le sample change au trig, par un **lock de sample**, sur les trois lecteurs du Sampler (12-BIT, VINTAGE, DT2)
  et sur la couche sample.
- Pas de réserve de samples à gérer : pas de banque par Kit, pas de numéros, pas de limite de 128 ; c'est un geste de
  machine, pas de plug-in (l'utilisateur). La piste garde son propre sample, chargé comme aujourd'hui (glisser depuis le
  Locker, double-clic).
- Un lock de sample nomme directement un sample du Locker par son empreinte. Il se pose en glissant un sample sur un
  pas, ou depuis le menu de lock du pas.
- Lockstep charge seul, hors du fil audio, les samples que les patterns d'une piste nomment, partagés par empreinte
  dans le processus : la mémoire est celle de ce que les patterns nomment, chargé une fois. Le fil audio ne fait que
  passer d'un sample déjà chargé à un autre, sans allocation. Un set existant n'a aucun lock de sample : rien ne migre.
- Seuls les locks changent le sample, à leur trig. Le choix du sample n'est ni une cible de LFO ni un paramètre hôte :
  il n'a ni numéro ni ordre à parcourir.
- Sample absent du Locker (autre PC, sample effacé) : le pas joue le sample propre de la piste, pas de silence ; le
  lock reste dans le pattern et rejoue son sample dès que la même empreinte revient ; la ligne d'état dit « N samples
  manquants », avec leurs noms.
- Deux cibles de lock distinctes sur une piste Sampler : SAMP machine (le sample du lecteur) et SAMP couche (le sample
  de la couche sample). Le glisser sur un pas vise la cible de la page affichée ; le menu de lock du pas propose les
  deux.
- Affichage : dans les lanes de Seq, un pas locké montre le nom court du sample (8 caractères) en couleur de lock ; le
  menu de lock d'un pas montre le Locker, avec recherche et écoute au survol comme le navigateur actuel.
