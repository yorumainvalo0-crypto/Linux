// MiniArcade photo plate: a 1:1 measuring board to put a build on and take
// photos of, so the case can be designed from real sizes. Same plate for
// every build, so photos of different builds compare.
//
// Seen from above, the board area (70 x 90 mm, the perfboard) starts at
// the marked origin; all rulers count from there, so a reading is the
// board coordinate the case uses (X right, Y up, like miniarcade_case.scad).
//
// Two colours (Creality CFS): every part with marks comes as a "base" and a
// "marks" STL - load both as one object with two parts, base light, marks
// dark. The marks are inlaid flush into the top 0.6 mm, so the surface is
// flat and nothing casts shadows.
//
// Plug-ins (one colour):
//   ruler  - height ruler that stands in one of the two slots; 0 = plate top
//   stop   - L-shaped corner stop: push the board into the corner at 0/0;
//            held by two of the short pins ("stoppin")
//   peg    - pins for the four corner holes of the board (holds it in place)
//
// Render:  openscad -D 'part="plate_base"' -o photoplate_base.stl photoplate.scad
// parts:   plate_base, plate_marks, ruler_base, ruler_marks, stop, stoppin, peg, all

part = "all";

// ---------------- plate ----------------
board = [70, 90];         // the area the rulers count from
M     = 22;               // margin around it: ticks, numbers, slots
W     = board[0] + 2 * M; // 114
D     = board[1] + 2 * M; // 134
T     = 3.0;              // plate thickness
R     = 4;                // corner radius
inlay = 0.6;              // depth of the coloured marks (3 layers of 0.2)
holes = [[2.5, 3.2], [67.5, 3.2], [2.5, 86.8], [67.5, 86.8]];   // board corner holes
hole_d = 3.0;             // for the pegs

// ticks: every mm, longer every 5 and 10 mm
tick_w  = [0.5, 0.6, 0.7];      // 1 / 5 / 10 mm ticks (wider than the 0.4 nozzle)
tick_l  = [2.0, 3.5, 5.0];
font    = "DejaVu Sans:style=Bold";
num_s   = 3.2;                  // digit height

// slots for the height ruler (centre, long side along X or Y)
slot    = [10.4, 3.25];         // the ruler's tongue is 10 x 3
slot_a  = [M - 5.5, M + 45];    // left margin, beside the middle of the board: ruler faces the bottom edge
slot_b  = [M + 35, M - 5.5];    // bottom margin, below the middle: ruler faces the side

// corner stop pegs
stop_pegs = [[M + 18, M - 3.5], [M - 3.5, M + 18]];

// ---------------- height ruler ----------------
ruler_len = 60;           // 0..55 marked
ruler_w   = 12;
ruler_t   = 3;
tongue    = [T, 10];      // into the slot: as long as the plate is thick

$fn = 40;

// ---------------- helpers ----------------
module rounded(size, r) {
  offset(r) offset(-r) square(size);
}

function tick_kind(k) = k % 10 == 0 ? 2 : (k % 5 == 0 ? 1 : 0);

// one tick at position p along an edge, pointing inwards
module tick(k) {
  i = tick_kind(k);
  translate([-tick_w[i] / 2, 0]) square([tick_w[i], tick_l[i]]);
}

module num(n, s = num_s) {
  text(str(n), size = s, font = font, halign = "center", valign = "center");
}

// ---------------- plate marks (2D) ----------------
module plate_marks_2d() {
  // rulers on all four edges, counting from the board origin
  for (k = [-M + 12 : board[0] + M - 12]) {
    x = M + k;
    translate([x, 0]) tick(k);                          // bottom edge
    translate([x, D]) mirror([0, 1]) tick(k);           // top edge
  }
  for (k = [-M + 12 : board[1] + M - 12]) {
    y = M + k;
    translate([0, y]) rotate(-90) mirror([1, 0]) tick(k);   // left edge
    translate([W, y]) rotate(90) mirror([1, 0]) tick(k);    // right edge
  }
  // numbers every 10 mm, upright as seen from the bottom edge
  for (k = [0 : 10 : board[0]]) {
    translate([M + k, 8]) num(k);
    translate([M + k, D - 8]) num(k);
  }
  for (k = [0 : 10 : board[1]]) {
    translate([8.5, M + k]) num(k);
    translate([W - 8.5, M + k]) num(k);
  }
  // the board area and a 10 mm grid inside it
  translate([M, M]) difference() {
    square(board);
    translate([0.6, 0.6]) square(board - [1.2, 1.2]);
  }
  for (i = [1 : board[0] / 10 - 1]) translate([M + 10 * i - 0.25, M]) square([0.5, board[1]]);
  for (i = [1 : board[1] / 10 - 1]) translate([M, M + 10 * i - 0.25]) square([board[0], 0.5]);
  // origin: an arrow for X and one for Y
  translate([M, M]) {
    translate([3, -3.2]) arrow(12);
    translate([-3.2, 3]) rotate(90) mirror([0, 1]) arrow(12);
  }
  translate([M + 21.5, M - 3.2]) text("X", size = 2.6, font = font, valign = "center");
  translate([M - 3.2, M + 21.5]) text("Y", size = 2.6, font = font, halign = "center");
  // corner marks to straighten a slanted photo; the square one is the origin corner
  for (c = [[6, 6], [W - 6, 6], [6, D - 6], [W - 6, D - 6]])
    translate(c) difference() {
      if (c == [6, 6]) square(7, center = true); else circle(d = 7);
      circle(d = 1.6);
    }
  // name and scale
  translate([W / 2, D - 16.5]) text("MiniArcade  1:1  mm", size = 3, font = font, halign = "center", valign = "center");
}

