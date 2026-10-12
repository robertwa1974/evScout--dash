// =====================================================================
//  Scout 80 dash — Waveshare ESP32-S3-Touch-LCD-7 enclosure mods
//  Base case: VaAndCob/ESP32-S3-Mastermind  waveshare7_3D/waveshare_7_case.stl (MIT)
//
//  Parts (pick with PART below, F6 render, then File > Export > STL):
//    "case"     original back shell + front bezel flange (+ optional cable hole)
//    "carrier"  rear retainer / carrier: clamps the dash panel from behind and
//               holds the GY-NEO6MV2 GPS board and GY-521 IMU between itself
//               and the case back (boards face the case floor, protected)
//    "pod"      GPS patch-antenna pod (25x25 ceramic patch, faces up) —
//               stick or screw it wherever it can see the sky
//    "assembly" everything in place, with a ghost dash panel (preview only)
//
//  Coordinate frame = original STL:
//    X = width (-98.5..98.5), Y = height (0..115), Z = depth.
//    Z = -0.5 is the case back face, Z = 18.5 is the front (glass) face.
//  All dimensions in mm.
// =====================================================================

PART = "assembly";            // "case" | "carrier" | "pod" | "assembly"
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
back_web      = 3.5;    // carrier material between socket bottom and screw head
rail_t        = max(10, standoff_len + socket_extra + back_web); // = crossbar, back prints flat
foot_reach    = 9.5;    // foot length along Y, beyond the cut-out edge
foot_margin   = 3;      // gap between foot inner face and the cut-out edge
preload       = 0.3;    // feet stop this short of the panel so tightening clamps it
bar_t         = rail_t; // centre crossbar thickness (holds the boards)
bridge_y      = [45.5, 77.5]; // left rail arches over the side port window (y 48..75)
bridge_gap    = 4;      // clearance under that arch for USB plug boots
bar_y0        = 73.5;   // crossbar bottom edge (clears the floor slot at x 44..51.5)
bar_y0_left   = 76;     // ...and left of x = -27 (clears the big port window, y <= 75)
bar_y1        = 105;    // top edge (clears the open recess at x -15..24, y >= 106.5)

// ---------------- GY-NEO6MV2 GPS board ----------------
gps_pcb       = [36.80, 25.75]; // measured
gps_c         = [-8, 90];  // centre on the case back (X,Y)
gps_top_clear = 3.0;        // room for NEO-6M chip / u.FL on the side facing the case
gps_bot_clear = 3.0;        // room for backup battery / pads on the other side

// ---------------- GY-521 MPU-6050 IMU ----------------
imu_pcb       = [20.55, 15.80]; // measured
imu_c         = [31, 90];
imu_top_clear = 2.0;
imu_bot_clear = 3.0;

pcb_t         = 1.6;
pcb_clear     = 0.4;    // total XY clearance in the pocket
ledge         = 1.5;    // width of the ledge the PCB edge sits on

// ---------------- wiring ----------------
case_cable_hole = true;      // add a hole through the case floor for GPS/IMU wiring
cable_hole_c    = [15, 90];  // under the carrier crossbar, between the two boards
cable_hole_d    = 10;        // passes a JST-PH/SH plug
chan_w          = 5;  chan_d = 2.5;   // wire channels on the carrier front face
coax_exit       = true;      // channel from the GPS pocket to the carrier top edge for the
                             //   antenna coax (u.FL) — needs a u.FL extension to reach the pod

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

