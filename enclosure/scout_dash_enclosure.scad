// =====================================================================
//  Scout 80 dash — Waveshare ESP32-S3-Touch-LCD-7 enclosure mods
//  Base case: VaAndCob/ESP32-S3-Mastermind  waveshare7_3D/waveshare_7_case.stl (MIT)
//
//  Parts (pick with PART below, F6 render, then File > Export > STL):
//    "case"     original back shell + front bezel flange + front-lip relief
//               + 4 captive-nut bosses inside the floor for bolting the box on
//    "box"      board box (v8: mirrored, wire windows both sides, no drum, SMA through the plain wall): GPS + IMU pockets, cable bay with SMA bulkhead,
//               wire windows in both bay side walls, 4 bolt-down feet
//    "lid"      lid for the box (4x M3 self-tapping)
//    "clamps"   two separate clamp bars (one STL, print both)
//    "pod"      GPS patch-antenna pod (only if you use the ceramic patch)
//    "assembly" everything in place, with a ghost dash panel (preview only)
//
//  Bench: M3 nuts into the case bosses, standoffs in, boards + SMA pigtail into the box,
//         lid on, box bolted to the case back (4x M3).
//  Car:   slide case in from the front, drop each clamp bar over its two standoffs,
//         2 screws per bar. Box and SMA stay reachable between the bars.
//
//  Coordinate frame = original STL:
//    X = width (-98.5..98.5), Y = height (0..115), Z = depth.
//    Z = -0.5 is the case back face, Z = 18.5 is the front (glass) face.
//  All dimensions in mm.
// =====================================================================

PART = "assembly";            // "case" | "tray" | "carrier" | "pod" | "assembly"
CASE_STL = "waveshare_7_case.stl";

$fn = 48;

// ---------------- dash panel & cut-out ----------------
panel_t        = 1.5;   // dash panel thickness at the opening (stock Scout steel ~1.5)
cut_clear      = 1.5;   // clearance per side between case and the dash cut-out
                        //   -> dash cut-out: keep the existing 203.2 mm (8 in) width, open the height to 118 mm, R12 corners

// ---------------- front bezel flange ----------------
flange_w       = 8;     // how far the bezel extends past the case all round
flange_t       = 3;     // bezel thickness (front face stays flush with case front)
flange_chamfer = 1.5;   // 45° chamfer on the outer front edge
flange_overlap = 0.6;   // how far the flange fuses into the case wall

// ---------------- case outline (measured from the STL) ----------------
case_w   = 197;  case_h = 115;  case_r = 10.5;
case_z0  = -0.5; case_z1 = 18.5;

// ---------------- existing board-screw holes (measured, Ø4 = M3 clearance) ----
holes = [[-61.6, 25.07], [64.6, 25.07], [-61.6, 90.73], [64.6, 90.73]];
// The four stock board screws are replaced by M3 male-female hex standoffs:
// the male end screws into the board through the case (holding the board in the
// case exactly like the stock screw), the hex body sticks out of the case back and
// is the locating pin + threaded anchor for the carrier.
standoff_len   = 6;     // hex body length: 6 for 'M3 x 6+6', 8 for 'M3 x 8+6'.
                        //   Male stud must be >= 5-6 mm: 2 mm case floor + ~4 mm into the board's standoffs.
standoff_af    = 5.5;   // hex across flats (M3 standard)
socket_clear   = 0.35;  // diametral clearance around the hex corners
socket_extra   = 0.5;   // socket depth beyond the standoff end
lead_in        = 1.0;   // 45° chamfer at the socket mouth so it finds the pin by feel
screw_hole_d   = 3.0;   // 3.0 = M3 screw self-threads into the print and stays captive;
                        //   3.4 = free clearance

