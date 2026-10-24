MiniArcade Color - Spiele von der SD-Karte / games from the SD card
=====================================================================

DEUTSCH

Den Ordner MINIARC (aus sdcard/) ins Hauptverzeichnis einer FAT32-Karte
kopieren, so dass die Spiele in MINIARC/games/ liegen, z. B.

    MINIARC/games/meteors.mag     Meteor Dodge (Arcade)
    MINIARC/games/iceslide.mag    Ice Slide (Puzzle, 20 Level)
    MINIARC/games/quest.mag       Pixel Quest (Jump & Run)

Im Menü der Konsole steht dann "SD-Spiele": die Liste der Spiele auf der
Karte, OK startet eins. Eigene Spiele sind Textdateien in MAG script; die
Sprache beschreibt LANGUAGE.md (auf Englisch, auch zum Weitergeben an eine
KI, die ein Spiel schreiben soll). Dateinamen: höchstens 8 Buchstaben oder
Ziffern, dann .mag (die Karte kennt nur kurze Namen); nicht sokoban.mag,
pumpkin.mag oder blocks.mag nennen. Ein Fehler im Spiel wird beim Start mit
Zeilennummer angezeigt. Die Konsole formatiert die Karte nie; sie schreibt
nur Rekord und Spielstände: MINIARC/save/<name>.sav. Wird die Karte während
eines Spiels gezogen, wartet das Spiel (falls es etwas Neues von der Karte
braucht) mit "SD-Karte wieder einstecken" und geht danach weiter.

ENGLISH

Copy the folder MINIARC (from sdcard/) to the root of a FAT32 card, so that
the games are in MINIARC/games/, e.g.

    MINIARC/games/meteors.mag     Meteor Dodge (arcade)
    MINIARC/games/iceslide.mag    Ice Slide (puzzle, 20 levels)
    MINIARC/games/quest.mag       Pixel Quest (platformer)

The console's menu then has "SD games": the list of the games on the card,
OK starts one. Games of your own are text files in MAG script; LANGUAGE.md
describes the language (give it to an AI that should write a game). File
names: at most 8 letters or digits, then .mag (the card only knows short
names); do not call one sokoban.mag, pumpkin.mag or blocks.mag. A mistake in
a game is shown with its line number when it starts. The console never
formats the card; it writes only the best score and save slots:
MINIARC/save/<name>.sav. If the card is pulled during a game, the game waits
(when it needs something new from the card) with "insert the SD card" and
goes on when it is back.
