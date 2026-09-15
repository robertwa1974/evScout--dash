// On-device test for nav_tile_reader.cpp against the hand-built fixture
// from firmware/assets/gen_nav_fixture.py (2026-09-14). CANNOT run until
// the SD card physically exists (arriving 2026-09-15) with that fixture
// copied onto it at /maps/Z16.nav - see "SETUP REQUIRED" below. The
// Python script already round-trip-verified this exact fixture
// independently (encode then decode from the spec, not from this file's
// logic) before this test was written - this test is the second,
// independent confirmation that the REAL C++ parser reads the same bytes
// the same way, which the Python self-check alone can't prove.
//
// SETUP REQUIRED before running:
//   1. python firmware/assets/gen_nav_fixture.py  (writes
//      firmware/assets/fixtures/Z16.nav if not already present)
//   2. Copy that file to the microSD card's /maps/Z16.nav (create the
//      /maps directory if needed)
//   3. Insert the card, then:
//        pio test -e waveshare-s3-lcd7 -f test_nav_tile
//
// Run via PlatformIO's embedded Unity runner (same reasoning as
// test_gps_parsing: the real dependency here, the SD card + FS library,
// can't run as a host/native binary either).
#include <Arduino.h>
#include <unity.h>
#include "display_driver.h"
#include "sd_driver.h"
#include "nav_tile_reader.h"

static NavTileData tile;  // ~12KB - static, never a stack local (see nav_tile_reader.h)

void setUp(void) {}
void tearDown(void) {}

void test_sd_card_mounts(void) {
    TEST_ASSERT_TRUE_MESSAGE(sd_available(),
        "No SD card mounted - insert one with /maps/Z16.nav copied onto it "
        "(see this file's SETUP REQUIRED comment) before running this test.");
}

void test_fixture_tile_loads(void) {
    bool ok = nav_tile_load(16, 100, 100, &tile);
    TEST_ASSERT_TRUE_MESSAGE(ok, "nav_tile_load failed - check /maps/Z16.nav exists on the card");
    TEST_ASSERT_FALSE(tile.truncated);
    TEST_ASSERT_EQUAL_UINT16(3, tile.featureCount);
}

static NavTileData otherTile;  // ~12KB - static, never a stack local (see nav_tile_reader.h) -
                                // an earlier version of this test declared it as a local and
                                // silently blew the task stack, boot-looping the board with no
                                // serial output ever reaching UNITY_BEGIN()

void test_out_of_bounds_tile_is_absent(void) {
    // Fixture's bounding box is exactly 1 tile at (100,100) - anything
    // else must cleanly report "not found," not garbage or a crash.
    bool ok = nav_tile_load(16, 999, 999, &otherTile);
    TEST_ASSERT_FALSE(ok);
}

void test_line_feature_decodes_correctly(void) {
    NavFeature &f = tile.features[0];
    TEST_ASSERT_EQUAL_UINT8(NAV_GEOM_LINESTRING, f.geomType);
    TEST_ASSERT_EQUAL_UINT16(3, f.vertexCount);
    TEST_ASSERT_EQUAL_INT16(100, f.vx[0]); TEST_ASSERT_EQUAL_INT16(100, f.vy[0]);
    TEST_ASSERT_EQUAL_INT16(500, f.vx[1]); TEST_ASSERT_EQUAL_INT16(300, f.vy[1]);
    TEST_ASSERT_EQUAL_INT16(900, f.vx[2]); TEST_ASSERT_EQUAL_INT16(100, f.vy[2]);
    TEST_ASSERT_EQUAL_UINT16(0xF800, f.colorRgb565);  // palette[0] = red
    TEST_ASSERT_EQUAL_UINT8(10, f.minZoom);
    TEST_ASSERT_EQUAL_UINT8(8, f.priority);
}

void test_polygon_feature_decodes_correctly(void) {
    NavFeature &f = tile.features[1];
    TEST_ASSERT_EQUAL_UINT8(NAV_GEOM_POLYGON, f.geomType);
    TEST_ASSERT_EQUAL_UINT16(4, f.vertexCount);
    TEST_ASSERT_EQUAL_INT16(1000, f.vx[0]); TEST_ASSERT_EQUAL_INT16(1000, f.vy[0]);
    TEST_ASSERT_EQUAL_INT16(1000, f.vx[1]); TEST_ASSERT_EQUAL_INT16(1400, f.vy[1]);
    TEST_ASSERT_EQUAL_INT16(1400, f.vx[2]); TEST_ASSERT_EQUAL_INT16(1400, f.vy[2]);
    TEST_ASSERT_EQUAL_INT16(1400, f.vx[3]); TEST_ASSERT_EQUAL_INT16(1000, f.vy[3]);
    TEST_ASSERT_EQUAL_UINT16(1, f.ringCount);
    TEST_ASSERT_EQUAL_UINT16(4, f.ringEnds[0]);
    TEST_ASSERT_EQUAL_UINT16(0x07E0, f.colorRgb565);  // palette[1] = green
    TEST_ASSERT_TRUE(f.isCasingOrSpecial);  // width_flags=0x80 in the fixture
}

void test_text_feature_decodes_correctly(void) {
    NavFeature &f = tile.features[2];
    TEST_ASSERT_EQUAL_UINT8(NAV_GEOM_TEXT, f.geomType);
    TEST_ASSERT_EQUAL_INT16(700, f.textX);
    TEST_ASSERT_EQUAL_INT16(700, f.textY);
    TEST_ASSERT_EQUAL_UINT8(4, f.textLen);
    TEST_ASSERT_EQUAL_STRING("TEST", f.text);
    TEST_ASSERT_EQUAL_UINT16(0x0000, f.colorRgb565);  // palette[2] = black
    TEST_ASSERT_EQUAL_UINT16(0, f.shieldBgRgb565);    // fixture has no shield
}

void setup() {
    delay(2000);

    // sd_init() asserts SD_CS via the CH422G I/O expander, which lives on
    // the same I2C bus lcd_panel_start() brings up - sd_driver.h requires
    // that ordering (see its "must run after lcd_panel_start()" comment).
    // firmware.ino's real setup() does this too; this test has no other
    // path to it since firmware.ino's own setup()/loop() are compiled out
    // under UNIT_TEST (see platformio.ini's [env:waveshare-s3-lcd7-test]).
    lcd_panel_start();
    sd_init();

    UNITY_BEGIN();
    RUN_TEST(test_sd_card_mounts);
    RUN_TEST(test_fixture_tile_loads);
    RUN_TEST(test_out_of_bounds_tile_is_absent);
    RUN_TEST(test_line_feature_decodes_correctly);
    RUN_TEST(test_polygon_feature_decodes_correctly);
    RUN_TEST(test_text_feature_decodes_correctly);
    UNITY_END();
}

void loop() {}