// ---------------- carrier / retainer ----------------
rail_w        = 12;     // width of the two vertical rails (centred on the screw holes)
back_web      = 3.5;    // carrier material under the screw head (counterbored if thicker)
cover_roof    = 2.5;    // carrier material over the board cavity
foot_reach    = 9.5;    // foot length along Y, beyond the cut-out edge
foot_margin   = 3;      // gap between foot inner face and the cut-out edge
preload       = 0.3;    // feet stop this short of the panel so tightening clamps it
bridge_y      = [45.5, 77.5]; // left rail arches over the side port window (y 48..75)
bridge_gap    = 4;      // clearance under that arch for USB plug boots
bar_y0        = 73.5;   // crossbar bottom edge (clears the floor slot at x 44..51.5)
bar_y0_left   = 76;     // ...and left of x = -27 (clears the big port window, y <= 75)
bar_y1        = 105;    // top edge (clears the open recess at x -15..24, y >= 106.5)

// ---------------- board tray ----------------
gps_pcb       = [36.45, 26.10]; // measured (re-checked); long side along X, 4-pin header end toward the wiring hole
box_xc        = 2.3375;           // box centre line (set mirror_layout = true to swap GPS/IMU sides)
mirror_layout = true;    // v8: GPS on the right, IMU on the left
gps_c         = [mirror_layout ? 2*box_xc + 11 : -11, 86.5];    // pocket centre on the case back (X,Y)
imu_pcb       = [15.80, 20.55]; // measured, turned 90°: 8-pin header edge faces the wiring hole
imu_c         = [mirror_layout ? 2*box_xc - 26 : 26, 86.5];
pcb_t         = 1.6;
pcb_clear     = 0.8;    // total XY clearance in each pocket (0.4 per side; FDM pockets print undersize)
ledge         = 1.5;    // PCB seat width around the pocket
tray_base     = 1.0;    // floor under the relief
relief        = 2.0;    // space under each PCB for solder joints (solder wires, not header pins)
lip_above     = 0.8;    // wall height above the PCB top face
snap          = 0.45;   // snap-lip overhang that holds each PCB in
tray_wall     = 2.5;
vhb_t         = 1.1;    // tray-to-case bond thickness (3M 5952 VHB ~1.1; 0 for epoxy/CA)
comp_clear    = 6.0;    // headroom above the PCB tops (NEO-6M, backup cell, u.FL + plug)
cover_clear   = 1.0;    // XY clearance between tray and the carrier's cover cavity
pad_gap       = 0.2;    // carrier pads stop this far off the tray walls

// ---------------- wiring ----------------
// Wires leave through the case's existing connector cutouts (UART1 / I2C), so there is
// no hole in the case floor. Both boards' header ends face a shared channel between the
// pockets, which exits through the tray's lower edge and a notch in the carrier.
case_cable_hole = false;        // legacy floor hole (off)
cable_hole_c    = [6.5, 86.5];  // only used if case_cable_hole = true
cable_hole_d    = 8;
wire_x          = mirror_layout ? 2*box_xc - 12.75 : 12.75;   // shared wire channel / exit between the pockets
wire_gap        = 10;           // openings in the pocket walls facing the channel
exit_w          = 9;            // width of the exit through the tray's lower wall
notch_h         = 4;            // height of the carrier notch the wires pass under
coax_exit       = true;         // channel from the cavity to the carrier top edge for the GPS coax
chan_w          = 5;  chan_d = 2.5;

// ---------------- antenna pod ----------------
patch         = [25, 25, 4.5]; // ceramic patch incl. adhesive; some are 8 mm thick
pod_wall      = 2;
pod_base      = 2;
pod_ear       = 9;             // screw ears each side (M3), set 0 for tape-only

// =====================================================================

module outline2d(grow = 0) {
    r = case_r + grow;
    translate([-case_w/2 + case_r, case_r])
        offset(r = r) square([case_w - 2*case_r, case_h - 2*case_r]);
}

module rrect(sz, r) {
    translate([r, r]) offset(r = r) square([sz[0] - 2*r, sz[1] - 2*r]);
}

// ---------------- case + flange ----------------
module flange() {
    z1 = case_z1;
    z0 = z1 - flange_t;
    difference() {
        hull() {
            translate([0, 0, z0]) linear_extrude(flange_t - flange_chamfer)
                outline2d(flange_w);
            translate([0, 0, z0]) linear_extrude(flange_t)
                outline2d(flange_w - flange_chamfer);
        }
        translate([0, 0, z0 - 1]) linear_extrude(flange_t + 2)
            outline2d(-flange_overlap);
    }
}

