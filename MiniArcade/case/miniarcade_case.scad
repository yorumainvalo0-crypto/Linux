// MiniArcade case for the 70 x 90 mm perfboard (ESP32-C3 SuperMini, 0.96" OLED,
// 5 tact switches, TP4056 USB-C charger, buzzer, Samsung phone battery).
//
// Seen from the front with the display on top (like a Game Boy):
//   X = 0..70 from the left board edge, Y = 0..90 from the bottom board edge,
//   Z = 0 at the underside of the board, the front (display side) is +Z.
// Positions were measured from photos; everything that matters is a parameter.
//
// No screws, no supports:
//   - the board sits on four pegs in the back shell and is clamped by the lid
//   - the lid snaps on with four spring tabs (bead into a groove) and comes
//     off again with a fingernail or a coin in the notch at the bottom edge
//   - no overhang steeper than 45 degrees; the only bridges are the short top
//     edges of the USB-C windows and the floor of the button pocket
//
// Render:  openscad -D 'part="bottom"' -o bottom.stl miniarcade_case.scad
// parts:   bottom, lid, testplate, plate (bottom + lid on one bed),
//          assembled / all (previews)

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
corner_r = 4.0;   // outside
inner_r  = 1.0;   // inside: small, so the square corners of the board fit
post_d   = 6.4;   // reaches a little into the wall (a flush touch breaks the mesh)
peg_d    = 2.2;   // pegs through the corner holes of the board
boss_d   = 5.0;   // lid bosses that press the board down (clear of the back shell wall)
split_h  = 3.3;   // the back shell reaches this far above the board

// ---------------- snap fit ----------------
tab_w    = 16;    // four spring tabs on the long sides
tab_t    = 1.2;
tab_y    = [20, 66];
tab_c    = 0.3;   // tab to wall
bead     = 0.6;   // bead on the tab; the tab bends (bead - tab_c) to snap in or out
notch_w  = 14;    // opening notch at the bottom edge

// ---------------- display ----------------
win_c = [36.75, 76.2];   // centre of the visible area
win   = [24.0, 13.0];    // window at the inside of the panel (widens outwards)

// ---------------- buttons, pressed directly through the lid ----------------
buttons = [[35.1, 54.8], [45.2, 44.6], [35.1, 33.3], [24.4, 45.0], [35.1, 45.0]];
body_sq  = 6.0;          // 6 x 6 mm tact switch body
floor_w  = 1.2;          // material under the funnels
ring     = 0.4;          // flat rim around each switch hole
disp_y   = 60.5;         // nothing of the D-pad beyond this: the display module starts at ~61

// ---------------- openings ----------------
usb_esp = 14.5;          // ESP32 USB-C on the left edge, Y of its centre
usb_chg = 13.5;          // charger USB-C on the right edge, Y of its centre
usb_w   = 13.0;          // wide enough for the plug housing
usb_d   = 9.0;           // window reaches this far below the board
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
z_split = board_t + split_h;           // where back shell and lid meet
z_in    = board_t + front_h;           // inside of the front panel
z_top   = z_in + top_t;
z_tab   = board_t + 0.4;               // lower end of the tabs, just above the board
z_bead  = (z_tab + z_split) / 2;       // bead and groove height

module rrect(x, y, w, h, r) {
  translate([x + r, y + r]) offset(r = r) square([w - 2 * r, h - 2 * r]);
}
module outer2d() { rrect(x0, y0, W, H, corner_r); }
module inner2d() { rrect(-gap, -gap, board_w + 2 * gap, board_h + 2 * gap, inner_r); }

