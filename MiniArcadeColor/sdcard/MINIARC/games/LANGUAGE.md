# MAG script - games for MiniArcade Color

MAG script is the language of the games the MiniArcade Color console loads
from its SD card. A game is one plain text file, `/MINIARC/games/NAME.MAG`.
The console compiles it when the player starts it, so a mistake shows up on
the console's screen as `line N: reason` together with the line itself.

This document is the complete reference. It is written so that a person or
an AI can write a correct game from it alone. Three complete example games
are in the same folder: `meteors.mag` (arcade), `iceslide.mag` (puzzle with
20 levels) and `quest.mag` (scrolling platformer).

Contents: 1 Quick facts, 2 Files, 3 A minimal game, 4 Source text,
5 Header, 6 Declarations, 7 Statements, 8 Expressions, 9 The frame,
10 Screen and colours, 11 Data blocks (sprites, maps, levels, tiles),
12 Built-in functions, 13 Saving, 14 Limits, 15 Errors, 16 Speed,
17 Big games, 18 Common mistakes, 19 A checklist.

---

## 1. Quick facts

- Values are **32-bit signed integers only**. No fractions, no strings in
  variables, no tables or objects. Text in double quotes is only allowed as
  the text argument of `text()` and `textw()`.
- Screen: 320 x 240 pixels, (0, 0) is the top left corner, y grows downwards.
- The console calls `init()` once, then `update()` and `draw()` every frame,
  **30 times a second** (or 60 with `fps 60`).
- Keys: `UP DOWN LEFT RIGHT OK`. `btn(k)` = held, `btnp(k)` = pressed now.
- Graphics come from text: sprites and maps are rows of characters in
  double quotes, written in the same file.
- Holding OK opens the console's pause menu (continue / restart / quit).
  The game does not need to do anything for it.
- Keywords and names are case-sensitive. Comments start with `--`.

## 2. Files on the card

| What | Where |
|------|-------|
| a game | `/MINIARC/games/NAME.MAG` |
| its save file (written by the console) | `/MINIARC/save/NAME.SAV` |

- The card uses **short file names**: at most 8 letters or digits, then
  `.mag`, e.g. `iceslide.mag`, `quest.mag`. Longer names are not seen.
- Do not call a game `sokoban.mag`, `pumpkin.mag` or `blocks.mag`: those
  names belong to the save files of the console's level packs.
- The list of SD games reads only the first lines of each file (the header),
  so put `game`, `author`, `info` and `fps` at the very top.
- The console never writes anything to the card except the save files.

## 3. A minimal complete game

```
game "Catch"
author "Me"
info "Catch the falling ball with LEFT and RIGHT."

var px, bx, by, points

func init()
  px = 150
  bx = rnd(300)
  by = 0
  points = 0
  score(0)
end

func update()
  if btn(LEFT) then px = px - 4 end
  if btn(RIGHT) then px = px + 4 end
  px = clamp(px, 0, SCREEN_W - 40)
  by = by + 3
  if by > 220 and bx + 8 > px and bx < px + 40 then
    points = points + 1
    score(points)
    sfx(1500, 40)
    by = 0
    bx = rnd(300)
  elif by > SCREEN_H then
    gameover(points)
  end
end

func draw()
  cls(NAVY)
  disc(bx + 4, by + 4, 4, YELLOW)
  rect(px, 224, 40, 6, WHITE)
end
```

## 4. Source text

- **Lines and statements.** Normally one statement per line. Several
  statements may share a line, e.g. `if x < 0 then x = 0 end`. A `;` between
  statements is allowed but never needed. Declarations at the top level
  (`const`, `var`, `array`, the header, the first line of a data block) take
  a line each; one `const`, `var` or `array` line may declare several names
  separated by commas.
- **Comments**: `--` to the end of the line.
- **Names**: letters, digits and `_`, not starting with a digit, at most 31
  characters. Case matters (`speed` and `Speed` are different).
- **Numbers**: decimal `123`, hexadecimal `0x7F`, binary `0b1011`, and one
  character in single quotes, which is its ASCII code: `'#'` = 35,
  `'A'` = 65. The largest decimal number is 2147483647; write negative
  numbers with a minus: `-5`. Hexadecimal and binary numbers may use all 32
  bits (`0xFFFFFFFF` is -1).
- **Texts** in double quotes: at most 60 characters, plain ASCII (and
  ä ö ü ß). No escape sequences; a text cannot contain `"`. Texts are only
  allowed as the text argument of `text()` and `textw()`, and in the header.