// front-lip relief: the glass recess measured 192.95 x 111.0 mm, 1.2 mm deep (z 17.3..18.5)
lip_open   = [192.95, 111.0];
lip_ctr    = [0.025, 57.55];
lip_relief = [0.35, 0.35];   // extra clearance per side, X and Y
lip_z0     = 17.0;           // a little below the recess floor so the step corner is cleared too
module case_mod() {
    difference() {
        union() {
            import(CASE_STL, convexity = 10);
            flange();
            for (f = feet) translate([f[0], f[1], 1.4]) cylinder(d = boss_d, h = boss_h + 0.1);
        }
        // bolt-down holes and nut pockets (nut drops in from the inside, before the board goes in)
        for (f = feet) translate([f[0], f[1], 0]) {
            translate([0, 0, case_z0 - 1]) cylinder(d = foot_hole, h = 2 + boss_h + 2);
            translate([0, 0, 1.5 + boss_h - nut_h]) rotate([0, 0, 30]) cylinder(d = nut_af / cos(30), h = nut_h + 1, $fn = 6);
        }
        translate([lip_ctr[0] - lip_open[0]/2 - lip_relief[0], lip_ctr[1] - lip_open[1]/2 - lip_relief[1], lip_z0])
            linear_extrude(case_z1 - lip_z0 + 1)
                rrect([lip_open[0] + 2*lip_relief[0], lip_open[1] + 2*lip_relief[1]], 8.5);
        if (case_cable_hole) translate([cable_hole_c[0], cable_hole_c[1], case_z0 - 1])
            cylinder(d = cable_hole_d, h = 4);
    }
}

// ---------------- tray ----------------
function pk(pcb) = [pcb[0] + pcb_clear, pcb[1] + pcb_clear];
tray_h  = tray_base + relief + pcb_t + lip_above;
tray_x0 = min(gps_c[0] - pk(gps_pcb)[0]/2, imu_c[0] - pk(imu_pcb)[0]/2) - tray_wall;
tray_x1 = max(gps_c[0] + pk(gps_pcb)[0]/2, imu_c[0] + pk(imu_pcb)[0]/2) + tray_wall;
tray_y0 = min(gps_c[1] - pk(gps_pcb)[1]/2, imu_c[1] - pk(imu_pcb)[1]/2) - tray_wall;
tray_y1 = max(gps_c[1] + pk(gps_pcb)[1]/2, imu_c[1] + pk(imu_pcb)[1]/2) + tray_wall;
tray_z0 = case_z0 - vhb_t;               // tray face that bonds to the case
tray_z_top = tray_z0 - tray_h;           // open (rear) face of the tray

// tray modelled in its print orientation: base on z = 0, pockets open upward
module tray_pocket(c, pcb, toward_hole) {
    P = pk(pcb);
    translate([c[0] - tray_x0, c[1] - tray_y0, 0]) {
        // PCB pocket from the seat up
        translate([-P[0]/2, -P[1]/2, tray_base + relief]) cube([P[0], P[1], tray_h]);
        // relief under the PCB
        translate([-P[0]/2 + ledge, -P[1]/2 + ledge, tray_base]) cube([P[0] - 2*ledge, P[1] - 2*ledge, relief + 0.01]);
        // wire opening toward the hole (full height, down to the floor)
        translate([toward_hole > 0 ? P[0]/2 - 1 : -P[0]/2 - tray_wall - 1, -wire_gap/2, tray_base])
            cube([tray_wall + 2, wire_gap, tray_h]);
        // fingernail notch on the far end
        translate([toward_hole > 0 ? -P[0]/2 : P[0]/2, 0, tray_base + relief]) cylinder(d = 8, h = tray_h);
    }
}

module tray_snaps(c, pcb) {
    // small lips on the two long walls, just above the PCB top
    P = pk(pcb);
    along_x = P[0] >= P[1];
    L = along_x ? P[0] : P[1];
    for (s = [-1, 1])
        translate([c[0] - tray_x0, c[1] - tray_y0, tray_base + relief + pcb_t + 0.1])
            rotate([0, 0, along_x ? 0 : 90])
                translate([-L/4, s * ((along_x ? P[1] : P[0])/2) - (s > 0 ? snap : 0), 0])
                    hull() {
                        cube([L/2, snap, 0.01]);
                        translate([0, s > 0 ? snap : 0, lip_above - 0.11]) cube([L/2, 0.01, 0.01]);
                    }
}