// ---------------- back shell ----------------
module bottom() {
  difference() {
    union() {
      difference() {
        translate([0, 0, z_floor]) linear_extrude(z_split - z_floor) outer2d();
        translate([0, 0, -back_h]) linear_extrude(back_h + split_h + board_t + 1) inner2d();
      }
      for (h = holes) {
        translate([h[0], h[1], -back_h - 0.01]) cylinder(d = post_d, h = back_h + 0.01);
        translate([h[0], h[1], -0.01]) cylinder(d = peg_d, h = board_t + 0.8);   // through the board
      }
      battery_ribs();
    }
    groove();
    for (s = [[x0 - 1, usb_esp], [board_w + gap - 0.5, usb_chg]])   // USB-C windows
      translate([s[0], s[1] - usb_w / 2, -usb_d]) cube([wall + gap + 1.5, usb_w, usb_d - 0.4]);
    translate([switch_x, board_h, switch_z]) rotate([-90, 0, 0])      // toggle switch
      linear_extrude(gap + wall + 2) teardrop(switch_d);
    translate([board_w / 2 - notch_w / 2, y0 - 1, z_split - 1.2]) cube([notch_w, wall + 1.5, 2]);
    translate([buzzer_c[0], buzzer_c[1], z_floor - 1]) {              // sound holes
      cylinder(d = 1.6, h = floor_t + 2);
      for (a = [0 : 60 : 300]) rotate(a) translate([3.2, 0, 0]) cylinder(d = 1.6, h = floor_t + 2);
    }
  }
}

// a round hole in a side wall with a 45 degree roof: prints without support
module teardrop(d) {
  rotate(180) union() {
    circle(d = d);
    rotate(45) square(d / 2);
  }
}

// V groove all around the inside of the back shell, the tab beads click in
module groove() {
  g = bead + 0.1;
  hull() {
    translate([0, 0, z_bead - 1.0]) linear_extrude(0.01) inner2d();
    translate([0, 0, z_bead]) linear_extrude(0.01) offset(delta = g) inner2d();
  }
  hull() {
    translate([0, 0, z_bead]) linear_extrude(0.01) offset(delta = g) inner2d();
    translate([0, 0, z_bead + 1.0]) linear_extrude(0.01) inner2d();
  }
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
        translate([0, 0, z_split]) linear_extrude(z_top - z_split) outer2d();
        translate([0, 0, z_split - 1]) linear_extrude(z_in - z_split + 1) inner2d();
      }
      for (h = holes) translate([h[0], h[1], board_t]) cylinder(d = boss_d, h = front_h + 0.01);
      for (x = [0, 1], y = tab_y) tab(x, y);
      pocket_tub();
    }
    for (h = holes) translate([h[0], h[1], board_t - 0.01]) cylinder(d = peg_d + 0.7, h = 1.2);   // peg tips
    translate([board_w / 2 - notch_w / 2, y0 - 1, z_split - 0.01]) cube([notch_w, wall + 1.5, 1.2]);
    window();
    pocket_air();
    for (b = buttons) translate([b[0] - body_sq / 2 - 0.4, b[1] - body_sq / 2 - 0.4, board_t])
      cube([body_sq + 0.8, body_sq + 0.8, btn_h + 1]);          // the switch bodies pass through
  }
}

// spring tab on the left (side 0) or right (side 1) wall; hangs from the panel
// with a V bead near its lower end that clicks into the groove
module tab(side, yc) {
  xo = side ? board_w + gap - tab_c - tab_t : -gap + tab_c;      // tab x from the wall
  xb = side ? xo + tab_t : xo;                                    // outer face of the tab
  dir = side ? 1 : -1;
  translate([xo, yc - tab_w / 2, z_tab]) cube([tab_t, tab_w, z_in - z_tab + 0.01]);
  hull() {                                                         // the bead, 45 degrees both ways
    translate([xb - (side ? 0.01 : 0), yc - tab_w / 2 + 1, z_bead - bead]) cube([0.01, tab_w - 2, 2 * bead]);
    translate([xb + dir * bead - (side ? 0.01 : 0), yc - tab_w / 2 + 1, z_bead]) cube([0.01, tab_w - 2, 0.01]);
  }
}