- A line is at most 500 characters long. Use spaces or tabs as you like.

**Keywords** (never usable as names): `game author info fps const var array
local func end if then elif elseif else while do for break continue return
and or not sprite map tiles`. `level` starts a level block at the top of the
file and is also a built-in function. The names of built-in functions and
built-in constants cannot be used as names either.

## 5. The header

The file must start with the `game` line (comments and empty lines before it
are fine). The other header lines are optional and come right after it.

| Line | Meaning |
|------|---------|
| `game "Title"` | the game's name, 1..24 characters (the status bar shows the first 14); required, first |
| `author "Name"` | shown in the list of SD games, at most 24 characters |
| `info "Text"` | a short description for the list, at most 100 characters |
| `fps 30` or `fps 60` | frames per second; 30 when left out |

## 6. Declarations

Everything at the top level of the file is a declaration. **Code is only
allowed inside functions.**

### const
```
const TILE = 16, SPEED = 3
const SKY = col(110, 170, 255)
```
A name for a number known when the game is loaded. The value may use
numbers, other consts, built-in constants, `col()` of constants and the
operators. A const must be declared before it is used.

### var
```
var x, y = 10, lives = 3
```
Global variables. Each name may have its own first value (a constant); the
others start at 0. **`var a, b = 3` sets only b to 3; a is 0.** Globals are
set to these values again whenever the game (re)starts: at the start, after
"restart" in the pause menu and after OK on the game over screen. A var must
be declared before any function that uses it (put all var lines at the top).

### array
```
const N = 20
array ex[N], ey[N]
```
A global array of N numbers, all 0 at the start, indexed `ex[0]` to
`ex[N - 1]`. The size is a constant. An index outside the array stops the
game with an error that names the line. Arrays are always global; there
are no local arrays, no arrays of arrays, and no way to pass an array to a
function (use the global array directly).

### func
```
func add(a, b)
  return a + b
end
```
A function with 0 to 8 parameters. Parameters are local variables. A
function returns the value of `return expr`, or 0 when it ends without one
or with a plain `return`. Functions may be called before (above) their
definition and may call themselves (recursion, at most 64 calls deep).
A call must give exactly as many arguments as the function has parameters.
Functions cannot be nested.

The console calls three functions by name, all without parameters:

| Function | Required | When |
|----------|----------|------|
| `init()` | no | once at the start of a round (after the vars are reset) |
| `update()` | yes | every frame, first: move things, read keys |
| `draw()` | yes | every frame, after update(): draw the picture |

### local
```
func update()
  local dx = 0
  local hit
```
A local variable inside a function, from its line to the end of the block it
is in (`end`, `elif`, `else`). One name per `local`. Without `= value` it is
0. A local may not have the name of a global, a const, a function or another
local of the same function.

## 7. Statements

```
x = expr                    -- assign to a local or a global var
arr[i] = expr               -- assign to an element of an array
name(args)                  -- call a function (its value is dropped)

if cond then
  ...
elif cond then              -- any number of elif (elseif works too)
  ...
else
  ...
end

while cond do
  ...
end

for i = first, last do      -- i = first, first+1, ..., last (last included)
  ...
end
for i = 10, 0, -2 do        -- with a step (not 0): 10, 8, 6, 4, 2, 0
  ...
end

break                       -- leave the innermost while / for
continue                    -- go on with the next round of the innermost loop
return                      -- leave the function (value 0)
return expr                 -- leave the function with a value
```

- `then` after if/elif and `do` after while/for may be left out.
- A condition is true when it is not 0.
- In a `for` loop, first, last and step are computed once, before the loop.
  The loop variable is a new local of the loop; changing it inside the loop
  does not change how often the loop runs. If first is already past last,
  the body does not run at all.
- `return` takes a value only when the value is on the same line.

## 8. Expressions

### Operators, from lowest to highest precedence

| Precedence | Operators | Meaning |
|-----------|-----------|---------|
| 1 | `or` | 1 if either side is not 0; the right side is only computed when needed |
| 2 | `and` | 1 if both sides are not 0; the right side is only computed when needed |
| 3 | `== != < <= > >=` | comparison: 1 or 0. **Cannot be chained**: write `a < b and b < c` |
| 4 | `\|` | bitwise or |
| 5 | `^` | bitwise **xor** (not "power"!) |
| 6 | `&` | bitwise and |
| 7 | `<< >>` | shift left, shift right (arithmetic, keeps the sign); the count is taken mod 32 |
| 8 | `+ -` | add, subtract |
| 9 | `* / %` | multiply, divide, remainder |
| 10 | `-x`, `not x` | negate; `not x` is 1 if x is 0, else 0 |