module tray() {
    W = tray_x1 - tray_x0;  H = tray_y1 - tray_y0;
    difference() {
        linear_extrude(tray_h) rrect([W, H], 2);
        tray_pocket(gps_c, gps_pcb, sign(wire_x - gps_c[0]));
        tray_pocket(imu_c, imu_pcb, sign(wire_x - imu_c[0]));
        // wiring hole through the tray floor, lined up with the case hole
        // wire channel joining both pockets to the hole
        // (floor stays solid: it seals any old hole in the case behind the tray)
        cx0 = gps_c[0] + pk(gps_pcb)[0]/2 - 1;
        cx1 = imu_c[0] - pk(imu_pcb)[0]/2 + 1;
        translate([cx0 - tray_x0, gps_c[1] - tray_y0 - wire_gap/2, tray_base])
            cube([cx1 - cx0, wire_gap, tray_h]);
        // exit straight down through the tray's lower wall
        translate([wire_x - exit_w/2 - tray_x0, -1, tray_base])
            cube([exit_w, gps_c[1] - tray_y0 + 1, tray_h]);
    }
    tray_snaps(gps_c, gps_pcb);
    tray_snaps(imu_c, imu_pcb);
}

// tray as installed on the case back (for the assembly view / checks)
module tray_installed() {
    translate([tray_x0, tray_y0, tray_z0]) mirror([0, 0, 1]) tray();
}

// ---------------- carrier ----------------
cut_y0 = -cut_clear;
cut_y1 = case_h + cut_clear;
panel_back_z = case_z1 - flange_t - panel_t;      // Z of the panel's rear face
foot_z1 = panel_back_z - preload;
foot_in0 = cut_y0 - foot_margin;                  // inner faces of the feet
foot_in1 = cut_y1 + foot_margin;
rail_y0  = foot_in0 - foot_reach;
rail_y1  = foot_in1 + foot_reach;

cav_depth = vhb_t + tray_h - lip_above + comp_clear;   // from the case back
rail_t   = max(10, standoff_len + socket_extra + back_web, cav_depth + cover_roof);
bar_t    = rail_t;                                  // flat back for printing
cbore_d  = 6.5;
cbore_h  = rail_t - (standoff_len + socket_extra) - back_web;   // keeps M3 x 8 screws

cav_x0 = tray_x0 - cover_clear;  cav_x1 = tray_x1 + cover_clear;
cav_y0 = tray_y0 - cover_clear;  cav_y1 = tray_y1 + cover_clear;
bar2_x0 = cav_x0 - 2;  bar2_y0 = cav_y0 - 2;     // lower crossbar section around the cavity

