# Oracle dynamique de la voix sampler du Digitakt II

Type: task
Status: open
Assignee: 
Blocked by: 

## Question

La cartographie du DT2 (`../elektron-drums/issues/23-cartographie-dt2.md`) a laissé l'oracle dynamique pas prêt :
sharcvm ne tourne que pour le Digitone II, coupled-run sans entrées. Le harnais du master (v1, ticket 03 de
`elektron-drums-v1`) appelle déjà des routines du DT2. Rendre appelables, hors temps réel et sous garde de durée, la
voix `0x1c4ecf` et STRETCH `0x1c5576` avec un sample et des réglages donnés, et enregistrer des vecteurs dans
`elektron-work\vectors-dt2` ; mesurer leur coût en instructions par voix et par bloc. Résultat attendu : l'oracle
existe (où, comment l'appeler), les premiers vecteurs, le coût mesuré.