// ---------------- funnels around the switches ----------------
// Every switch sits at the bottom of its own 45 degree funnel; the plunger is
// level with the funnel floor. Together the five funnels form a D-pad cross.
// Printed face down the funnel walls are 45 degree overhangs and the ridges
// between them are short, so no large bridge is left.
z_pf  = board_t + btn_h;                      // funnel floor, level with the plungers
fun_b = body_sq + 0.8 + 2 * ring;             // funnel width at the floor

module sq(c, w, z) { translate([c[0] - w / 2, c[1] - w / 2, z]) cube([w, w, 0.01]); }

module pocket_air() {
  intersection() {
    union() for (b = buttons) hull() {
      sq(b, fun_b, z_pf);
      sq(b, fun_b + 2 * (z_top + 0.5 - z_pf), z_top + 0.5);
    }
    translate([-10, -10, z_pf - 1]) cube([board_w + 20, disp_y - 1.2 + 10, 20]);   // straight on the display side
  }
}
module pocket_tub() {                          // the material that carries the funnels
  intersection() {
    union() for (b = buttons) hull() {
      sq(b, fun_b + 2 * floor_w, z_pf - floor_w);
      sq(b, fun_b + 2 * floor_w + 2 * (z_in - z_pf + floor_w), z_in);
    }
    translate([-10, -10, 0]) cube([board_w + 20, disp_y + 10, 20]);
  }
}

module window() {                                               // chamfered: wide view
  hull() {
    translate([win_c[0] - win[0] / 2, win_c[1] - win[1] / 2, z_in - 0.01]) cube([win[0], win[1], 0.01]);
    translate([win_c[0] - win[0] / 2 - top_t, win_c[1] - win[1] / 2 - top_t, z_top])
      cube([win[0] + 2 * top_t, win[1] + 2 * top_t, 0.01]);
  }
}

// ---------------- test plate ----------------
// 1.2 mm plate the size of the board: lay it on the front and check that
// the corner holes, the switches and the display window line up. The
// notches on the left and right edge mark where the USB-C sockets sit.
module testplate() {
  difference() {
    linear_extrude(1.2) square([board_w, board_h]);
    for (h = holes) translate([h[0], h[1], -1]) cylinder(d = peg_d + 0.4, h = 4);
    translate([win_c[0] - win[0] / 2, win_c[1] - win[1] / 2, -1]) cube([win[0], win[1], 4]);
    for (b = buttons) translate([b[0], b[1], -1]) linear_extrude(4) square(body_sq + 0.8, center = true);
    translate([0, usb_esp, 0.6]) rotate(45) cube([2.5, 2.5, 4], center = true);
    translate([board_w, usb_chg, 0.6]) rotate(45) cube([2.5, 2.5, 4], center = true);
    translate([buzzer_c[0], buzzer_c[1], -1]) cylinder(d = 3, h = 4);
  }
}

// ---------------- output ----------------
module bottom_print() { translate([0, 0, -z_floor]) bottom(); }                       // floor down
module lid_print()    { translate([0, board_h, z_top]) rotate([180, 0, 0]) lid(); }   // face down

module switches() {
  for (b = buttons) color("#222222") translate([b[0] - 3, b[1] - 3, board_t]) cube([6, 6, 3.5]);
  for (b = buttons) color("#111111") translate([b[0], b[1], board_t]) cylinder(d = 3.5, h = btn_h);
}

if (part == "bottom") bottom_print();
else if (part == "lid") lid_print();
else if (part == "testplate") testplate();
else if (part == "plate") {                     // everything for the case on one bed
  bottom_print();
  translate([W + 10, 0, 0]) lid_print();
}
else if (part == "assembled") {                 // closed case, for a look
  color("#3a3f4b") bottom();
  color("#2d8f5a") cube([board_w, board_h, board_t]);
  switches();
  color("#dfe3ea") lid();
}
else if (part == "all") {                       // exploded view
  color("#3a3f4b") bottom();
  color("#2d8f5a", 0.9) cube([board_w, board_h, board_t]);
  switches();
  color("#dfe3ea", 0.85) translate([0, 0, 22]) lid();
}