module carrier() {
    xs = [holes[0][0], holes[1][0]];           // rail centre lines
    difference() {
        union() {
            // two rails
            for (x = xs) translate([x - rail_w/2, rail_y0, case_z0 - rail_t])
                cube([rail_w, rail_y1 - rail_y0, rail_t]);
            // feet
            for (x = xs) for (end = [0, 1]) {
                y_in  = end == 0 ? foot_in0 : foot_in1;
                y_out = end == 0 ? rail_y0  : rail_y1;
                translate([x - rail_w/2, min(y_in, y_out), case_z0 - rail_t])
                    cube([rail_w, abs(y_out - y_in), foot_z1 - case_z0 + rail_t]);
            }
            // crossbar (cover)
            translate([xs[0] + rail_w/2 - 0.01, bar_y0_left, case_z0 - bar_t])
                cube([xs[1] - xs[0] - rail_w + 0.02, bar_y1 - bar_y0_left, bar_t]);
            translate([-27, bar_y0, case_z0 - bar_t])
                cube([xs[1] - rail_w/2 + 27 + 0.01, bar_y1 - bar_y0, bar_t]);
            translate([bar2_x0, bar2_y0, case_z0 - bar_t])
                cube([cav_x1 + 2 - bar2_x0, bar_y1 - bar2_y0, bar_t]);
        }
        // arch over the side port window
        translate([xs[0] - rail_w/2 - 1, bridge_y[0], case_z0 - bridge_gap])
            cube([rail_w + 2, bridge_y[1] - bridge_y[0], bridge_gap + 1]);
        // standoff sockets, screw holes, counterbores
        for (h = holes) translate([h[0], h[1], 0]) {
            sock_d = standoff_af / cos(30) + socket_clear;
            sock_h = standoff_len + socket_extra;
            translate([0, 0, case_z0 - bar_t - 1]) cylinder(d = screw_hole_d, h = bar_t + 2);
            translate([0, 0, case_z0 - sock_h]) cylinder(d = sock_d, h = sock_h + 1);
            translate([0, 0, case_z0 - lead_in])
                cylinder(d1 = sock_d, d2 = sock_d + 2*lead_in + 0.02, h = lead_in + 0.01);
            if (cbore_h > 0.2)
                translate([0, 0, case_z0 - bar_t - 1]) cylinder(d = cbore_d, h = cbore_h + 1);
        }
        // cover cavity over the tray, leaving 4 corner pads that bear on the tray walls
        difference() {
            translate([cav_x0, cav_y0, case_z0 - cav_depth]) cube([cav_x1 - cav_x0, cav_y1 - cav_y0, cav_depth + 1]);
            pad_z = tray_z_top - pad_gap;   // pad faces stop just short of the tray rim
            for (px = [tray_x0, tray_x1 - 6]) for (py = [tray_y0, tray_y1 - 6])
                translate([px, py, case_z0 - cav_depth - 1]) cube([6, 6, pad_z - (case_z0 - cav_depth - 1)]);
        }
        // wire exit: notch through the cavity's lower wall, toward the connector cutouts
        translate([wire_x - exit_w/2, bar2_y0 - 1, case_z0 - notch_h])
            cube([exit_w, cav_y0 - bar2_y0 + 2, notch_h + 1]);
        // coax exit from the cavity to the top edge
        if (coax_exit) translate([gps_c[0] - 12 - chan_w/2, cav_y1 - 1, case_z0 - chan_d])
            cube([chan_w, bar_y1 - cav_y1 + 2, chan_d + 1]);
    }
}

// ---------------- v5: standalone board box + lid, separate clamp bars ----------------
// The box is the tray with walls carried up to full height and a screw-on lid. It is
// stuck to the case back with VHB and sits in the open between the two clamp bars,
// so the GPS, IMU and antenna lead stay reachable without removing anything.
box_h     = tray_base + relief + pcb_t + comp_clear;   // wall top above the box floor
lid_t     = 2.0;
lip_h     = 2.0;     // lid lip that drops inside the walls
lip_w     = 1.2;
lid_clear = 0.3;     // gap between lip and wall
ear_d     = 8;       // lid screw ears on the two short ends
ear_out   = 2.5;     // ear centre this far outside the wall
pilot_d   = 2.5;     // M3 self-tapping pilot (use 2.2 for M2.5)
win_w     = 11;      // wire window in the lower wall: passes a JST-PH 4-pin plug
win_h     = 6;
coax_w    = 4;       // antenna-lead notches in the upper wall (closed over by the lid)
coax_d    = 3;
coax_xs   = [gps_c[0] - 12, gps_c[0] + 12];
clamp_t   = max(10, standoff_len + socket_extra + back_web);

module box_outline(grow = 0) { offset(delta = grow) rrect([tray_x1 - tray_x0, tray_y1 - tray_y0], 2); }
function ear_pos() = [[-ear_out, (tray_y1 - tray_y0)/2], [tray_x1 - tray_x0 + ear_out, (tray_y1 - tray_y0)/2]];

