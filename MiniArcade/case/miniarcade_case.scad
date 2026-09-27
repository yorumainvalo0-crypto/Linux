// MiniArcade case for the 70 x 90 mm perfboard (ESP32-C3 SuperMini, 0.96" OLED,
// 5 tact switches, TP4056 USB-C charger, buzzer, Samsung phone battery).
//
// Seen from the front with the display on top (like a Game Boy):
//   X = 0..70 from the left board edge, Y = 0..90 from the bottom board edge,
//   Z = 0 at the underside of the board, the front (display side) is +Z.
// Measured from photos; every value that matters is a parameter below.
// Print "testplate" first and lay it on the board to check the holes.
//
// Render one part:  openscad -D 'part="bottom"' -o bottom.stl miniarcade_case.scad
// parts: bottom, lid, caps, testplate, all (preview)

part = "all";

// ---------------- board ----------------
board_w = 70;
board_h = 90;
board_t = 1.6;
holes   = [[2.5, 3.2], [67.5, 3.2], [2.5, 86.8], [67.5, 86.8]];   // corner holes

// ---------------- heights (measure if you can) ----------------
front_h = 7.5;    // room above the board: display module and buttons fit below this
btn_h   = 5.0;    // tact switch: top of the plunger above the board
back_h  = 16.0;   // room below the board: modules, wires, buzzer (~15 mm), battery

// ---------------- case ----------------
wall     = 2.0;
floor_t  = 2.0;
top_t    = 2.0;
gap      = 0.5;   // board edge to wall
corner_r = 4.0;
tol      = 0.25;  // clearance for moving parts (caps)

// ---------------- screws: M2.5 x 25 countersunk, from the back ----------------
screw_d  = 2.8;   // clearance through floor, post and board
pilot_d  = 2.1;   // bites into the lid bosses
head_d   = 5.2;
post_d   = 6.4;   // reaches a little into the wall (a flush touch breaks the mesh)

// ---------------- display ----------------
win_c = [36.75, 76.2];   // centre of the visible area
win   = [24.0, 13.0];    // window at the inside of the panel (widens outwards)

// ---------------- buttons: [x, y, shape] 0 = arrow up/right/down/left, 4 = OK ----------------
btn_c = [35.1, 45.0];
buttons = [
  [35.1, 54.8, 0],      // up
  [45.2, 44.6, 1],      // right
  [35.1, 33.3, 2],      // down
  [24.4, 45.0, 3],      // left
  [35.1, 45.0, 4]       // OK in the middle
];
cap_sq  = 6.6;           // arrow caps are square: they cannot turn
cap_rd  = 6.8;           // OK cap is round
cap_out = 1.5;           // how far the caps stand out of the case

// ---------------- openings ----------------
usb_esp = 14.5;          // ESP32 USB-C on the left edge, Y of its centre
usb_chg = 13.5;          // charger USB-C on the right edge, Y of its centre
usb_w   = 13.0;          // wide enough for the plug housing
usb_d   = 9.0;           // notch depth below the board (open towards the board)
switch_x = 58;           // toggle switch in the top wall
switch_z = -8;           // its centre below the board
switch_d = 6.4;          // M6 bushing of a mini toggle switch
buzzer_c = [12, 77];     // buzzer on the back: sound holes in the floor

// ---------------- battery (Samsung 1900 mAh) ----------------
bat   = [51, 66, 5.5];   // width, length, thickness - measure yours
bat_p = [9.5, 2.0];      // lower left corner of the pocket

$fn = 48;

// =========================================================
W  = board_w + 2 * (gap + wall);
H  = board_h + 2 * (gap + wall);
x0 = -(gap + wall);
y0 = -(gap + wall);
z_floor = -(back_h + floor_t);
z_top   = board_t + front_h + top_t;
z_in    = board_t + front_h;           // inside of the front panel

module rrect(x, y, w, h, r) {
  translate([x + r, y + r]) offset(r = r) square([w - 2 * r, h - 2 * r]);
}
module outer2d() { rrect(x0, y0, W, H, corner_r); }
module inner2d() { rrect(-gap, -gap, board_w + 2 * gap, board_h + 2 * gap, corner_r - wall); }

// ---------------- bottom shell ----------------
module bottom() {
  difference() {
    union() {
      difference() {
        translate([0, 0, z_floor]) linear_extrude(back_h + floor_t) outer2d();
        translate([0, 0, -back_h]) linear_extrude(back_h + 1) inner2d();
      }
      for (h = holes) translate([h[0], h[1], -back_h - 0.01]) cylinder(d = post_d, h = back_h);
      battery_ribs();
    }
    for (h = holes) {                                          // screws from the back
      translate([h[0], h[1], z_floor - 1]) cylinder(d = screw_d, h = back_h + floor_t + 2);
      translate([h[0], h[1], z_floor - 0.01]) cylinder(d1 = head_d, d2 = screw_d, h = (head_d - screw_d) / 2);
    }
    usb_notch(x0 - 1, usb_esp);                                 // ESP32, left
    usb_notch(board_w + gap - 0.5, usb_chg);                    // charger, right
    translate([switch_x, board_h, switch_z]) rotate([-90, 0, 0])  // toggle switch, top wall
      cylinder(d = switch_d, h = gap + wall + 2);
    translate([buzzer_c[0], buzzer_c[1], z_floor - 1])          // sound holes
      for (a = [0 : 60 : 300]) rotate(a) translate([3.2, 0, 0]) cylinder(d = 1.6, h = floor_t + 2);
    translate([buzzer_c[0], buzzer_c[1], z_floor - 1]) cylinder(d = 1.6, h = floor_t + 2);
  }
}

