/* Host test for the parts of speed mode and the saved lamp colour that need no car. ./build.sh test runs it. */
#define main daemon_main
#include "../src/lightvibesd.c"
#undef main
#include <assert.h>

static void write_file(const char *p, const char *s) { FILE *f = fopen(p, "w"); assert(f); fputs(s, f); fclose(f); }

/* the test build has no tinyalsa */
struct mixer *mixer_open(unsigned int card) { (void)card; return NULL; }

/* A pretend lamp: two scripts stand in for `service` and `logcat`, and a state file holds what the lamp shows.
   LAMP_MODE: ok | ignore_colour | ignore_bright | silent | missing */
static const char *FAKE_SERVICE =
"#!/bin/sh\n"
"D=$LAMP_DIR; . $D/state\n"
"[ \"$LAMP_MODE\" = missing ] && { echo \"service: Service $2 does not exist\"; exit 0; }\n"
"answer() { [ \"$LAMP_MODE\" = silent ] && return; n=$(cat $D/n); n=$((n+1)); echo $n > $D/n;\n"
"  printf '11-06 10:00:00.%03d D/SystemService(  651): SYS_SML_STATUS_M_RESP: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x \\n' $n $((r>>8)) $((r&255)) $((g>>8)) $((g&255)) $((b>>8)) $((b&255)) $br ${dim:-1} $dm >> $D/log; }\n"
"dec() { echo $(( (($1&255)<<8) | (($1>>8)&255) )); }\n"
"if [ \"$2\" = com.hkmc.micom.hev ]; then v=$(grep \"^$5 \" $D/hev | cut -d' ' -f2); printf 'Result: Parcel(00000000 %08x   \\x27....\\x27)\\n' ${v:-4294967295}; exit 0; fi\n"
"case $3 in\n"
" 76) answer ;;\n"
" 77) if [ $5 = 5 ]; then [ \"$LAMP_MODE\" = ignore_bright ] || br=$7; elif [ $5 = 9 ]; then dm=$7; elif [ $5 = 8 ]; then dim=$7; fi ;;\n"
" 78) if [ \"$LAMP_MODE\" != ignore_colour ]; then r=$(dec $7); g=$(dec ${11}); b=$(dec ${15}); fi ;;\n"
"esac\n"
"echo \"r=$r g=$g b=$b br=$br dm=$dm dim=${dim:-1}\" > $D/state\n"
"echo \"Result: Parcel(00000000    '....')\"\n";
static const char *FAKE_LOGCAT = "#!/bin/sh\ncat $LAMP_DIR/log\n";
static const char *FAKE_GETPROP = "#!/bin/sh\necho \"value-of-$1\"\n";