// ---- v6: cable bay below the boards with an SMA bulkhead in its lower wall ----
bay_len    = 28;      // bay extends this far below the board pockets (toward the bottom of the display)
box_h6     = 14;      // wall top above the box floor (was box_h = 10.6): room for the SMA hex
bay_floor  = 1.5;
sma_x      = mirror_layout ? 2*box_xc - 5 : 5;   // SMA axis X on the case back
sma_z      = 7.5;     // SMA axis height above the box floor
sma_hole_d = 6.5;     // 1/4"-36 thread
sma_flat   = 5.8;     // D-flat across (set = sma_hole_d for a round hole)
sma_hex_af = 8.3;     // inside hex flange pocket, stops the connector turning while you tighten the nut
sma_boss_t = 0;    sma_hex_d = 2.0;   // v8: no boss, the SMA goes through the plain wall (more thread outside)
bay_win    = [11, 6]; // wire windows in both side walls of the bay, same height and position
// (v7: cable drum removed: the RG178 lead loops loosely in the bay)
// bolt-down: 4 feet on the box base, captive M3 nuts in bosses inside the case floor
feet       = [[-36.6, 98], [41.3, 98], [-26, 38.05], [30, 38.05]];   // case coords, all on plain 2 mm floor
foot_d     = 9;   foot_t = 3;   foot_hole = 3.4;
nut_af     = 5.65;          // M3 nut 5.5 AF: slight press fit so the nut stays put
nut_h      = 2.6;
boss_d     = 10;  boss_h = 3.6;   // above the inside floor face (z 1.5)
W6 = tray_x1 - tray_x0;  H6 = tray_y1 - tray_y0 + bay_len;
module box6_outline(grow = 0) { translate([0, -bay_len]) offset(delta = grow) rrect([W6, H6], 2); }
function sx(x) = x - tray_x0;              // case X -> box coords

module box6_base() {
    difference() {
        union() {
            tray();
            linear_extrude(box_h6) difference() { box6_outline(); box6_outline(-tray_wall); }
            linear_extrude(bay_floor) intersection() { box6_outline(); translate([0, -bay_len]) square([W6, bay_len + 0.01]); }
            for (e = ear_pos()) translate([e[0], e[1], 0]) cylinder(d = ear_d, h = box_h6);
            // internal lid-screw bosses in the bay's lower corners
            for (bx = [6, W6 - 6]) translate([bx, -bay_len + 6, 0]) cylinder(d = 7.5, h = box_h6);
            // SMA boss on the inside of the lower wall
            if (sma_boss_t > 0) translate([sx(sma_x) - 7.5, -bay_len + tray_wall - 0.01, 0]) cube([15, sma_boss_t, sma_z + 5]);
            // bolt-down feet
            for (f = feet) linear_extrude(foot_t) hull() {
                translate([sx(f[0]), f[1] - tray_y0]) circle(d = foot_d);
                translate([min(max(sx(f[0]), 2), W6 - 2), min(max(f[1] - tray_y0, -bay_len + 2), H6 - bay_len - 2)]) circle(d = foot_d);
            }
        }
        // SMA hole (D-flat on top) through wall + boss
        translate([sx(sma_x), -bay_len - 1, sma_z]) rotate([-90, 0, 0])
            intersection() {
                cylinder(d = sma_hole_d, h = tray_wall + sma_boss_t + 2);
                translate([-sma_hole_d/2, -sma_flat/2, 0]) cube([sma_hole_d, sma_hole_d, tray_wall + sma_boss_t + 2]);
            }
        // hex pocket for the bulkhead flange, on the inside face of the boss
        if (sma_boss_t > 0) translate([sx(sma_x), -bay_len + tray_wall + sma_boss_t - sma_hex_d, sma_z]) rotate([-90, 0, 0])
            rotate([0, 0, 30]) cylinder(d = sma_hex_af / cos(30), h = sma_hex_d + 1, $fn = 6);
        // wire windows in both side walls of the bay
        for (wx = [-1, W6 - tray_wall - 1])
            translate([wx, -bay_len/2 - bay_win[0]/2, bay_floor]) cube([tray_wall + 2, bay_win[0], bay_win[1]]);
        // lid screw pilots
        for (e = ear_pos()) translate([e[0], e[1], 1]) cylinder(d = pilot_d, h = box_h6);
        for (bx = [6, W6 - 6]) translate([bx, -bay_len + 6, 1]) cylinder(d = pilot_d, h = box_h6);
        for (f = feet) translate([sx(f[0]), f[1] - tray_y0, -1]) cylinder(d = foot_hole, h = foot_t + 2);
    }
}