module case_mod() {
    difference() {
        union() {
            import(CASE_STL, convexity = 10);
            flange();
        }
        if (case_cable_hole)
            translate([cable_hole_c[0], cable_hole_c[1], case_z0 - 1])
                cylinder(d = cable_hole_d, h = 4);
    }
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

module pocket(c, pcb, top_clear, bot_clear) {
    // open toward the case (front face of the carrier, Z = case_z0)
    L = pcb[0] + pcb_clear;  W = pcb[1] + pcb_clear;
    ledge_z = case_z0 - top_clear - pcb_t;   // Z of the face the PCB rests on
    translate([c[0], c[1], 0]) {
        // full-size pocket down to the PCB seat
        translate([-L/2, -W/2, ledge_z]) cube([L, W, -ledge_z + case_z0 + 1]);
        // reduced pocket below the seat for bottom-side parts
        translate([-L/2 + ledge, -W/2 + ledge, ledge_z - bot_clear])
            cube([L - 2*ledge, W - 2*ledge, bot_clear + 0.01]);
        // fingernail notches to lift the board out
        for (s = [-1, 1]) translate([s * L/2, 0, ledge_z - 1])
            cylinder(d = 8, h = -ledge_z + case_z0 + 2);
    }
}

module carrier() {
    xs = [holes[0][0], holes[1][0]];           // rail centre lines
    difference() {
        union() {
            // two rails
            for (x = xs) translate([x - rail_w/2, rail_y0, case_z0 - rail_t])
                cube([rail_w, rail_y1 - rail_y0, rail_t]);
            // feet + gussets
            for (x = xs) for (end = [0, 1]) {
                y_in  = end == 0 ? foot_in0 : foot_in1;
                y_out = end == 0 ? rail_y0  : rail_y1;
                translate([x - rail_w/2, min(y_in, y_out), case_z0 - rail_t])
                    cube([rail_w, abs(y_out - y_in), foot_z1 - case_z0 + rail_t]);
            }
            // centre crossbar carrying the boards
            translate([xs[0] + rail_w/2 - 0.01, bar_y0_left, case_z0 - bar_t])
                cube([xs[1] - xs[0] - rail_w + 0.02, bar_y1 - bar_y0_left, bar_t]);
            translate([-27, bar_y0, case_z0 - bar_t])
                cube([xs[1] - rail_w/2 + 27 + 0.01, bar_y1 - bar_y0, bar_t]);
        }
        // arch over the side port window
        translate([xs[0] - rail_w/2 - 1, bridge_y[0], case_z0 - bridge_gap])
            cube([rail_w + 2, bridge_y[1] - bridge_y[0], bridge_gap + 1]);
        // screw holes + counterbores from the back
        for (h = holes) translate([h[0], h[1], 0]) {
            sock_d = standoff_af / cos(30) + socket_clear;
            sock_h = standoff_len + socket_extra;
            translate([0, 0, case_z0 - bar_t - 1]) cylinder(d = screw_hole_d, h = bar_t + 2);
            translate([0, 0, case_z0 - sock_h]) cylinder(d = sock_d, h = sock_h + 1);
            translate([0, 0, case_z0 - lead_in])
                cylinder(d1 = sock_d, d2 = sock_d + 2*lead_in + 0.02, h = lead_in + 0.01);
        }
        pocket(gps_c, gps_pcb, gps_top_clear, gps_bot_clear);
        pocket(imu_c, imu_pcb, imu_top_clear, imu_bot_clear);
        // wire channels on the front face: each pocket -> cable hole
        for (c = [gps_c, imu_c]) hull() {
            translate([c[0], c[1], case_z0 - chan_d]) cylinder(d = chan_w, h = chan_d + 1);
            translate([cable_hole_c[0], cable_hole_c[1], case_z0 - chan_d])
                cylinder(d = chan_w, h = chan_d + 1);
        }
        // landing for the cable hole
        translate([cable_hole_c[0], cable_hole_c[1], case_z0 - chan_d])
            cylinder(d = cable_hole_d, h = chan_d + 1);
        // coax exit to the top edge
        if (coax_exit) hull() {
            translate([gps_c[0] - 12, gps_c[1], case_z0 - chan_d]) cylinder(d = chan_w, h = chan_d + 1);
            translate([gps_c[0] - 12, bar_y1 + 1, case_z0 - chan_d]) cylinder(d = chan_w, h = chan_d + 1);
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
else if (PART == "carrier")  carrier();
else if (PART == "pod")      pod();
else {
    color("DarkOrange") case_mod();
    color("SteelBlue") carrier();
    color("SeaGreen") translate([0, case_h + 30, 0]) rotate([90, 0, 0]) pod();
    ghost_panel();
}

// Reference numbers (shown in the console on F5/F6)
echo(str("Dash cut-out: 203.2 (existing width) x ", case_h + 2*cut_clear, " mm, R12 corners"));
echo(str("Bezel outer size: ", case_w + 2*flange_w, " x ", case_h + 2*flange_w, " mm"));
echo(str("Panel rear face at Z=", panel_back_z, "; feet stop at Z=", foot_z1));
echo(str("Carrier screws: M3 x ", ceil(rail_t - standoff_len - socket_extra + 4), " (", rail_t - standoff_len - socket_extra, " mm of carrier + ~4.5 mm thread engagement)"));