module arrow(len) {
  translate([0, -0.35]) square([len - 2.5, 0.7]);
  translate([len - 3, 0]) polygon([[0, -1.4], [3, 0], [0, 1.4]]);
}

// everything that goes through the plate
module plate_holes_2d() {
  for (h = holes) translate([M, M] + h) circle(d = hole_d);
  translate(slot_a) square(slot, center = true);
  translate(slot_b) square([slot[1], slot[0]], center = true);
  for (p = stop_pegs) translate(p) circle(d = hole_d);
}

module plate_base() {
  difference() {
    linear_extrude(T) rounded([W, D], R);
    translate([0, 0, T - inlay]) linear_extrude(inlay + 1) plate_marks_2d();
    translate([0, 0, -1]) linear_extrude(T + 2) plate_holes_2d();
  }
}

module plate_marks() {
  translate([0, 0, T - inlay]) linear_extrude(inlay)
    intersection() {
      difference() { plate_marks_2d(); plate_holes_2d(); }
      rounded([W, D], R);
    }
}

// ---------------- height ruler ----------------
// Printed flat, marks up. Standing in a slot, x is the height above the
// plate top and the marks face the camera.
module ruler_marks_2d() {
  for (k = [0 : ruler_len - 5]) {
    i = tick_kind(k);
    x0 = k == 0 ? 0 : k - tick_w[i] / 2;
    translate([x0, 0]) square([tick_w[i], tick_l[i] + 1]);
  }
  for (k = [10 : 10 : ruler_len - 10])
    translate([k, 9]) rotate(-90) num(k, 3.0);
  translate([ruler_len - 4, 9]) rotate(-90) text("mm", size = 2.4, font = font, halign = "center", valign = "center");
}

module ruler_base() {
  difference() {
    union() {
      cube([ruler_len, ruler_w, ruler_t]);
      translate([-tongue[0], (ruler_w - tongue[1]) / 2, 0]) cube([tongue[0] + 0.01, tongue[1], ruler_t]);
    }
    translate([0, 0, ruler_t - inlay]) linear_extrude(inlay + 1) ruler_marks_2d();
  }
}

module ruler_marks() {
  translate([0, 0, ruler_t - inlay]) linear_extrude(inlay)
    intersection() { ruler_marks_2d(); square([ruler_len, ruler_w]); }
}

// ---------------- corner stop ----------------
// An L that sits in the bottom-left corner of the board area: a foot on
// the plate, a wall the board edges push against. Printed foot down; two
// pins go through the foot into the plate.
stop_leg  = 26;
stop_foot = 6;
stop_wall = 2;
stop_h    = 6;
foot_t    = 2;

module stop() {
  difference() {
    union() {
      linear_extrude(foot_t) union() {                 // foot, outside the board area
        translate([-stop_foot, -stop_foot]) square([stop_leg + stop_foot, stop_foot]);
        translate([-stop_foot, -stop_foot]) square([stop_foot, stop_leg + stop_foot]);
      }
      linear_extrude(stop_h) union() {                 // wall, inner faces on the board edges
        translate([-stop_wall, -stop_wall]) square([stop_leg + stop_wall, stop_wall]);
        translate([-stop_wall, -stop_wall]) square([stop_wall, stop_leg + stop_wall]);
      }
    }
    for (p = stop_pegs) translate(p - [M, M]) translate([0, 0, -1]) cylinder(d = hole_d, h = foot_t + 2);
  }
}

// through the foot into the plate, head on top
module stoppin() {
  cylinder(d = 5, h = 1);                                     // head (printed down)
  translate([0, 0, 1]) cylinder(d = hole_d - 0.15, h = foot_t + T - 0.3);
}

// ---------------- pegs for the board's corner holes ----------------
// printed as it stands: the collar widens at 45 degrees, so no supports
module peg() {
  cylinder(d = hole_d - 0.15, h = T);                                  // into the plate
  translate([0, 0, T]) cylinder(d1 = hole_d - 0.15, d2 = 5, h = 1.1);  // 45 degree flare
  translate([0, 0, T + 1.1]) cylinder(d = 5, h = 0.4);                 // the board rests on it
  translate([0, 0, T + 1.5]) cylinder(d = 2.1, h = 2.5);               // through the board hole
}

// ---------------- output ----------------
if (part == "plate_base")  plate_base();
if (part == "plate_marks") plate_marks();
if (part == "ruler_base")  ruler_base();
if (part == "ruler_marks") ruler_marks();
if (part == "stop")        stop();
if (part == "stoppin")     for (i = [0 : 1]) translate([i * 8, 0, 0]) stoppin();
if (part == "peg")         for (i = [0 : 3]) translate([i * 8, 0, 0]) peg();
if (part == "plate") { color("white") plate_base(); color("black") plate_marks(); }
if (part == "all") {
  color("white") plate_base();
  color("black") plate_marks();
  // ruler standing in slot A (marks towards the bottom edge), stop in the corner, four pegs
  multmatrix([[0, -1, 0, slot_a[0] + ruler_w / 2], [0, 0, -1, slot_a[1] + ruler_t / 2], [1, 0, 0, T], [0, 0, 0, 1]]) {
    color("white") ruler_base();
    color("black") ruler_marks();
  }
  color("orange") translate([M, M, T]) stop();
  for (p = stop_pegs) color("red") translate([p[0], p[1], T + foot_t + 1]) rotate([180, 0, 0]) stoppin();
  for (h = holes) color("orange") translate([M, M] + h) translate([0, 0, 0]) peg();
}
