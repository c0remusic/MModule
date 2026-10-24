# Fonctions de MoDule Seq

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Relever les fonctions du séquenceur Max for Live
MoDule Seq (c0remusic/MModuleSeq, md-drums-seq) : pas, longueurs, locks, slides, accents, conditions, micro-timing,
chaînes, transport, ce qu'il envoie à MoDule (ADR 0003).

## Answer

MoDule Seq 0.2.1 (Max for Live, noyau C++) : 16 Tracks, 16 Patterns de 1 à 64 pas communs, 7 échelles MKII, Hits
avec vélocité, 24 Locks par pas, 65 Conditions d'Elektron (X%, A:B, FILL, PRE, NEI, 1ST et négations), ratchets 1-8
avec courbe, micro-timing ±23/24, Swing, Accent et Slide par Pattern en rangées communes, Mutes, Fill tenu, bascule
en fin de Pass, chaînes de 16, transport esclave de Live, impression et capture en clips Session, Push 2. Il envoie
tout en notes (ADR 0003) : Hits 36-51, Locks 64-87, Accent 88, Slide 89-114, hello 124-127 ; MoDule lui rend le Kit par
fichier. Pas d'enregistrement live, pas de longueur par Track. Détail : [research/06-module-seq.md](../research/06-module-seq.md).
