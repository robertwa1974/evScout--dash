// Standalone, hardware-independent NMEA parsing sanity check (2026-09-14).
//
// Does NOT hand-roll a new GGA/RMC/VTG parser - gps_driver.cpp already
// parses NMEA via TinyGPSPlus (mikalhart/TinyGPSPlus@1.1.0, a pinned,
// widely-used dependency), so a second parser would just be duplicate code
// with its own bug surface for no benefit. Instead this feeds canned,
// known-good NMEA strings directly into TinyGPSPlus (the same library
// gps_driver.cpp uses) and asserts the parsed fields, catching a library
// version bump or a misunderstanding of its API - not a from-scratch
// parser bug.
//
// TinyGPSPlus itself #includes Arduino.h (confirmed by reading its header
// before assuming a `platform = native` host build would work), so this
// can't run as a true host/native binary - it runs on real hardware via
// PlatformIO's embedded Unity test runner instead:
//   pio test -e waveshare-s3-lcd7 -f test_gps_parsing
// No physical GPS module or wiring is involved either way - "no serial
// port" in the sense that matters here (no live NMEA stream needed), even
// though the test binary itself runs on the ESP32 rather than the host.
//
// Checksums for all three canned sentences were computed (XOR of every
// byte between '$' and '*'), not copied from memory - confirmed *47, *6A,
// *48 respectively before trusting TinyGPSPlus would even accept them
// (encode() silently ignores a sentence that fails its checksum, which
// would otherwise make every assertion below silently see stale/zeroed
// fields instead of a clear parse failure).
//
// Two assumptions in an earlier version of this file were wrong and this
// test caught both on first run against real hardware: (1) NMEA's 2-digit
// year is windowed as 2000+yy with no century heuristic (the classic
// 1994-dated reference RMC sentence below parses as year 2094, not 1994);
// (2) TinyGPSPlus does not parse VTG at all - see
// test_vtg_sentences_are_not_parsed_by_tinygpsplus's comment.
#include <Arduino.h>
#include <unity.h>
#include <TinyGPS++.h>

static void feed(TinyGPSPlus &gps, const char *sentence) {
    for (const char *p = sentence; *p; p++) gps.encode(*p);
    gps.encode('\r');
    gps.encode('\n');
}

// Standard NMEA reference sentences (same example fix widely used in NMEA
// 0183 documentation): position 48 07.038' N, 011 31.000' E.
static const char *GGA = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char *RMC = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
static const char *VTG = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";

void setUp(void) {}
void tearDown(void) {}

void test_gga_parses_position(void) {
    TinyGPSPlus gps;
    feed(gps, GGA);
    TEST_ASSERT_TRUE(gps.location.isValid());
    // 4807.038 N -> 48 + 07.038/60
    TEST_ASSERT_FLOAT_WITHIN(0.0005, 48.1173, gps.location.lat());
    // 01131.000 E -> 11 + 31.000/60
    TEST_ASSERT_FLOAT_WITHIN(0.0005, 11.516667, gps.location.lng());
    TEST_ASSERT_TRUE(gps.altitude.isValid());
    TEST_ASSERT_FLOAT_WITHIN(0.05, 545.4, gps.altitude.meters());
    TEST_ASSERT_TRUE(gps.satellites.isValid());
    TEST_ASSERT_EQUAL_UINT32(8, gps.satellites.value());
}

void test_rmc_parses_speed_course_datetime(void) {
    TinyGPSPlus gps;
    feed(gps, RMC);
    TEST_ASSERT_TRUE(gps.location.isValid());  // status 'A' = active/valid fix
    TEST_ASSERT_FLOAT_WITHIN(0.0005, 48.1173, gps.location.lat());
    TEST_ASSERT_FLOAT_WITHIN(0.0005, 11.516667, gps.location.lng());
    TEST_ASSERT_TRUE(gps.speed.isValid());
    // 022.4 knots -> km/h (022.4 * 1.852)
    TEST_ASSERT_FLOAT_WITHIN(0.05, 41.4848, gps.speed.kmph());
    TEST_ASSERT_TRUE(gps.course.isValid());
    TEST_ASSERT_FLOAT_WITHIN(0.05, 84.4, gps.course.deg());
    TEST_ASSERT_TRUE(gps.date.isValid());
    // NMEA's date field is 2-digit-year (ddmmyy) - TinyGPSPlus windows it
    // as 2000+yy unconditionally (confirmed by this assertion failing
    // against 1994 on real hardware, then reading TinyGPSPlus's date
    // parsing - it has no century heuristic at all, just adds 2000). Every
    // real GPS module in service today emits yy in 2000-2099 anyway, so
    // this is a correct, harmless limitation for this project - just not
    // what the classic 1994-dated NMEA reference sentence would suggest
    // at a glance.
    TEST_ASSERT_EQUAL_UINT16(2094, gps.date.year());
    TEST_ASSERT_EQUAL_UINT8(3, gps.date.month());
    TEST_ASSERT_EQUAL_UINT8(23, gps.date.day());
    TEST_ASSERT_TRUE(gps.time.isValid());
    TEST_ASSERT_EQUAL_UINT8(12, gps.time.hour());
    TEST_ASSERT_EQUAL_UINT8(35, gps.time.minute());
    TEST_ASSERT_EQUAL_UINT8(19, gps.time.second());
}

// TinyGPSPlus's core object model (gps.speed/course/location/etc) is
// hardwired to exactly two sentence types - confirmed by reading
// TinyGPS++.cpp's sentence-type switch, which only recognizes "RMC" and
// "GGA" (see the GPS_SENTENCE_RMC/GPS_SENTENCE_GGA cases; nothing branches
// on "VTG" at all). A VTG sentence is parsed as GPS_SENTENCE_OTHER and
// silently produces no field updates - not a crash, not a checksum
// rejection, just a harmless no-op. This means gps_driver.cpp's speed and
// course already come entirely from RMC; a real module that also emits
// VTG frames would have those frames quietly ignored, which is fine since
// nothing in this project reads a VTG-only field. Documented here instead
// of asserting something false, since the original ask was to test VTG
// parsing and the honest answer is "there isn't any."
void test_vtg_sentences_are_not_parsed_by_tinygpsplus(void) {
    TinyGPSPlus gps;
    feed(gps, VTG);
    TEST_ASSERT_FALSE(gps.speed.isValid());
    TEST_ASSERT_FALSE(gps.course.isValid());
}

void test_bad_checksum_is_rejected(void) {
    TinyGPSPlus gps;
    // Same GGA sentence with the last checksum digit corrupted (47 -> 48) -
    // confirms encode() actually validates checksums rather than trusting
    // any well-formed-looking sentence, which the earlier "positive" tests
    // alone wouldn't catch.
    feed(gps, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48");
    TEST_ASSERT_FALSE(gps.location.isValid());
}

void setup() {
    delay(2000);  // let the board settle before Unity starts talking over Serial
    UNITY_BEGIN();
    RUN_TEST(test_gga_parses_position);
    RUN_TEST(test_rmc_parses_speed_course_datetime);
    RUN_TEST(test_vtg_sentences_are_not_parsed_by_tinygpsplus);
    RUN_TEST(test_bad_checksum_is_rejected);
    UNITY_END();
}

void loop() {}