module usb_notch(x, yc) {                                       // open towards the board
  translate([x, yc - usb_w / 2, -usb_d]) cube([wall + gap + 1.5, usb_w, usb_d + 1]);
}

module battery_ribs() {                                         // four corner guides
  rib = 1.2; len = 8; hgt = min(4, bat[2]);
  for (cx = [0, 1], cy = [0, 1]) {
    px = bat_p[0] + cx * bat[0];
    py = bat_p[1] + cy * bat[1];
    sx = cx ? 1 : -1;
    sy = cy ? 1 : -1;
    translate([0, 0, -back_h - 0.01]) linear_extrude(hgt) {
      translate([px + (sx > 0 ? 0.5 : -0.5 - rib), py + (sy > 0 ? -len : 0)]) square([rib, len]);
      translate([px + (sx > 0 ? -len : 0), py + (sy > 0 ? 0.5 : -0.5 - rib)]) square([len, rib]);
    }
  }
}

// ---------------- front lid ----------------
module lid() {
  difference() {
    union() {
      difference() {
        linear_extrude(z_top) outer2d();
        translate([0, 0, -1]) linear_extrude(z_in + 1) inner2d();
      }
      for (h = holes) translate([h[0], h[1], board_t]) cylinder(d = post_d, h = front_h + 0.01);
    }
    for (h = holes) translate([h[0], h[1], board_t - 1]) cylinder(d = pilot_d, h = front_h);
    window();
    for (b = buttons) translate([b[0], b[1], 0]) cap_hole(b[2]);
  }
}

module window() {                                               // chamfered: wide view
  hull() {
    translate([win_c[0] - win[0] / 2, win_c[1] - win[1] / 2, z_in - 0.01]) cube([win[0], win[1], 0.01]);
    translate([win_c[0] - win[0] / 2 - top_t, win_c[1] - win[1] / 2 - top_t, z_top])
      cube([win[0] + 2 * top_t, win[1] + 2 * top_t, 0.01]);
  }
}

module cap_hole(shape) {
  translate([0, 0, z_in - 3]) linear_extrude(top_t + 4) cap2d(shape, tol);
}
module cap2d(shape, grow) {
  if (shape == 4) circle(d = cap_rd + 2 * grow);
  else square(cap_sq + 2 * grow, center = true);
}

// ---------------- button caps ----------------
// Rest on the plunger; the flange keeps them from falling out. Printed top
// down: the 45 degree flange and the engraved mark need no supports.
module cap(shape) {
  stem  = (z_in - 1.0) - (board_t + btn_h);        // flange underside down to the plunger
  body  = top_t + cap_out;
  difference() {
    union() {
      hull() {                                      // flange, 45 degrees
        linear_extrude(0.01) cap2d(shape, 1.0);
        translate([0, 0, 1.0]) linear_extrude(0.01) cap2d(shape, 0);
      }
      translate([0, 0, 1.0]) linear_extrude(body) cap2d(shape, 0);
      translate([0, 0, -stem]) cylinder(d = 3.2, h = stem + 0.01);
    }
    translate([0, 0, 1.0 + body - 0.5]) linear_extrude(1) mark(shape);   // engraved
  }
}
module mark(shape) {
  if (shape == 4) difference() { circle(d = 4); circle(d = 3); }
  else rotate(-90 * shape) polygon([[0, 2], [-1.8, -1.2], [1.8, -1.2]]);   // arrow
}

module caps_plate() {                            // printed top down, stems up
  for (i = [0 : 4]) translate([i * 12, 0, 0]) rotate([180, 0, 0]) translate([0, 0, -(1.0 + top_t + cap_out)])
    cap(buttons[i][2]);
}

// ---------------- test plate ----------------
// 1.2 mm plate the size of the board: lay it on the front and check that
// the screw holes, the buttons and the display window line up. The notches
// on the left and right edge mark where the USB-C sockets sit.
module testplate() {
  difference() {
    linear_extrude(1.2) square([board_w, board_h]);
    for (h = holes) translate([h[0], h[1], -1]) cylinder(d = screw_d, h = 4);
    translate([win_c[0] - win[0] / 2, win_c[1] - win[1] / 2, -1]) cube([win[0], win[1], 4]);
    for (b = buttons) translate([b[0], b[1], -1]) linear_extrude(4) cap2d(b[2], tol);
    for (y = [usb_esp]) translate([0, y, -1]) rotate(45) cube([2.5, 2.5, 4], center = true);
    for (y = [usb_chg]) translate([board_w, y, -1]) rotate(45) cube([2.5, 2.5, 4], center = true);
    translate([buzzer_c[0], buzzer_c[1], -1]) cylinder(d = 3, h = 4);
  }
}

// ---------------- output ----------------
if (part == "bottom") bottom();
else if (part == "lid") translate([0, 0, z_top]) rotate([180, 0, 0]) lid();   // printed face down
else if (part == "caps") caps_plate();
else if (part == "testplate") testplate();
else {
  color("#3a3f4b") bottom();
  color("#2d8f5a", 0.9) cube([board_w, board_h, board_t]);                     // the board
  color("#dfe3ea", 0.85) translate([0, 0, 22]) lid();
  for (b = buttons) color("#e8543f") translate([b[0], b[1], 22 + z_in - 1.0]) cap(b[2]);
}
