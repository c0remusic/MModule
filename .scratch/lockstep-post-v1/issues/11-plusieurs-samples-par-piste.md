# Plusieurs samples par piste

Type: grilling
Status: open
Assignee: 
Blocked by: 05

## Question

Le DT2 verrouille le sample par pas (SAMP, 1 016 emplacements par projet) ; Lockstep tient un sample par piste
(`Engine::setSample`), et sa spec v1 laissait ouverte la question « combien de samples une piste lit ». Le lecteur DT2
du Sampler (« Place du Digitakt II dans Lockstep ») doit-il changer de sample au trig ? Si oui : combien de samples une
piste tient chargés (ceux que ses patterns nomment ? une banque par Kit ?), où ils vivent dans l'état et le Kit, comment
un changement au trig se fait sans allocation sur le fil audio, ce que coûte la mémoire à 16 instances, et si les LFO
peuvent viser le choix du sample.
