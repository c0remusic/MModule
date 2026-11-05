# Effets d'envoi partagés

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 10, 11

## Quoi

Un exemplaire de chaque effet, retour stéréo, en parallèle : delay et reverb avec sélecteur de source (Syntakt,
Echo et Gate Box du MD), envoi delay vers reverb du Syntakt gardé ; chorus et phaser/flanger (deux modes) maison en
flottant, marqués « + ». Chaque retour sur Main ou une sortie choisie. Une piste séparée alimente quand même les
envois. Le réglage à l'oreille du chorus et du phaser/flanger contre le Virus TI se fait avec l'utilisateur, en fin
de ticket.

## Critère de fin

- Delay et reverb du Syntakt contre le port exact (Q1 ou Q2).
- Envoi à 0 : null (Q5).
- Coût des envois mesuré ; l'utilisateur a validé chorus et phaser/flanger à l'écoute.

## Références

Spécification §5. Ticket « Sources des effets d'envoi ».

## Noté par le ticket 10 (2026-11-05)

- `Echo` et `GateBox` sont prêts : pointeurs stéréo bruts, `GateBox` prend le retour de n'importe quel delay par `_echoL`/`_echoR`,
  y compris celui du Syntakt. DVOL existe sur la machine (Echo vers Gate Box) : le garder ou le laisser à 0 pour « en parallèle »
  se décide ici. L'ordre, delay avant reverb dans un bloc, reste implicite : à dire.
- FB au-delà de 64 auto-oscille, sur la machine et dans le port.