Operators of the same level work from left to right: `10 - 2 - 3` = 5.
Use parentheses whenever in doubt. Note that `not a == b` means
`(not a) == b`; write `not (a == b)` or `a != b`.

### Integer rules
- All arithmetic is 32-bit and wraps around: `2147483647 + 1` = -2147483648.
- `/` cuts off towards zero: `7 / 2` = 3, `-7 / 2` = -3.
- `%` has the sign of the left side: `-7 % 3` = -1. For a remainder that is
  never negative use `((a % n) + n) % n`.
- Division or remainder by 0 stops the game with an error (a constant 0 is
  already refused when the game loads).
- For smooth movement use fixed point: keep positions in 1/16 pixel
  (`x = x + vx` with `vx = 24` means 1.5 pixels per frame) and draw at
  `x / 16`.

### Values of names
- a local, a var: its value; `arr[i]`: an element;
- a const or built-in constant: its value;
- a sprite, map or tile set name: its number (see 11.6);
- a function: only callable, `name(...)`.

## 9. The frame

Every frame the console does this:

1. waits until it is time for the next frame (30 or 60 per second);
2. reads the keys, **clears the screen to black**;
3. calls `update()`, then `draw()`;
4. if the game has called `score()` at least once, draws the status bar
   (title, score, best score, battery) over the top 26 pixels (`BAR_H`);
5. sends the picture to the display;
6. if `gameover()` was called in this frame: shows the game over screen.

- `update()` runs exactly once per frame. When the console is too busy the
  game slows down; it never skips updates. Use `time()` for real time.
- Draw everything in `draw()`. The screen is empty at the start of every
  frame, so draw the whole picture every frame (`cls(colour)` for a
  background).
- **Pause**: holding OK for about 0.7 s opens the console's pause menu:
  continue, restart (vars reset, `init()` again), quit to the list of SD
  games. The game is frozen meanwhile and `time()` does not advance.
- **Game over**: `gameover(points)` shows the console's game over screen
  after the frame (with "NEW RECORD!" when it beats the best score, which
  is then saved on the card). OK starts a new round (vars reset, `init()`
  again), holding OK goes back to the list. Code after `gameover()` in the
  same frame still runs.
- A game without an end does not need `gameover()`; the player leaves with
  the pause menu.

## 10. Screen, colours, text

- 320 x 240 pixels. Constants: `SCREEN_W` = 320, `SCREEN_H` = 240,
  `BAR_H` = 26 (the height of the status bar).
