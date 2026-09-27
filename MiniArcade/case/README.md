# MiniArcade case

3D printable case for the 70 x 90 mm perfboard. Everything is in
`miniarcade_case.scad` (OpenSCAD); the STL files next to it are rendered
from it and ready for the slicer.

![case](preview.png)

| file | what | print |
|---|---|---|
| `testplate.stl` | 1.2 mm plate the size of the board - print this first | flat, ~10 min |
| `bottom.stl` | back shell: screw posts, battery guides, USB-C openings, switch hole, sound holes | floor down |
| `lid.stl` | front: display window, pocket for the buttons, screw bosses | front face down |

![buttons](preview_pocket.png)

The buttons are pressed directly: around the D-pad the lid sinks into a
pocket whose floor is level with the tops of the plungers, and each switch
looks through its own square hole. (Separate caps are still in the file:
`direct = false`, part `caps`.)

No supports needed (the pocket floor is a short bridge). PLA or PETG, 0.2 mm layers, 3 walls, 15 % infill works
on a Creality K2 Plus (both shells fit on the bed together).

## Test plate first

Lay the test plate on the front of the board:

- the 4 holes sit over the corner holes of the board
- the 5 switch bodies go through their square holes without touching
- the display window frames the picture (switch the console on)
- the notches on the left / right edge point at the two USB-C sockets

If something is off, change the number in the `.scad` file (e.g. `win_c`,
`buttons`, `usb_esp`) and render again:

    openscad -D 'part="lid"' -o lid.stl miniarcade_case.scad

## Please measure before printing the shells

These were estimated from photos:

| parameter | now | what |
|---|---|---|
| `btn_h` | 5.0 | top of a button plunger above the board - the pocket floor sits there |
| `front_h` | 7.5 | tallest part on the front (display module) plus a little room |
| `back_h` | 16 | tallest part on the back (buzzer ~15 mm) plus room for the battery |
| `bat` | 51 x 66 x 5.5 | the Samsung battery |

If `btn_h` is a bit off the plungers only stand a little above or below the
pocket floor - the switch bodies pass through the holes either way.

## Assembly

- 4 x M2.5 x 25 countersunk screws from the back: they go through the floor,
  the posts and the board and bite into the bosses of the lid.
- Toggle switch: mounted in the top wall (6.4 mm hole for the M6 bushing of a
  mini toggle switch), held by its own nut on the outside. Move it with
  `switch_x` / `switch_z`.
- Battery: lies in the four corner guides on the floor, below the modules.
- USB-C: both sockets are reachable through notches in the side walls (the
  ESP32 on the left, the charger on the right).