module box6_lid() {
    difference() {
        union() {
            linear_extrude(lid_t) union() {
                box6_outline();
                for (e = ear_pos()) translate(e) circle(d = ear_d);
            }
            translate([0, 0, lid_t - 0.01]) linear_extrude(lip_h + 0.01) difference() {
                box6_outline(-(tray_wall + lid_clear));
                box6_outline(-(tray_wall + lid_clear + lip_w));
                for (bx = [6, W6 - 6]) translate([bx, -bay_len + 6]) circle(d = 9);
                translate([sx(sma_x) - 8.5, -bay_len]) square([17, tray_wall + sma_boss_t + 1.5]);
            }
        }
        for (e = ear_pos()) translate([e[0], e[1], -1]) cylinder(d = 3.4, h = lid_t + 2);
        for (bx = [6, W6 - 6]) translate([bx, -bay_len + 6, -1]) cylinder(d = 3.4, h = lid_t + 2);
    }
}
module box6_installed() { translate([tray_x0, tray_y0, case_z0 - vhb_t]) mirror([0, 0, 1]) box6_base(); }
module lid6_installed() { translate([tray_x0, tray_y0, case_z0 - vhb_t - box_h6 - lid_t]) mirror([0,0,1]) mirror([0,0,1]) box6_lid(); }

// print orientation: floor on the bed, opening up
module box_base() {
    difference() {
        union() {
            tray();
            translate([0, 0, tray_h - 0.01]) linear_extrude(box_h - tray_h + 0.01)
                difference() { box_outline(); box_outline(-tray_wall); }
            for (e = ear_pos()) translate([e[0], e[1], 0]) cylinder(d = ear_d, h = box_h);
        }
        // wire window through the lower wall, lined up with the tray's wire exit
        translate([wire_x - tray_x0 - win_w/2, -1, tray_base]) cube([win_w, tray_wall + 2, win_h]);
        // antenna-lead notches in the upper wall
        for (cx = coax_xs) translate([cx - tray_x0 - coax_w/2, tray_y1 - tray_y0 - tray_wall - 1, box_h - coax_d])
            cube([coax_w, tray_wall + 2, coax_d + 1]);
        // lid screw pilots
        for (e = ear_pos()) translate([e[0], e[1], 1]) cylinder(d = pilot_d, h = box_h);
    }
}

// print orientation: outer face on the bed, lip up
module box_lid() {
    difference() {
        union() {
            linear_extrude(lid_t) union() {
                box_outline();
                for (e = ear_pos()) translate(e) circle(d = ear_d);
            }
            translate([0, 0, lid_t - 0.01]) linear_extrude(lip_h + 0.01) difference() {
                box_outline(-(tray_wall + lid_clear));
                box_outline(-(tray_wall + lid_clear + lip_w));
            }
        }
        for (e = ear_pos()) translate([e[0], e[1], -1]) cylinder(d = 3.4, h = lid_t + 2);
        // gaps in the lip at the antenna-lead notches
        for (cx = coax_xs) translate([cx - tray_x0 - 3, tray_y1 - tray_y0 - tray_wall - lid_clear - lip_w - 1, lid_t])
            cube([6, lip_w + 2, lip_h + 1]);
    }
}

module box_installed() { translate([tray_x0, tray_y0, case_z0 - vhb_t]) mirror([0, 0, 1]) box_base(); }
module lid_installed() { translate([tray_x0, tray_y0, case_z0 - vhb_t - box_h - lid_t]) box_lid(); }