static char lamp_dir[64];
static void script(const char *name, const char *body, char *out, size_t n) {
    snprintf(out, n, "%s/%s", lamp_dir, name);
    write_file(out, body); chmod(out, 0755);
}
static const char *result_value(const char *key, char *out, size_t n) {
    char p[400], line[300]; snprintf(p, sizeof p, "%s/diag/result", lamp_dir);
    FILE *f = fopen(p, "r"); assert(f);
    size_t kl = strlen(key); out[0] = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') { line[strcspn(line, "\n")] = 0; snprintf(out, n, "%s", line + kl + 1); }
    fclose(f);
    return out;
}
static void lamp_test(const char *mode, const char *c, const char *b, int reply, int restored, const char *dm_start) {
    char p[300], v[100];
    setenv("LAMP_MODE", mode, 1);
    snprintf(p, sizeof p, "%s/state", lamp_dir); snprintf(v, sizeof v, "r=255 g=68 b=35 br=2 dm=%s\n", dm_start); write_file(p, v);
    snprintf(p, sizeof p, "%s/log", lamp_dir); write_file(p, "");
    snprintf(p, sizeof p, "%s/n", lamp_dir); write_file(p, "0\n");
    remove(light_state_path);
    memset(lamp_stamp, 0, sizeof lamp_stamp);
    snprintf(p, sizeof p, "%s/diag/request", lamp_dir); write_file(p, "test\n");
    assert(diag_poll() == 1);
    assert(access(p, F_OK) != 0);              /* the request is gone once read */
    diag_run();
    assert(diag_poll() == 0);
    assert(!strcmp(result_value("stage", v, sizeof v), "done"));
    if (strcmp(result_value("c", v, sizeof v), c)) { printf("%s: colours %s, expected %s\n", mode, v, c); assert(0); }
    if (strcmp(result_value("b", v, sizeof v), b)) { printf("%s: brightness %s, expected %s\n", mode, v, b); assert(0); }
    assert(atoi(result_value("reply", v, sizeof v)) == reply);
    if (atoi(result_value("restored", v, sizeof v)) != restored) { printf("%s: restored %s\n", mode, v); assert(0); }
    /* the lamp ends up as it started, however it behaved */
    if (restored) {
        char st[100]; snprintf(p, sizeof p, "%s/state", lamp_dir);
        FILE *f = fopen(p, "r"); assert(f && fgets(st, sizeof st, f)); fclose(f);
        char want[100]; snprintf(want, sizeof want, "r=255 g=68 b=35 br=2 dm=%s dim=1\n", dm_start);
        if (strcmp(st, want)) { printf("%s: left the lamp at %s", mode, st); assert(0); }
    }
    int s[7];
    if (reply) assert(saved_load(s) && s[5] == 0);   /* a clean finish, or the next start would "restore" */
}

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

    /* power from running totals: 1000 Wh more over 2 s is 1800 kW... no: 10 Wh over 2 s is 18 kW */
    pw_n = 0; double kw = 0;   /* (the sampler thread isn't running in this test) */
    assert(!power_from_samples(1000, 5000, &kw));            /* one sample isn't a rate */
    assert(!power_from_samples(1250, 5001, &kw));            /* under the 1 s window */
    assert(power_from_samples(2000, 5005, &kw) && kw > 17.9 && kw < 18.1);   /* 5 Wh in 1 s is 18 kW */
    assert(power_from_samples(2250, 5006, &kw) && kw > 17.9 && kw < 18.1);   /* from the 1250 sample: 5 Wh in 1 s */
    assert(power_from_samples(3250, 5000, &kw) && kw < 0);   /* regen */
    assert(!power_from_samples(4000, 100, &kw) && pw_n == 1); /* totals reset: start again */
    /* the car's average, whatever unit it's in */
    assert(fabs(eff_convert(2, 26, 0) - 2.6) < 1e-9);
    assert(fabs(eff_convert(2, 26, 1) - 2.6 * KMH_PER_MPH) < 1e-9);
    assert(fabs(eff_convert(1, 244, 0) - 100.0 / (24.4 * KMH_PER_MPH)) < 1e-9);   /* the EV6 in km: 24.4 kWh/100 km */
    assert(fabs(eff_convert(1, 244, 1) - 100.0 / 24.4) < 1e-9);                   /* = 4.1 km/kWh */
    assert(fabs(eff_convert(3, 42, 0) - 4.2 / KMH_PER_MPH) < 1e-9);
    /* units: "auto" follows the car, unknown counts as miles */
    snprintf(C.speed_unit, sizeof C.speed_unit, "auto"); snprintf(C.eff_unit, sizeof C.eff_unit, "auto");
    car_mi = -1; assert(!speed_in_kmh() && !eff_in_km());
    car_mi = 0; assert(speed_in_kmh() && eff_in_km());
    car_mi = 1; assert(!speed_in_kmh() && !eff_in_km());
    snprintf(C.speed_unit, sizeof C.speed_unit, "kmh"); assert(speed_in_kmh());
    /* power colours: regen end, the top, and clamping */
    assert(wheel_walk(-50, -50, 150, 50, 22) == 50 && wheel_walk(150, -50, 150, 50, 22) == 22 && wheel_walk(999, -50, 150, 50, 22) == 22);
    {   int prev = 50;
        for (int k = -50; k <= 150; k += 5) { int i = wheel_walk(k, -50, 150, 50, 22); assert(i <= prev && i >= 22 && !wheel_pale(i)); prev = i; } }

    /* power's three colours: regen walks blue to green, driving walks green to red */
    C.power_low = -50; C.power_high = 150; C.power_color_low = 0; C.power_color_zero = 44; C.power_color_high = 22;
    assert(power_wheel_index(-50) == 0 && power_wheel_index(0) == 44 && power_wheel_index(150) == 22 && power_wheel_index(-200) == 0);
    for (int k = -50; k < 0; k += 5) { int i = power_wheel_index(k); assert((i == 0 || i >= 44) && !wheel_pale(i)); }
    for (int k = 5; k <= 150; k += 5) { int i = power_wheel_index(k); assert(i >= 22 && i <= 44); }
    C.power_low = 10;          /* no regen in the range: zero is the low end */
    assert(power_wheel_index(-20) == 0 && power_wheel_index(150) == 22);
    /* counters that restart: a drop counts what it reads now, a glitch counts nothing */
    assert(counter_step(100, 130) == 30 && counter_step(900, 12) == 12 && counter_step(5, 5000) == 0);
    assert(net_step(269, 253) == -16 && net_step(100, 130) == 30 && net_step(300, 5000) == 0 && net_step(900, 12) == 0);

    /* replies from the vehicle service */
    double v = -1;
    assert(parse_speed_reply("speed 34.5\n", &v) && v > 34.4 && v < 34.6);
    assert(parse_speed_reply("speed 0\n", &v) && v == 0);
    assert(!parse_speed_reply("speed -\n", &v));
    assert(!parse_speed_reply("refused\n", &v));

    /* palette auto-detection (Kia EV6 vs Hyundai Ioniq 5/6), and the speed fade works on both palettes */
    {
        char bp[] = "/tmp/lightvibesd-prop-XXXXXX"; int bfd = mkstemp(bp); assert(bfd >= 0); close(bfd);
        snprintf(C.palette, sizeof C.palette, "auto");
        build_prop_path = bp; hmgapi_json_path = "/nonexistent";
        write_file(bp, "ro.product.manufacturer=HYUNDAI MOBIS\nro.build.product=CVEV.USA.S5W_M.V009.001.260507\n");
        assert(!strcmp(detect_palette(), "kia"));
        write_file(bp, "ro.product.manufacturer=HYUNDAI MOBIS\nro.build.product=NEEV.USA.S5W_M.V009.001.260507\n");
        assert(!strcmp(detect_palette(), "hyundai"));
        write_file(bp, "ro.product.device=wp_daudioplus_neev23se_us\n");
        assert(!strcmp(detect_palette(), "hyundai"));
        write_file(bp, "ro.build.product=CEEV.USA.S5W_M.V001.001.260507\n");
        assert(!strcmp(detect_palette(), "hyundai"));
        snprintf(C.palette, sizeof C.palette, "kia");
        assert(!strcmp(detect_palette(), "kia"));
        remove(bp); build_prop_path = "/nonexistent";
        set_palette("hyundai");
        int sl = C.speed_low, sh = C.speed_high, cl = C.speed_color_low, ch = C.speed_color_high; char su[8]; snprintf(su, sizeof su, "%s", C.speed_unit);
        C.speed_low = 0; C.speed_high = 80; C.speed_color_low = 44; C.speed_color_high = 22; snprintf(C.speed_unit, sizeof C.speed_unit, "mph");
        assert(speed_wheel_index(0) == 44 && speed_wheel_index(200) == 22);
        int prev = 44;
        for (int mph = 0; mph <= 80; mph++) { int i = speed_wheel_index(mph * KMH_PER_MPH); assert(!wheel_pale(i) && i <= prev && i >= 22); prev = i; }
        C.speed_low = sl; C.speed_high = sh; C.speed_color_low = cl; C.speed_color_high = ch; snprintf(C.speed_unit, sizeof C.speed_unit, "%s", su);
        set_palette("kia"); snprintf(C.palette, sizeof C.palette, "auto");
    }

    /* the saved colour */
    char path[] = "/tmp/lightvibesd-test-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    light_state_path = path;
    int s[7];
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
    base.dim = 2; saved_write(0);
    assert(saved_load(s) && s[6] == 2);
    write_file(path, "16 32 239 11 1 0\n");                        /* a file from 0.7.x: no dim */
    assert(saved_load(s) && s[6] == -1);
    base.dim = -1;

    /* ---------- the lamp test, against the pretend lamp ---------- */
    snprintf(lamp_dir, sizeof lamp_dir, "/tmp/lightvibesd-lamp-XXXXXX");
    assert(mkdtemp(lamp_dir));
    setenv("LAMP_DIR", lamp_dir, 1);
    char dpath[200];
    snprintf(dpath, sizeof dpath, "%s/diag", lamp_dir); mkdir(dpath, 0755);
    snprintf(dpath, sizeof dpath, "%s/settings", lamp_dir);
    diag_set_dir(dpath);
    char want_dir[200]; snprintf(want_dir, sizeof want_dir, "%s/diag", lamp_dir);
    assert(!strcmp(diag_dir, want_dir));
    script("service", FAKE_SERVICE, C.service_bin, sizeof C.service_bin);
    script("logcat", FAKE_LOGCAT, C.logcat_bin, sizeof C.logcat_bin);
    static char gp[96]; script("getprop", FAKE_GETPROP, gp, sizeof gp); getprop_bin = gp;
    snprintf(C.svc_name, sizeof C.svc_name, "com.hkmc.micom.system");
    snprintf(C.loop_ctl, sizeof C.loop_ctl, "x");
    snprintf(C.mode, sizeof C.mode, "spectrum");
    C.card = 0; C.device = 2; C.dry = 0; C.override_drive_mode = 1;
    hmgapi_dir = lamp_dir;
    char mods[220]; snprintf(mods, sizeof mods, "%s/mods", lamp_dir); mkdir(mods, 0755);
    snprintf(mods, sizeof mods, "%s/mods/auto-dst", lamp_dir); mkdir(mods, 0755);
    snprintf(mods, sizeof mods, "%s/mods/light-vibes", lamp_dir); mkdir(mods, 0755);
    snprintf(mods, sizeof mods, "%s/installed.txt", lamp_dir); write_file(mods, "version 1.0.2\nbuild x\n");
    diag_hold_ms = 1; read_wait_ms = 1;
    usb_roots[0] = NULL;

    lamp_test("ok", "3/3", "3/3", 1, 1, "1");
    char v2[100];
    assert(!strcmp(result_value("hmg", v2, sizeof v2), "1.0.2"));
    assert(!strcmp(result_value("mods", v2, sizeof v2), "auto-dst"));           /* not ourselves */
    assert(!strcmp(result_value("product", v2, sizeof v2), "value-of-ro.build.product"));
    assert(!strcmp(result_value("svc", v2, sizeof v2), "00000000"));
    assert(!strcmp(result_value("user", v2, sizeof v2), "255,68,35,2,1"));
    /* drive-mode colour is switched off for the test and comes back */
    lamp_test("ok", "3/3", "3/3", 1, 1, "2");
    /* the failures the reports describe */
    lamp_test("ignore_colour", "0/3", "3/3", 1, 1, "1");
    lamp_test("ignore_bright", "3/3", "1/3", 1, 1, "1");     /* raw 2, its own starting level, still "matches" */
    lamp_test("silent", "0/3", "0/3", 0, 0, "1");
    assert(!strcmp(result_value("reply", v2, sizeof v2), "0"));
    lamp_test("missing", "0/3", "0/3", 0, 0, "1");
    assert(!strcmp(result_value("svc", v2, sizeof v2), "missing"));

    /* the trip, against a pretend EV service: counters that restart mid-drive, climate, and a service restart */
    setenv("LAMP_MODE", "ok", 1);
    char hp[300]; snprintf(hp, sizeof hp, "%s/hev", lamp_dir);
    static char tp[200], ep[200], bp[200];
    snprintf(tp, sizeof tp, "%s/trip", lamp_dir); trip_path = tp;
    snprintf(ep, sizeof ep, "%s/energy.log", lamp_dir); energy_log_path = ep;
    snprintf(bp, sizeof bp, "%s/boot_id", lamp_dir); boot_id_path = bp; write_file(bp, "boot-1\n");
    trip_begin();
    double ev = 0;
    energy_scale = 100;                                              /* the arithmetic first, unscaled */
    write_file(hp, "12 2\n31 1000\n66 0\n71 200\n72 100\n89 300\n90 10\n1 2\n2 26\n");
    energy_sample(1);
    assert(read_avg_eff(&ev, 0) && fabs(ev - 2.6) < 1e-9);
    assert(read_trip_eff(&ev, 0) == -1);                             /* no distance yet */
    write_file(hp, "12 2\n31 1010\n66 0\n71 500\n72 300\n89 300\n90 10\n");   /* +1.0 km, driving: +500 Wh net */
    energy_sample(1);
    write_file(hp, "12 2\n31 1020\n66 0\n71 400\n72 300\n89 400\n90 10\n");   /* +1.0 km, regen: net -100, regen +100 */
    energy_sample(1);
    assert(en.mot == 400 && en.gen == 100);
    assert(read_trip_eff(&ev, 0) == 1 && fabs(ev - 2.0 / KMH_PER_MPH / 0.4) < 0.01);   /* 2.0 km on 400 Wh */
    assert(read_trip_eff(&ev, 1) == 1 && fabs(ev - 2.0 / 0.4) < 0.01);
    energy_scale = 250;                                              /* calibrated to the cluster */
    assert(read_trip_eff(&ev, 1) == 1 && fabs(ev - 2.0 / 1.0) < 0.01);
    energy_scale = 100;
    /* the drive it was calibrated on: 5.1 km, 484 Wh motor, 219 Wh climate came to the cluster's 2.85 mi/kWh */
    {   long long m = en.mot, o0 = en.odo0, o = en.odo; double c = en.clim;
        en.mot = 484; en.clim = 219; en.odo0 = 847632; en.odo = 847683; energy_scale = TRIP_SCALE_DEFAULT;
        assert(read_trip_eff(&ev, 0) == 1 && fabs(ev - 2.85) < 0.05);
        en.mot = m; en.clim = c; en.odo0 = o0; en.odo = o; energy_scale = 100; }
    /* the service restarts mid-drive: the trip carries on */
    trip_save();
    trip_begin();
    assert(en.mot == 400 && en.gen == 100 && en.odo0 == 1000);
    /* READY off then on without a restart: a new trip, as the cluster's Current Drive does */
    write_file(hp, "12 2\n31 1030\n44 0\n66 0\n71 400\n72 300\n89 400\n90 10\n");
    energy_sample(1);
    assert(en.mot == 400 && en.ready == 0);
    write_file(hp, "12 2\n31 1030\n44 1\n66 0\n71 410\n72 300\n89 400\n90 10\n");
    energy_sample(1);
    assert(en.mot == 10 && en.odo0 == 1030 && en.ready == 1);   /* the second it switched on counts to the new trip */
    write_file(hp, "12 2\n31 1040\n44 1\n66 0\n71 450\n72 300\n89 400\n90 10\n");
    energy_sample(1);
    assert(en.mot == 50 && en.odo - en.odo0 == 10);
    trip_save(); trip_begin();
    assert(en.mot == 50 && en.odo0 == 1030);       /* and a restart after that carries on with it */

    /* the next drive (a new boot) starts again */
    write_file(bp, "boot-2\n");
    trip_begin();
    assert(en.mot == 0 && !en.have_odo);
    write_file(hp, "12 2\n31 1020\n1 2\n2 1023\n");               /* motors not answering, average invalid */
    energy_sample(1);
    assert(read_trip_eff(&ev, 0) == -1 && !read_avg_eff(&ev, 0));
    struct stat est; assert(!stat(ep, &est) && est.st_size > 0);     /* the moving samples were logged */

    /* speed/power/efficiency brightness: Auto turns on the car's "dimmed in dark", a level turns it off, and the
       owner's setting comes back with their colour */
    {
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        write_file(st, "r=255 g=68 b=35 br=4 dm=1 dim=2\n");
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path);
        base.valid = 0; we_set_dim = 0;
        snapshot_base();
        assert(base.valid && base.dim == 2 && base.bright_raw == 4);
        take_over_lamp();
        snprintf(C.gauge_bright, sizeof C.gauge_bright, "7"); C.gauge_dim = 0;
        gauge_brightness();
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "br=8") && strstr(line, "dim=1"));
        C.gauge_dim = 1;                                   /* dimmed in dark, with the level kept */
        gauge_brightness();
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dim=2") && strstr(line, "br=8"));
        snprintf(C.gauge_bright, sizeof C.gauge_bright, "3"); C.gauge_dim = 0;
        gauge_brightness();
        restore_base();
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        if (!strstr(line, "br=4") || !strstr(line, "dim=2")) { printf("after restore: %s", line); assert(0); }
        base.valid = 0; snprintf(C.gauge_bright, sizeof C.gauge_bright, "own");
    }

    /* Spectrum at a fixed level: min = max = the level, no flash; dimmed in dark on and back to the owner's after */
    {
        char d2[200]; snprintf(d2, sizeof d2, "%s/fixed", lamp_dir); mkdir(d2, 0755);
        char f[260];
        snprintf(f, sizeof f, "%s/mode", d2); write_file(f, "spectrum\n");
        snprintf(f, sizeof f, "%s/brightness_fixed", d2); write_file(f, "1\n");
        snprintf(f, sizeof f, "%s/gauge_brightness", d2); write_file(f, "6\n");
        snprintf(f, sizeof f, "%s/gauge_dim", d2); write_file(f, "1\n");
        snprintf(f, sizeof f, "%s/flash_on_beat", d2); write_file(f, "1\n");
        char keep_svc[96], keep_lc[96]; snprintf(keep_svc, sizeof keep_svc, "%s", C.service_bin); snprintf(keep_lc, sizeof keep_lc, "%s", C.logcat_bin);
        load_config(d2);
        snprintf(C.service_bin, sizeof C.service_bin, "%s", keep_svc); snprintf(C.logcat_bin, sizeof C.logcat_bin, "%s", keep_lc);
        assert(C.bright_fixed && C.bright_min == 6 && C.bright_max == 6 && !flash_enabled());
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        write_file(st, "r=255 g=68 b=35 br=4 dm=1 dim=1\n");
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path);
        base.valid = 0; we_set_dim = 0; music_dim_sent = 0;
        snapshot_base(); take_over_lamp(); music_dim();
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dim=2"));
        restore_base();
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dim=1") && strstr(line, "br=4"));
        snprintf(f, sizeof f, "%s/mode", d2); write_file(f, "beat\n");
        snprintf(f, sizeof f, "%s/brightness_fixed", d2); write_file(f, "0\n");
        load_config(d2);
        snprintf(C.service_bin, sizeof C.service_bin, "%s", keep_svc); snprintf(C.logcat_bin, sizeof C.logcat_bin, "%s", keep_lc);
        assert(!C.bright_fixed && flash_enabled());
        snprintf(C.mode, sizeof C.mode, "spectrum"); base.valid = 0;
    }

    /* pulse in the drive-mode colour: the link goes on for the music and back off after; other modes turn it off */
    {
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        write_file(st, "r=255 g=68 b=35 br=4 dm=1 dim=1\n");
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path);
        snprintf(C.mode, sizeof C.mode, "pulse"); C.pulse_color = -2; C.override_drive_mode = 1; C.dry = 0;
        base.valid = 0; snapshot_base(); take_over_lamp();
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dm=2"));
        C.pulse_color = 10; drive_link_sync();                      /* switched to a picked colour mid-song */
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dm=1"));
        C.pulse_color = -2; drive_link_sync();
        restore_base();
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        if (!strstr(line, "dm=1")) { printf("after drive-mode pulse: %s", line); assert(0); }
        C.pulse_color = -1; snprintf(C.mode, sizeof C.mode, "spectrum"); base.valid = 0;
    }

    /* choosing the drive-mode colour turns the car's drive-mode link on at once, with no music, and keeps it */
    {
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        write_file(st, "r=255 g=68 b=35 br=4 dm=1 dim=1\n");
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path);
        snprintf(C.mode, sizeof C.mode, "pulse"); C.pulse_color = -2; pulse_car_applied = -1; base.valid = 0;
        pulse_car_colour(0);
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dm=2"));
        int sv[7]; assert(saved_load(sv) && sv[4] == 2 && sv[5] == 0);
        assert(pulse_car_applied == -2);
        /* then a colour is picked (not set as the car's): the link goes off, and stays off */
        C.pulse_color = 30; C.pulse_set_car = 0;
        pulse_car_colour(0);
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "dm=1"));
        assert(saved_load(sv) && sv[4] == 1);
        C.pulse_color = -1; snprintf(C.mode, sizeof C.mode, "spectrum"); pulse_car_applied = -1; base.valid = 0;
    }

    /* pulse "set as the car's colour": the lamp and the saved colour both take it, with no music playing */
    {
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        write_file(st, "r=255 g=68 b=35 br=4 dm=2 dim=1\n");                /* drive-mode-linked colour on */
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path);
        static char pc[200]; snprintf(pc, sizeof pc, "%s/user_color", lamp_dir); public_colour_path = pc;
        snprintf(C.mode, sizeof C.mode, "pulse"); C.pulse_color = 44; C.pulse_set_car = 1; pulse_car_applied = -1;
        base.valid = 0;
        pulse_car_colour(0);
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        char want[64]; snprintf(want, sizeof want, "r=%d g=%d b=%d br=4", WHEEL_R[44], WHEEL_G[44], WHEEL_B[44]);
        if (!strstr(line, want) || !strstr(line, "dm=1")) { printf("lamp after pulse colour: %s", line); assert(0); }
        int sv[7]; assert(saved_load(sv) && sv[0] == WHEEL_R[44] && sv[1] == WHEEL_G[44] && sv[2] == WHEEL_B[44] && sv[4] == 1 && sv[5] == 0);
        FILE *pf = fopen(pc, "r"); char pl[64]; assert(pf && fgets(pl, sizeof pl, pf)); fclose(pf);
        char pw[64]; snprintf(pw, sizeof pw, "%d %d %d 1\n", WHEEL_R[44], WHEEL_G[44], WHEEL_B[44]); assert(!strcmp(pl, pw));
        assert(pulse_car_applied == 44);
        write_file(st, "r=1 g=2 b=3 br=4 dm=1 dim=1\n");
        pulse_car_colour(0);                                         /* already done: not sent again */
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        assert(strstr(line, "r=1 g=2 b=3"));
        C.pulse_set_car = 0; pulse_car_colour(0); assert(pulse_car_applied == -3);
        C.pulse_color = -1; snprintf(C.mode, sizeof C.mode, "spectrum");
    }

    /* the settings from before Light Vibes: kept when it's turned on, put back when it's turned off, whatever happened in
       between; only Pulse's "also set as car's color" changes them */
    {
        setenv("LAMP_MODE", "ok", 1);
        char st[300], line[120], op[200]; snprintf(st, sizeof st, "%s/state", lamp_dir);
        snprintf(op, sizeof op, "%s/original_light", lamp_dir); original_path = op; remove(op);
        write_file(st, "r=255 g=68 b=35 br=7 dm=2 dim=2\n");
        memset(lamp_stamp, 0, sizeof lamp_stamp); remove(light_state_path); base.valid = 0;
        original_record();
        int o[7]; assert(light_file_load(op, o) && o[0] == 255 && o[1] == 68 && o[2] == 35 && o[3] == 7 && o[4] == 2 && o[6] == 2);
        /* Light Vibes changes things: a picked colour turns the link off, dimming goes off */
        write_file(st, "r=1 g=2 b=3 br=3 dm=1 dim=1\n");
        original_record();                                           /* already kept: not replaced */
        assert(light_file_load(op, o) && o[0] == 255 && o[4] == 2);
        original_restore();
        FILE *sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        if (!strstr(line, "r=255 g=68 b=35 br=7 dm=2") || !strstr(line, "dim=2")) { printf("lamp after putting back: %s", line); assert(0); }
        assert(!light_file_load(op, o));                              /* done with: the next start keeps a new one */
        /* "also set as car's color": that colour is what goes back, with the link off so it shows */
        original_record();
        snprintf(C.mode, sizeof C.mode, "pulse"); C.pulse_color = 44; C.pulse_set_car = 1; pulse_car_applied = -1; base.valid = 0;
        pulse_car_colour(0);
        assert(light_file_load(op, o) && o[0] == WHEEL_R[44] && o[1] == WHEEL_G[44] && o[2] == WHEEL_B[44] && o[4] == 1 && o[3] == 7);
        write_file(st, "r=9 g=9 b=9 br=2 dm=2 dim=1\n");
        original_restore();
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf);
        char want[80]; snprintf(want, sizeof want, "r=%d g=%d b=%d br=7 dm=1", WHEEL_R[44], WHEEL_G[44], WHEEL_B[44]);
        if (!strstr(line, want)) { printf("lamp after putting back the car colour: %s", line); assert(0); }
        /* switched off in the app: Pulse leaves the lamp alone */
        C.enabled = 0; C.pulse_color = -2; pulse_car_applied = -1; write_file(st, "r=9 g=9 b=9 br=2 dm=1 dim=1\n");
        pulse_car_colour(0);
        sf = fopen(st, "r"); assert(sf && fgets(line, sizeof line, sf)); fclose(sf); assert(strstr(line, "dm=1"));
        C.enabled = 1; C.pulse_color = -1; C.pulse_set_car = 0; snprintf(C.mode, sizeof C.mode, "spectrum"); pulse_car_applied = -1; base.valid = 0;
        original_path = "/nonexistent/original_light";
    }

    /* the check during a song: a lamp that kept the user's colour, or stopped answering, gets noticed */
    setenv("LAMP_MODE", "ignore_colour", 1);
    base.valid = 1; base.r = 255; base.g = 68; base.b = 35; base.bright_raw = 2; base.drive_mode = 1;
    char sp[300]; snprintf(sp, sizeof sp, "%s/state", lamp_dir); write_file(sp, "r=255 g=68 b=35 br=2 dm=1\n");
    last_rgb[0] = 16; last_rgb[1] = 67; last_rgb[2] = 244; last_raw = 9;
    memset(&chk, 0, sizeof chk);
    #define RUN_CHECK() do { check_start(); assert(check_pid > 0); int st_; pid_t p_ = check_pid; waitpid(p_, &st_, 0); check_pid = 0; check_finished(st_); } while (0)
    RUN_CHECK();
    assert(chk.colour_stuck == 1 && chk.bright_stuck == 1 && chk.ok == 0);
    write_file(sp, "r=16 g=67 b=244 br=9 dm=1\n");
    RUN_CHECK();
    assert(chk.ok == 1);
    setenv("LAMP_MODE", "silent", 1);
    int seen[6]; lamp_last_status(seen);     /* as the service does when it takes the lamp over */
    RUN_CHECK();
    assert(chk.no_reply == 1);
    base.valid = 0;

    /* the report: answers are limited to known words, and it reaches a stick */
    char usb[120], rep[120];
    snprintf(usb, sizeof usb, "%s/stick", lamp_dir); mkdir(usb, 0755);
    snprintf(rep, sizeof rep, "%s/public", lamp_dir);
    report_dir = rep;
    usb_roots[0] = usb; usb_any_folder = 1;
    log_path = "/nonexistent/log";
    snprintf(dpath, sizeof dpath, "%s/diag/request", lamp_dir);
    write_file(dpath, "save colour=no bright=../../etc\n");
    assert(diag_poll() == 0);
    assert(!strcmp(result_value("stage", v2, sizeof v2), "saved"));
    assert(!strcmp(result_value("seen", v2, sizeof v2), "no/unknown"));
    assert(strstr(result_value("usb", v2, sizeof v2), "Light Vibes reports/light-vibes-report-"));
    char rp[200]; snprintf(rp, sizeof rp, "%s/report.txt", rep);
    FILE *rf = fopen(rp, "r"); assert(rf); char l1[100]; assert(fgets(l1, sizeof l1, rf)); assert(!strncmp(l1, "Light Vibes report", 18)); fclose(rf);
    /* no stick: an empty folder isn't one */
    usb_any_folder = 0;
    write_file(dpath, "save colour=yes bright=yes\n");
    assert(diag_poll() == 0);
    assert(!strcmp(result_value("usb", v2, sizeof v2), "none"));
    /* a link left where the result goes isn't followed out of the folder */
    char tmp_link[220], target[220];
    snprintf(tmp_link, sizeof tmp_link, "%s/diag/.result.tmp", lamp_dir);
    snprintf(target, sizeof target, "%s/target", lamp_dir); write_file(target, "keep\n");
    assert(!symlink(target, tmp_link));
    diag_flush("done");
    rf = fopen(target, "r"); assert(rf && fgets(l1, sizeof l1, rf)); assert(!strcmp(l1, "keep\n")); fclose(rf);

    char rm[120]; snprintf(rm, sizeof rm, "rm -rf %s", lamp_dir); assert(!system(rm));
    remove(path);

    puts("lamp tests passed");
    return 0;
}
