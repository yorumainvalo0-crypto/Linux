MiniArcade Color - Level von der SD-Karte / levels from the SD card
=====================================================================

DEUTSCH

Den Ordner MINIARC (aus sdcard/) ins Hauptverzeichnis einer FAT32-Karte
kopieren. Sokoban, Pumpkin Maze und Block Puzzle (Abenteuer) zeigen die
Level der Karte dann hinter den eingebauten, mit "SD" markiert, alle offen.
Die Konsole formatiert die Karte nie und schreibt nichts außer dem
Fortschritt dieser Level: MINIARC/save/<spiel>.sav (zuerst als .tmp, dann
umbenannt). Ohne Karte läuft alles wie immer; wird die Karte während eines
Levels gezogen, läuft das Level weiter (es liegt im RAM), nur sein
Ergebnis wird nicht gespeichert.

Dateien: MINIARC/levels/sokoban.txt, pumpkin.txt, blocks.txt - ein Level
pro Zeile (höchstens 318 Zeichen, höchstens 999 Level). Eine Zeile, die
mit # beginnt und kein | enthält, ist ein Kommentar; leere Zeilen
zählen nicht. Kaputte Zeilen werden übersprungen und gezählt.
Neue Packs macht und prüft tools/levelpack.py ("check", "gen", "samples").

ENGLISH

Copy the folder MINIARC (from sdcard/) to the root of a FAT32 card.
Sokoban, Pumpkin Maze and Block Puzzle (adventure) then list the card's
levels after the built-in ones, marked "SD", all of them open. The console
never formats the card and writes nothing but the progress of these
levels: MINIARC/save/<game>.sav (written as .tmp first, then renamed).
Without a card everything is as before; pull the card during a level and
the level goes on (it is in RAM), only its result is not saved.

Files: MINIARC/levels/sokoban.txt, pumpkin.txt, blocks.txt - one level a
line (318 characters at most, 999 levels at most). A line that starts with
# and has no | in it is a comment; empty lines do not count. Broken lines
are skipped and counted. tools/levelpack.py makes and checks packs
("check", "gen", "samples").

FORMATS / FORMATE

sokoban.txt   the rows of the level split by |:
                #####|#@$.#|#####
              # wall, space (or - or _) floor, . goal, $ box, * box on a
              goal, @ player, + player on a goal. 3..8 rows of up to 21,
              one player, as many boxes as goals, walls all round.

pumpkin.txt   NAME (12 characters at most) and 13 rows of 20 tiles, all
              split by | (the phone editor's text):
                FIRST ROLL|####################|#S....#...
              . floor  # wall  S start  G goal  O pit  i ice  m mud
              * bumper  > < ^ v arrows  c candy  k key (3 at most)
              D door (opens with every key)  C rune  H U moving pits (6)
              one start, one goal that can be reached.

blocks.txt    goal moves target board - goal gems, blocks (every stone
              and ice) or score (target = the points), 1..99 moves, the
              board 8 rows of 8 split by |:
                gems 12 0 ........|........| ... |xgxx.xxx|xxx.xgxx
              . free  x block  g gem  s stone  i ice (2 clears).
              8 free cells at least; a score of 1..100 points a move.