- Drawing outside the screen is fine; it is cut off.
- A colour is a number 0..255 (the console's 256-colour palette). Use the
  colour names or `col(r, g, b)` with r, g, b in 0..255 (the nearest colour
  is used). **A colour is never 0xRRGGBB**: `col(255, 128, 0)`, not
  `0xFF8000`. A colour outside 0..255 stops the game with an error.

| Name | | Name | | Name | |
|------|-|------|-|------|-|
| `BLACK` | | `WHITE` | | `RED` | |
| `GREEN` | | `BLUE` | | `YELLOW` | |
| `CYAN` | | `ORANGE` | | `PURPLE` | |
| `PINK` | | `GREY` | | `BROWN` | |
| `DKGREY` | dark grey | `LTGREY` | light grey | `DKGREEN` | dark green |
| `DKRED` | dark red | `DKBLUE` | dark blue | `NAVY` | very dark blue |
| `LIME` | yellow green | `SAND` | light brown | | |

Text sizes for `text()`, `num()`, `textw()`, `numw()`:

| size | look | width of a letter | height |
|------|------|-------------------|--------|
| 1 | small | 5 | 9 |
| 2 | normal (the default) | 10 | 18 |
| 3 | bold | 14 | 24 |
| 4 | big bold | 21 | 36 |

Every letter has the same width, so a text of n letters is n times as wide.
To centre a text: `text((SCREEN_W - textw("HELLO", 3)) / 2, 100, "HELLO", WHITE, 3)`.

## 11. Data blocks

Sprites, maps, levels and tile sets are declared at the top level, usually
at the end of the file. Code may use them before (above) their declaration.
Their rows **stay on the card**: the console reads them when they are drawn
(see 17), so a game can have hundreds of levels without using RAM for them.

### 11.1 sprite
```
sprite ship 8 6
"...ww..."
"..wccw.."
".wbbbbw."
"wrrbbrrw"
"wr.ss.rw"
"w......w"
```
`sprite NAME WIDTH HEIGHT` (1..64 each), then exactly HEIGHT rows, one per
line, each exactly WIDTH characters in double quotes. Each character is a
palette letter:

| Letter | Colour | Letter | Colour | Letter | Colour |
|--------|--------|--------|--------|--------|--------|
| `.` or space | transparent | `k` | black | `w` | white |
| `r` | red | `o` | orange | `y` | yellow |
| `g` | green | `d` | dark green | `c` | cyan |
| `b` | blue | `p` | purple | `m` | pink (magenta) |
| `s` | grey (silver) | `n` | brown | `a` | dark grey (ash) |
| `t` | sand / skin (tan) | | | | |

Any other character is an error. Upper case letters are not palette letters.

### 11.2 map
```
map room 8 4
"########"
"#..o...#"
"#.@..e.#"
"########"
```
`map NAME WIDTH HEIGHT` (width 1..400, height 1..200), then HEIGHT rows of
WIDTH characters. Any printable ASCII character except `"` may be used; the
game decides what they mean (`mget()`), and a tile set decides how they look
(`drawmap()`).

### 11.3 level
```
level 16 12
"################"
...
```
`level WIDTH HEIGHT` is a map without a name. Levels are numbered 1, 2, 3,
... in the order they appear in the file; `level(n)` gives the map of level
n and `levels()` how many there are. Use them for games with many levels:
`loadmap(level(n))`.

### 11.4 tiles
```
tiles land 16
  "#" = brick          -- a sprite
  "=" = grass
  "~" = BLUE           -- a colour: a filled square
  "." = col(0, 0, 60)
end
```
`tiles NAME SIZE` (1..64 pixels per tile), then one line per map character:
the character in double quotes, `=`, and a sprite name or a colour. A sprite
must be square, SIZE x SIZE, or a whole part of it: with SIZE 16 an 8 x 8
sprite is drawn twice as big, a 4 x 4 one four times. Characters that are
not listed are not drawn (the background shows). Ends with `end`.

### 11.5 Rows
- One row per line, nothing else on that line (a comment after it is fine).
- Every row has exactly the width given; a wrong length is an error naming
  the row.
- Keep the rows of a block evenly indented (all rows the same way). Uneven
  indentation works but costs a little RAM.

### 11.6 Sprites, maps and tile sets are numbers
The name of a sprite is the number of the sprite: the first sprite in the
file is 0, the next 1, and so on (maps and levels share their own numbering,
tile sets theirs). So sprites defined one after the other can be animated by
adding to the first one's name:
```
sprite walk1 16 16
...
sprite walk2 16 16
...
spr(walk1 + frame % 2, x, y)
```
A number that is not a sprite (map, tile set) stops the game with an error.

## 12. Built-in functions

All arguments are numbers unless marked "text". Optional arguments are in
square brackets. Every built-in returns a number (0 when it has nothing to
return). Coordinates are in pixels.

### Drawing

| Function | What it does |
|----------|--------------|
| `cls(c)` | fill the whole screen with colour c |
| `rect(x, y, w, h, c)` | filled rectangle, top left x, y, w x h pixels (nothing if w or h <= 0) |
| `box(x, y, w, h, c)` | rectangle outline, 1 pixel |
| `line(x1, y1, x2, y2, c)` | line, both ends included |
| `circle(x, y, r, c)` | circle outline around x, y with radius r (0..1000) |
| `disc(x, y, r, c)` | filled circle |
| `tri(x1, y1, x2, y2, x3, y3, c)` | filled triangle |
| `pixel(x, y, c)` | one pixel |
| `text(x, y, "text", c [, size])` | text, top left x, y; size 1..4, default 2 |
| `num(x, y, n, c [, size])` | the number n as text |
| `textw("text" [, size])` | width of the text in pixels |
| `numw(n [, size])` | width of the number n in pixels |
| `spr(s, x, y [, scale [, flip]])` | sprite s with its top left at x, y; scale 1..8 (default 1); flip not 0 = mirrored left/right |
| `sprw(s)`, `sprh(s)` | width and height of sprite s |
| `drawmap(m, t, x, y)` | map m drawn with tile set t, the map's top left corner at x, y; only the tiles on the screen are drawn and read |

For scrolling, give the map's corner a negative x: `drawmap(world, land, -camx, 32)`.

### Maps

| Function | What it does |
|----------|--------------|
| `loadmap(m)` | copies map m from the card into RAM (at most 16384 cells). It is then "the loaded map": `mset()` can change it, `mget()` and `drawmap()` use the changed copy. Only one map is loaded at a time; `loadmap()` again (the same or another map) starts from the card's version |
| `mget(m, x, y)` | the character of map m at column x, row y, as a number (`'#'` = 35); **0 outside the map**. From RAM for the loaded map, otherwise read from the card |
| `mset(m, x, y, ch)` | sets a character of the loaded map (m must be the loaded map, x, y inside, ch 32..126) |
| `mapw([m])`, `maph([m])` | width and height of map m, or of the loaded map (0 if none) |
| `level(n)` | the map of level block n (1..levels()) |
| `levels()` | the number of level blocks |

### Keys and tilt

| Function | What it does |
|----------|--------------|
| `btn(k)` | 1 while key k is held |
| `btnp(k)` | 1 in the frame key k was pressed. Arrow keys held down repeat (after 0.25 s, then every 0.08 s), like in the console's menus |
| `tiltx()`, `tilty()` | how far the console is tilted, -100..100 (right/down positive); 0 without a tilt sensor or with tilt control off. Tilting also works like the arrow keys |

Keys: `UP` `DOWN` `LEFT` `RIGHT` `OK`. On consoles without an own OK key, OK
is UP+DOWN (or LEFT+RIGHT) pressed together; those presses do not count as
the arrow keys. Arrow keys are read about 0.07 s after they go down for that
reason. Holding OK opens the pause menu; a short OK press is `btnp(OK)`.

### Sound, numbers, time

| Function | What it does |
|----------|--------------|
| `sfx(freq, ms)` | a beep of freq Hz (40..15000) for ms milliseconds (at most 2000); a new one replaces the one playing. Silent when the sound is off |
| `rnd(n)` | a random number 0..n-1 (0 if n <= 0) |
| `time()` | milliseconds since this round started (the pause not counted) |
| `abs(x)`, `min(a, b)`, `max(a, b)` | as usual |
| `clamp(v, lo, hi)` | v kept within lo..hi |
| `sin(a)`, `cos(a)` | a in degrees (any number, taken mod 360), result -1024..1024. `x + r * cos(a) / 1024` |
| `atan2(dy, dx)` | the angle 0..359 pointing along dx, dy (0 = right, 90 = down, as on the screen) |
| `sqrt(n)` | the integer square root (0 for n <= 0) |
| `col(r, g, b)` | the colour nearest to r, g, b (each 0..255) |

### Score and game over

| Function | What it does |
|----------|--------------|
| `score(n)` | sets the score shown in the status bar (0..65535 shown); the first call switches the status bar on |
| `gameover(n)` | ends the round with n points after this frame (see 9) |

## 13. Saving

| Function | What it does |
|----------|--------------|
| `save(slot, v)` | keeps v in save slot 0..15 |
| `load(slot)` | the value of slot 0..15; 0 if it was never saved |

- Slots survive switching the console off; they are in
  `/MINIARC/save/NAME.SAV` on the card, together with the best score.
- `save()` is cheap: the console writes the file at most every 2 seconds,
  when a round ends and when the game is left, and only if something
  changed. Typical use: the level reached, `save(0, reached)`.
- The best score is kept by the console itself (`gameover()`); there is no
  need to save it.
- When the card cannot be written (full, write protected, pulled) the game
  goes on; the values are written later if possible.

## 14. Limits

| What | Limit |
|------|-------|
| compiled code | 24576 bytes (about 2000-3000 lines of code) |
| var + array together | 2048 numbers |
| functions | 249 |
| parameters of a function | 8 |
| locals of a function (a for loop takes 3) | 32 |
| calls inside calls (recursion) | 64 deep |
| values on the stack | 1024 |
| blocks inside blocks (if, while, for) | 16 |
| sprites + maps + levels | 600 |
| tile sets | 16 |
| sprite | 64 x 64 |
| map, level | 400 x 200 |
| a map loaded with loadmap() | 16384 cells (e.g. 128 x 128) |
| names | 1200, at most 31 characters each |
| statements | 4000 |
| text in quotes | 60 characters |
| line | 500 characters |
| save slots | 16 |
| instructions per frame (update + draw) | 150000 |
| instructions for init() | 2000000 |

Very deep nesting (brackets in brackets in calls in blocks) is refused with
"nested too deeply" before it could overflow the console's stack.

## 15. Errors

### When the game loads
The game is compiled when it starts. The first mistake is shown as

```
line 12: unknown variable 'scroe' - declare it: local scroe = ... (in a function) or var scroe at the top
```

followed by the line itself. OK goes back to the list. Typical messages:

| Message | Cause |
|---------|-------|
| `a game starts with its name: game "My Game"` | the first line is not `game "..."` |
| `the game needs a func update()` / `draw()` | an entry function is missing |
| `unknown variable 'x' ...` | assignment to a name never declared |
| `unknown name 'x'` | a name used that is not declared anywhere |
| `unknown function 'f'` | a call of a function that does not exist |
| `'if' on line 3 has no 'end'` | a missing `end` (the line of the open block) |
| `'+=' is not supported: write x = x + ...` | other languages' operators: `+= -= ++ && \|\| ! ~= . { } #` |
| `use == to compare (= only assigns)` | `if a = 1` |
| `write 'elif' instead of 'else if'` | |
| `only whole numbers: no fractions like 1.5 ...` | |
| `a text in quotes can only be the text of text() or textw()` | |
| `rect() needs 5 arguments, got 3` | wrong number of arguments |
| `f() takes 1 argument (line 4), called with 2` | |
| `division by zero` | a constant 0 after `/` or `%` |
| `'arr' is an array: use arr[index]` | |
| `'K' is a const and cannot be assigned` | |
| `'end' is a keyword and cannot be used as a name` | also for built-in names |
| `'a' is already defined on line 2` | |
| `row 2 of sprite s has 3 characters, but the sprite is 4 wide` | |
| `'q' is not a palette letter (. k w r o y g d c b p m s n a t)` | |
| `sprite s has 2 rows, but 3 are needed` | |
| `argument 1 of spr() must be a sprite, but 'm' is a map` | |
| `comparisons cannot be chained: write (a < b) and (b < c)` | |
| `'break' outside a while or for loop` | |
| `local 'a' would hide a variable of the same name (line 3) ...` | |
| `use 'func' to define a function` | `function` instead of `func` |
| `only declarations at the top (...) - code goes inside a func` | a statement outside a function |
| `'y' is used on line 3 before it is declared here ...` | a var declared below its first use |
| `tile '#': its sprite is 3x3, a tile of 16 pixels needs ...` | |
| `too many variables: var and array together have at most 2048 numbers` | |
| `the game is too big: more than 24576 bytes of code ...` | |
| `line too long (at most 500 characters)` | |
| `nested too deeply ...` | split the code into functions |
| `not enough memory to compile a game` | the console has no room (rare) |

### While the game runs
A runtime error stops the game and shows the line it happened in, then OK
goes back to the list:

| Message | Cause |
|---------|-------|
| `script too slow or endless loop in update() (150000 instructions in one frame)` | a loop that never ends, or far too much work in one frame (also `draw()`, `init()`) |
| `array index 10 is outside 0..9` | |
| `division by zero` | |
| `functions call each other too deep (more than 64) - endless recursion?` | |
| `the stack is full ...` | the same, with many locals |
| `colour 300 is not 0..255 - use a colour name or col(r, g, b)` | |
| `99 is not a sprite` / `not a map` / `not a tile set` | a wrong number given as a sprite, map, tile set |
| `mset: this map is not loaded - call loadmap(map) first` | |
| `mset: 5, 0 is outside the map (2 x 1)` | |
| `level(0): there are 20 levels (1..20)` | |
| `btn(7): the keys are UP, DOWN, LEFT, RIGHT, OK` | |
| `the step of a for loop is 0` | |
| `save(16, ...): the slots are 0..15` | |
| `loadmap: the map has 20000 cells, at most 16384 fit in RAM` | |

A bad game never crashes or hangs the console.

### The card is pulled
If the SD card is pulled during a game, the game goes on as long as it only
needs what it has already read. When it needs a part of its file it has not
read yet, it shows "insert the SD card" and waits; when the card is back,
the game goes on where it was (if the file is still the same). Holding OK
leaves the game.

## 16. Speed

- The budget is 150000 instructions per frame for `update()` and `draw()`
  together. A simple statement like `x = x + vx` is about 4 instructions, a
  call of a built-in about 5 plus its arguments; a drawing built-in costs a
  little more for big areas. The example games use 1000-10000 per frame.
  Stay well below the budget: at 30 frames a second, about 30000
  instructions per frame are comfortable on the console.
- Drawing is done by the console and is fast: `drawmap()` of a whole
  screen of tiles costs about as much as a few hundred instructions.
- Avoid looping over a whole big map every frame. Draw it with `drawmap()`,
  look at single cells with `mget()`, and keep moving things in arrays.
- `fps 60` halves the time per frame; use it only for small games.

## 17. Big games

- **Many levels**: write them as `level` blocks and load the current one with
  `loadmap(level(n))`. Only that level is in RAM; the others stay on the
  card. Save the level reached with `save()`.
- **Maps bigger than RAM, scrolling worlds**: do not `loadmap()` them. Draw
  them with `drawmap(m, t, -camx, y)` and test cells with `mget(m, x, y)`:
  the console reads only the rows and columns it needs from the card and
  keeps the last ones in a cache (4 KB of the file; sprites are kept decoded,
  6 KB). Things that change (collected coins, defeated enemies) go into
  arrays; pick them up from the map as they come into view (see quest.mag,
  `lookahead()`).
- A map that is loaded can be changed with `mset()`; a map that is not
  loaded cannot.
- The examples: `iceslide.mag` (20 levels, loadmap/mget/mset, save/load),
  `quest.mag` (three worlds of 200-240 x 13 tiles drawn from the card).

## 18. Common mistakes to avoid

1. Forgetting `end` after `if`, `while`, `for` or `func`.
2. `else if` - write `elif`.
3. `x += 1`, `x++`, `x--` - write `x = x + 1`. (`--` starts a comment!)
4. `&&`, `||`, `!`, `~=` - write `and`, `or`, `not`, `!=`.
5. `^` for power - `^` is XOR. Write `x * x`.
6. Fractions like `0.5` - use bigger units (1/16 pixel) and divide when drawing.
7. Strings in variables, string concatenation, `print()` - texts only in
   `text()`; numbers with `num()`.
8. `function` - write `func`. Code outside functions - put it in `init()`.
9. `var a, b = 3` expecting both to be 3 - write `var a = 3, b = 3`.
10. Arrays: indexes start at 0 and end at N-1; arrays are global and have a
    fixed size; `#arr` does not exist (keep the size in a const).
11. `not a == b` - means `(not a) == b`. Write `a != b`.
12. `a < b < c` - write `a < b and b < c`.
13. Colours like `0xFF0000` - use `RED` or `col(255, 0, 0)`.
14. Sprite rows of the wrong length, letters that are not in the palette
    (only `. k w r o y g d c b p m s n a t`), or capital letters.
15. Naming a variable `level`, `map`, `sprite`, `time`, `score`, `min` ...
    (built-in names and keywords are reserved).
16. Using a var in a function above its `var` line - put all `var`,
    `const` and `array` lines at the top of the file.
17. Expecting the screen to keep last frame's picture - it is cleared before
    every frame; draw everything in `draw()`.
18. Forgetting `score(0)` in `init()` when the status bar should show from
    the start; drawing important things in the top 26 pixels when the
    status bar is on.
19. `mset()` on a map that was not loaded with `loadmap()`.
20. Using `/` on negative fixed-point positions for tiles: `-1 / 16` is 0,
    not -1. Keep world positions >= 0 or use `clamp()`.
21. Huge loops every frame (e.g. over every cell of a big map) - see 16.
22. A file name longer than 8 letters, or not ending in `.mag`.
23. `return` with the value on the next line - the value must be on the
    same line as `return`.
24. Calling `gameover()` every frame after the end - call it once.

## 19. A checklist for a new game

- First line `game "..."`, then `author`, `info`, maybe `fps 60`.
- Then `const`, `var`, `array` lines.
- `func init()` sets every variable for a new round (also called after
  restart and after game over).
- `func update()` reads keys with `btn()`/`btnp()` and moves things.
- `func draw()` starts with `cls(colour)` and draws everything.
- `score()` for a score in the status bar, `gameover()` when the round is
  lost, `save()`/`load()` for progress.
- Sprites, maps, levels and tile sets at the end of the file; every row
  exactly as wide as declared.