// two independent clamp bars, each located by two standoffs
module clamps() {
    xs = [holes[0][0], holes[1][0]];
    for (i = [0, 1]) let (x = xs[i]) difference() {
        union() {
            translate([x - rail_w/2, rail_y0, case_z0 - clamp_t]) cube([rail_w, rail_y1 - rail_y0, clamp_t]);
            for (end = [0, 1]) {
                y_in  = end == 0 ? foot_in0 : foot_in1;
                y_out = end == 0 ? rail_y0  : rail_y1;
                translate([x - rail_w/2, min(y_in, y_out), case_z0 - clamp_t])
                    cube([rail_w, abs(y_out - y_in), foot_z1 - case_z0 + clamp_t]);
            }
        }
        if (i == 0) translate([x - rail_w/2 - 1, bridge_y[0], case_z0 - bridge_gap])
            cube([rail_w + 2, bridge_y[1] - bridge_y[0], bridge_gap + 1]);
        for (h = holes) if (abs(h[0] - x) < 1) translate([h[0], h[1], 0]) {
            sock_d = standoff_af / cos(30) + socket_clear;
            sock_h = standoff_len + socket_extra;
            translate([0, 0, case_z0 - clamp_t - 1]) cylinder(d = screw_hole_d, h = clamp_t + 2);
            translate([0, 0, case_z0 - sock_h]) cylinder(d = sock_d, h = sock_h + 1);
            translate([0, 0, case_z0 - lead_in])
                cylinder(d1 = sock_d, d2 = sock_d + 2*lead_in + 0.02, h = lead_in + 0.01);
        }
    }
}

// ---------------- antenna pod ----------------
module pod() {
    in = [patch[0] + 0.6, patch[1] + 0.6];
    out = [in[0] + 2*pod_wall, in[1] + 2*pod_wall];
    h = pod_base + patch[2];
    difference() {
        union() {
            translate([-out[0]/2, -out[1]/2, 0]) linear_extrude(h) rrect(out, 3);
            if (pod_ear > 0) hull() for (s = [-1, 1])
                translate([s * (out[0]/2 + pod_ear/2), 0, 0]) cylinder(d = pod_ear, h = pod_base + 1);
        }
        // patch pocket (antenna face up)
        translate([-in[0]/2, -in[1]/2, pod_base]) cube([in[0], in[1], h]);
        // centre hole for the coax pigtail + exit slot under the base
        translate([0, 0, -1]) cylinder(d = 4, h = pod_base + 2);
        translate([-2, 0, -0.01]) cube([4, out[1], 1.2]);
        // ear screw holes
        if (pod_ear > 0) for (s = [-1, 1])
            translate([s * (out[0]/2 + pod_ear/2), 0, -1]) cylinder(d = 3.4, h = 5);
    }
}

// ---------------- output ----------------
module ghost_panel() {
    color([0.5, 0.5, 0.5, 0.35]) translate([0, 0, panel_back_z]) linear_extrude(panel_t)
        difference() {
            translate([-150, -60]) square([300, 235]);
            translate([-case_w/2 - cut_clear, -cut_clear])
                rrect([case_w + 2*cut_clear, case_h + 2*cut_clear], 12);
        }
}

if (PART == "case")          case_mod();
else if (PART == "box")      box6_base();
else if (PART == "lid")      box6_lid();
else if (PART == "clamps")   clamps();
else if (PART == "pod")      pod();
else if (PART == "assembly") {
    color("DarkOrange") case_mod();
    color("Gold") box6_installed();
    color("Khaki") lid6_installed();
    color("SteelBlue") clamps();
    ghost_panel();
}

// Reference numbers (shown in the console on F5/F6)
echo(str("Dash cut-out: 203.2 (existing width) x ", case_h + 2*cut_clear, " mm, R12 corners"));
echo(str("Bezel outer size: ", case_w + 2*flange_w, " x ", case_h + 2*flange_w, " mm"));
echo(str("Panel rear face at Z=", panel_back_z, "; feet stop at Z=", foot_z1));
echo(str("Clamp bars: ", clamp_t, " mm thick; screws M3 x 8"));
echo(str("Box: ", W6, " x ", H6, " x ", box_h6 + lid_t, " mm with lid (", box_h6 + lid_t + vhb_t, " mm off the case back incl. pad)"));
echo(str("Box bolts: 4x M3 x ", ceil(foot_t + vhb_t + 2 + (boss_h - nut_h) + nut_h), " into captive M3 nuts in the case"));
echo(str("Tray: ", tray_x1 - tray_x0, " x ", tray_y1 - tray_y0, " x ", tray_h, " mm"));
