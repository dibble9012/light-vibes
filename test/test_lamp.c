/* Host test for the parts of speed mode and the saved lamp colour that need no car. ./build.sh test runs it. */
#define main daemon_main
#include "../src/lightvibesd.c"
#undef main
#include <assert.h>

static void write_file(const char *p, const char *s) { FILE *f = fopen(p, "w"); assert(f); fputs(s, f); fclose(f); }

int main(void) {
    C.speed_low = 0; C.speed_high = 80; C.speed_color_low = 44; C.speed_color_high = 22;
    snprintf(C.speed_unit, sizeof C.speed_unit, "mph");

    /* ends and clamping */
    assert(speed_wheel_index(0) == 44);
    assert(speed_wheel_index(80 * KMH_PER_MPH) == 22);
    assert(speed_wheel_index(200) == 22);
    /* the fade never lands on a pale colour, and moves one way only */
    int prev = 44;
    for (int mph = 0; mph <= 80; mph++) {
        int i = speed_wheel_index(mph * KMH_PER_MPH);
        assert(!wheel_pale(i) && i <= prev && i >= 22);
        prev = i;
    }
    /* no speed range sits on one colour: 45 steps of speed share out the 15 non-pale colours */
    int same = 0, longest = 0; prev = -1;
    for (int mph = 0; mph <= 80; mph++) {
        int i = speed_wheel_index(mph * KMH_PER_MPH);
        same = i == prev ? same + 1 : 1; prev = i;
        if (same > longest) longest = same;
    }
    assert(longest <= 8);
    /* the short way round passes through the top of the wheel */
    C.speed_color_low = 60; C.speed_color_high = 4;
    int mid = speed_wheel_index(40 * KMH_PER_MPH);
    assert(mid >= 60 || mid <= 4);
    /* a pale colour the user picked is kept */
    C.speed_color_low = 35; C.speed_color_high = 22;
    assert(speed_wheel_index(0) == 35);
    /* km/h, and low == high */
    snprintf(C.speed_unit, sizeof C.speed_unit, "kmh");
    C.speed_color_low = 44; C.speed_color_high = 22; C.speed_high = 100;
    assert(speed_wheel_index(100) == 22);
    C.speed_low = C.speed_high = 50;
    assert(speed_wheel_index(49) == 44 && speed_wheel_index(50) == 22);

    /* replies from the vehicle service */
    double v = -1;
    assert(parse_speed_reply("speed 34.5\n", &v) && v > 34.4 && v < 34.6);
    assert(parse_speed_reply("speed 0\n", &v) && v == 0);
    assert(!parse_speed_reply("speed -\n", &v));
    assert(!parse_speed_reply("refused\n", &v));

    /* the saved colour */
    char path[] = "/tmp/lightvibesd-test-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    light_state_path = path;
    int s[6];
    write_file(path, "garbage");
    assert(!saved_load(s));
    base.r = 16; base.g = 32; base.b = 239; base.bright_raw = 11; base.drive_mode = 1;
    saved_write(1);
    assert(saved_load(s) && s[0] == 16 && s[1] == 32 && s[2] == 239 && s[3] == 11 && s[4] == 1 && s[5] == 1);
    /* a colour left on the lamp mid-song must not replace it: snapshot_base trusts the file while it's dirty */
    base.r = 53; base.g = 245; base.b = 169; base.bright_raw = 4; base.valid = 0;
    snapshot_base();
    assert(base.valid && base.r == 16 && base.g == 32 && base.b == 239 && base.bright_raw == 11);
    saved_write(0);
    assert(saved_load(s) && s[5] == 0);
    remove(path);

    puts("lamp tests passed");
    return 0;
}
