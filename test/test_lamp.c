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
"  if [ \"$LAMP_MODE\" = watch_seq ]; then\n"
"    if [ $n -le 2 ]; then r=382; g=471; b=277; dm=1; else r=3; g=199; b=192; dm=2; echo 'watch stop' > $D/diag/request; fi\n"
"  fi\n"
"  printf '11-06 10:00:00.%03d D/SystemService(  651): SYS_SML_STATUS_M_RESP: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x \\n' $n $((r>>8)) $((r&255)) $((g>>8)) $((g&255)) $((b>>8)) $((b&255)) $br ${dim:-1} $dm >> $D/log; }\n"
"dec() { echo $(( (($1&255)<<8) | (($1>>8)&255) )); }\n"
"if [ \"$2\" = com.hkmc.micom.hev ]; then v=$(grep \"^$5 \" $D/hev | cut -d' ' -f2); printf 'Result: Parcel(00000000 %08x   \\x27....\\x27)\\n' ${v:-4294967295}; exit 0; fi\n"
"case $3 in\n"
" 76) answer ;;\n"
" 77) if [ $5 = 5 ]; then [ \"$LAMP_MODE\" = ignore_bright ] || br=$7;\n"
"     elif [ $5 = 9 ]; then dm=$7;\n"
"     elif [ $5 = 8 ]; then dim=$7;\n"
"     fi ;;\n"
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
    char p[400], line[1024]; snprintf(p, sizeof p, "%s/diag/result", lamp_dir);
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
        write_file(bp, "ro.product.name=wp_daudioplus_kia_us\nro.build.product=KIA.USA.0000.V015.012.260507\n");   /* the Niro EV's file */
        assert(!strcmp(detect_palette(), "kia"));
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
    assert(!strcmp(result_value("b_ceil", v2, sizeof v2), "12,15"));
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

    /* drive history: rows while the car is on, a new file for each drive, the one before kept unless it was too short */
    {
        static char hd[200]; snprintf(hd, sizeof hd, "%s/hist", lamp_dir); history_dir = hd;
        char d0[300], d1[300]; snprintf(d0, sizeof d0, "%s/drive_0.csv", hd); snprintf(d1, sizeof d1, "%s/drive_1.csv", hd);
        #define ROWS(path) ({ FILE *f_ = fopen(path, "r"); int n_ = -1; char l_[200]; if (f_) { n_ = 0; while (fgets(l_, sizeof l_, f_)) if (l_[0] != '#') n_++; fclose(f_); } n_; })
        write_file(bp, "boot-3\n"); trip_begin(); memset(&hist, 0, sizeof hist);
        write_file(hp, "12 2\n31 2000\n44 0\n66 0\n71 0\n72 0\n89 0\n90 0\n1 2\n2 30\n");
        energy_sample(1); history_tick(); history_stop();
        assert(ROWS(d0) == -1);                                       /* car off: nothing */
        write_file(hp, "12 2\n31 2000\n44 1\n66 0\n71 0\n72 0\n89 0\n90 0\n1 2\n2 30\n");
        energy_sample(1);
        for (int i = 0; i < 70; i++) history_tick();
        /* a client connecting now hears which drive, and the rows not on disk yet */
        int sv2[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv2));
        history_hello(sv2[0]);
        char hb[4096]; int hn = (int)recv(sv2[1], hb, sizeof hb - 1, MSG_DONTWAIT); assert(hn > 0); hb[hn] = 0;
        assert(!strncmp(hb, "histdrive ", 10) && strstr(hb, "\nhist ") != NULL);
        int buffered = 0; for (char *q = hb; (q = strstr(q, "\nhist ")); q++) buffered++;
        assert(buffered > 0 && buffered < 70);                       /* only what the file lacks */
        close(sv2[0]); close(sv2[1]);
        history_stop();
        assert(ROWS(d0) == 70);
        FILE *hf = fopen(d0, "r"); char hl[200]; assert(hf && fgets(hl, sizeof hl, hf) && !strncmp(hl, "# drive boot-3 2000 ", 20));
        assert(fgets(hl, sizeof hl, hf)); fclose(hf);
        long long ts; char sp[16], kw[16], tr[16], av[16];
        assert(sscanf(hl, "%lld %15s %15s %15s %15s", &ts, sp, kw, tr, av) == 5 && !strcmp(sp, "-") && !strcmp(tr, "-") && !strcmp(av, "3.00"));
        memset(&hist, 0, sizeof hist);                                /* the service restarts mid-drive: same file */
        history_tick(); history_stop();
        assert(ROWS(d0) == 71 && ROWS(d1) == -1);
        write_file(bp, "boot-4\n"); trip_begin(); energy_sample(1);   /* the next drive */
        for (int i = 0; i < 5; i++) history_tick();
        history_stop();
        assert(ROWS(d0) == 5 && ROWS(d1) == 71);
        write_file(bp, "boot-5\n"); trip_begin(); energy_sample(1);   /* that one was too short to keep */
        for (int i = 0; i < 3; i++) history_tick();
        history_stop();
        assert(ROWS(d0) == 3 && ROWS(d1) == 71);
        history_dir = "/nonexistent";
    }

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

    /* ---------- stock color capture (snap & watch) ---------- */
    {
        usb_roots[0] = usb; usb_any_folder = 1;
        diag_watch_ms = 2;
        char stf[260], logf[260], nf[260], v3[512], cap_usb[320];
        snprintf(stf, sizeof stf, "%s/state", lamp_dir);
        snprintf(logf, sizeof logf, "%s/log", lamp_dir);
        snprintf(nf, sizeof nf, "%s/n", lamp_dir);
        snprintf(cap_usb, sizeof cap_usb, "%s/Light Vibes reports/captured-colors.txt", usb);

        /* 1. Manual "Capture current" (snap) reads 0x01xx stock RGB + metadata and appends to USB captured-colors.txt */
        setenv("LAMP_MODE", "ok", 1);
        write_file(stf, "r=382 g=471 b=277 br=6 dm=1 dim=2\n");
        write_file(logf, ""); write_file(nf, "0\n"); memset(lamp_stamp, 0, sizeof lamp_stamp);
        write_file(dpath, "snap\n");
        assert(diag_poll() == 1);
        diag_dispatch();
        assert(diag_poll() == 0);
        assert(!strcmp(result_value("stage", v3, sizeof v3), "snap_done"));
        assert(!strcmp(result_value("reply", v3, sizeof v3), "1"));
        assert(!strcmp(result_value("rgb", v3, sizeof v3), "382,471,277"));
        assert(!strcmp(result_value("hex", v3, sizeof v3), "0x017E,0x01D7,0x0115"));
        assert(!strcmp(result_value("bright", v3, sizeof v3), "6") && !strcmp(result_value("dim", v3, sizeof v3), "2") && !strcmp(result_value("dm", v3, sizeof v3), "1"));
        assert(strstr(result_value("usb", v3, sizeof v3), "Light Vibes reports/captured-colors.txt"));

        /* 2. Hands-off "Auto-capture" (watch) deduplicates steady polls, logs Drive Mode changes, and stops cleanly on "watch stop" */
        setenv("LAMP_MODE", "watch_seq", 1);
        write_file(stf, "r=382 g=471 b=277 br=5 dm=1 dim=1\n");
        write_file(logf, ""); write_file(nf, "0\n"); memset(lamp_stamp, 0, sizeof lamp_stamp);
        write_file(dpath, "watch\n");
        assert(diag_poll() == 1);
        diag_dispatch();
        assert(diag_poll() == 0);
        assert(!strcmp(result_value("stage", v3, sizeof v3), "watch_done"));
        assert(!strcmp(result_value("watch_count", v3, sizeof v3), "2"));
        assert(!strcmp(result_value("items", v3, sizeof v3), "3,199,192,5,1,2;382,471,277,5,1,1"));
        assert(saved_load(s) && s[5] == 0);

        FILE *cf = fopen(cap_usb, "r"); assert(cf);
        char cap_all[4096]; size_t cn = fread(cap_all, 1, sizeof cap_all - 1, cf); fclose(cf);
        cap_all[cn] = 0;
        assert(strstr(cap_all, "source=manual") && strstr(cap_all, "source=auto#2"));
    }

    /* ---------- zero-lag beat onset, Beat wheel permutation, Flash blackout, Pulse envelope, and Spectrum auto-adapt ---------- */
    {
        load_config("/nonexistent");
        C.dry = 1; C.debug = 0; rate = 48000;

        /* 1. Hue-aware vivid colour filter, Hyundai Ioniq 5 0x01xx stock mood colours, Beat stepping, and #RRGGBB snapping */
        set_palette("hyundai");
        assert(wheel_n == 74);
        for (int i = 16; i <= 21; i++) assert(!wheel_saturated(i));   /* salmon / flesh pinks excluded */
        for (int i = 27; i <= 41; i++) assert(!wheel_saturated(i));   /* pale peach / tan / cream / icy mints excluded */
        assert(!wheel_saturated(64) && !wheel_saturated(65) && !wheel_saturated(66) && !wheel_saturated(71));
        assert(wheel_saturated(67) && wheel_saturated(68) && wheel_saturated(69) && wheel_saturated(70) && wheel_saturated(72) && wheel_saturated(73));
        assert(nearest_vivid_wheel(0, 255, 64) == 69 && WHEEL_R[69] == 382 && WHEEL_G[69] == 471 && WHEEL_B[69] == 277);
        assert(nearest_vivid_wheel(254, 168, 17) == 70 && WHEEL_R[70] == 510 && WHEEL_G[70] == 424 && WHEEL_B[70] == 273);
        assert(nearest_vivid_wheel(3, 199, 192) == 68 && nearest_vivid_wheel(24, 142, 253) == 67);
        assert(wheel_walk(-50, -50, 0, 69, 67) == 69 && wheel_walk(0, -50, 0, 69, 67) == 67 && wheel_walk(150, 0, 150, 67, 22) == 22);
        for (int kw2 = -50; kw2 <= 150; kw2 += 10) {
            int wi = kw2 <= 0 ? wheel_walk(kw2, -50, 0, 69, 67) : wheel_walk(kw2, 0, 150, 67, 22);
            assert(wheel_saturated(wi) && !wheel_pale(wi));
        }
        set_palette("kia");
        assert(wheel_n == 64);
        for (int i = 28; i <= 39; i++) assert(!wheel_saturated(i));   /* pale peach / straw / white excluded */
        for (int i = 40; i <= 49; i++) assert(wheel_saturated(i));    /* Kia vivid greens retained */

        const char *pals[2] = {"kia", "hyundai"};
        int steps[2] = {19, 23};
        for (int p = 0; p < 2; p++) {
            set_palette(pals[p]);
            for (int si = 0; si < 2; si++) {
                int seen_col[MAXWHEEL] = {0}, idx = 22, uniq = 0;
                for (int k = 0; k < 50; k++) {
                    idx = wheel_next(idx, steps[si]);
                    assert(wheel_saturated(idx));
                    if (!seen_col[idx]) { seen_col[idx] = 1; uniq++; }
                }
                assert(uniq >= 35);
                if (p == 1) assert(seen_col[69] && seen_col[70]);     /* Hyundai Beat visits 0x01xx Emerald Green & Marigold Gold */
            }
        }
        /* #RRGGBB bands snap to vivid swatches on Kia AND re-snap cleanly (including 0x01xx stock colours) on Hyundai */
        set_palette("kia");
        C.nbands = parse_bands(DEFAULT_BANDS, C.bands);
        assert(C.nbands == 5);
        for (int i = 0; i < C.nbands; i++) {
            assert(palette_has_rgb(KIA_R, KIA_G, KIA_B, C.bands[i].r, C.bands[i].g, C.bands[i].b));
            int wi = nearest_vivid_wheel(C.bands[i].r, C.bands[i].g, C.bands[i].b);
            assert(WHEEL_R[wi] == C.bands[i].r && WHEEL_G[wi] == C.bands[i].g && WHEEL_B[wi] == C.bands[i].b);
        }
        set_palette("hyundai");
        assert(C.bands[2].r == 382 && C.bands[2].g == 471 && C.bands[2].b == 277);   /* #00FF40 Mids -> #69 Healing Forest */
        for (int i = 0; i < C.nbands; i++) {
            assert(palette_has_rgb(HYUNDAI_R, HYUNDAI_G, HYUNDAI_B, C.bands[i].r, C.bands[i].g, C.bands[i].b));
            int wi = nearest_vivid_wheel(C.bands[i].r & 0xff, C.bands[i].g & 0xff, C.bands[i].b & 0xff);
            assert(WHEEL_R[wi] == C.bands[i].r && WHEEL_G[wi] == C.bands[i].g && WHEEL_B[wi] == C.bands[i].b);
        }
        set_palette("kia");

        /* 2. Zero-lag transient detection + sustained-note re-arming:
              steady 55 Hz bass for 1 s, then a 300 ms 65 Hz kick at t=1000 ms (must fire in the exact first 10.67 ms block
              and NOT double-fire at t=1150 ms despite min_beat_ms=140), then a fast second kick at t=1500 ms */
        C.min_beat_ms = 140; C.sensitivity = 145; C.beat_low_hz = 40; C.beat_high_hz = 180;
        Ana a; ana_init(&a);
        short pcm_buf[PERIOD * CH];
        long long sample_idx = 0;
        int kick1_block = -1, extra_during_tail = 0, kick2_fired = 0;
        for (int blk = 0; blk < 160; blk++) {
            long long t = blk * 1000LL * PERIOD / rate;
            for (int i = 0; i < PERIOD; i++, sample_idx++) {
                double ts = sample_idx / (double)rate;
                double sig = 0.12 * sin(2 * M_PI * 55.0 * ts);
                if (blk >= 94 && blk < 122) {   /* ~1002 ms to ~1301 ms: 300 ms decaying kick */
                    double tk = (sample_idx - 94LL * PERIOD) / (double)rate;
                    sig += 0.60 * sin(2 * M_PI * 65.0 * tk) * exp(-tk * 10.0);
                }
                if (blk >= 141 && blk < 155) {  /* ~1504 ms: second kick */
                    double tk2 = (sample_idx - 141LL * PERIOD) / (double)rate;
                    sig += 0.60 * sin(2 * M_PI * 65.0 * tk2) * exp(-tk2 * 12.0);
                }
                int v16 = (int)lround(sig * 20000.0);
                pcm_buf[i * CH] = pcm_buf[i * CH + 1] = (short)v16;
            }
            Frame fr = ana_block(&a, pcm_buf, PERIOD, t);
            if (blk >= 94 && blk <= 95 && fr.beat && kick1_block < 0) kick1_block = blk;
            else if (blk > 95 && blk < 122 && fr.beat) extra_during_tail++;
            if (blk >= 141 && blk <= 142 && fr.beat) kick2_fired = 1;
        }
        assert(kick1_block == 94);       /* zero-lag: detected in the exact block where the kick starts */
        assert(extra_during_tail == 0);  /* no false re-trigger during the 300 ms decaying 808 tail */
        assert(kick2_fired == 1);

        /* 3. Flash on beat with level 0 blackout floor: snaps to raw 11 (UI 10) on beat, drops to raw 1 (UI 0) right after flash_ms */
        snprintf(C.mode, sizeof C.mode, "beat");
        C.flash_on_beat = 1; C.flash_ms = 70; C.bright_min = 0; C.bright_max = 10; C.min_interval_ms = 50;
        Drv d; memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
        Frame fb; memset(&fb, 0, sizeof fb); fb.db = -18; fb.beat = 1;
        drive(&d, &a, fb, 1000);
        assert(d.sent_level == 10);
        fb.beat = 0;
        drive(&d, &a, fb, 1040);
        assert(d.sent_level == 10);      /* still inside 70 ms flash hold */
        drive(&d, &a, fb, 1075);
        assert(d.sent_level == 0);       /* flash expired: snaps straight to blackout (UI 0 -> raw 1) */

        /* 4. Pulse mode: holds peak 10 for PULSE_HOLD_MS (105 ms), ignores non-rising steady bass, plunges to bright_min between beats */
        snprintf(C.mode, sizeof C.mode, "pulse");
        C.pulse_color = -1; C.pulse_decay = 35; C.bright_min = 1; C.bright_max = 10; C.min_interval_ms = 30;
        memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
        fb.beat = 1; fb.onset_ratio = 3.0;
        drive(&d, &a, fb, 2000);
        assert(d.sent_level == 10);
        fb.beat = 0; fb.onset_ratio = 0; /* steady bass between beats produces onset_ratio == 0 */
        drive(&d, &a, fb, 2090);
        assert(d.sent_level == 10);      /* held through PULSE_HOLD_MS (105 ms) so car LIN lamp completes fade-up ramp */
        for (long long tp2 = 2110; tp2 <= 2420; tp2 += 11) drive(&d, &a, fb, tp2);
        assert(d.sent_level == 1);       /* reaches floor well before next 120 BPM beat at 2500 ms */

        /* 5. Spectrum auto-adaptation + 115 ms minimum dwell:
              a quieter treble entry wins when it surges above its own baseline, while manual mode stays on bass;
              sub-100 ms non-beat wobble stays locked to avoid LIN mid-fade mud */
        C.nbands = parse_bands("40-200:22:1 2000-5000:56:1", C.bands);
        C.band_smoothing_ms = 0; C.switch_margin = 15;
        int dyn_band = -1, static_band = -1;
        for (int dyn = 0; dyn <= 1; dyn++) {
            C.spectrum_dynamic = dyn;
            ana_init(&a);
            sample_idx = 0;
            Frame fs; memset(&fs, 0, sizeof fs);
            for (int blk = 0; blk < 180; blk++) {
                long long t = blk * 1000LL * PERIOD / rate;
                double treb_amp = blk < 140 ? 0.02 : 0.09;   /* +13 dB treble entry at ~1.5 s, still below 0.25 bass raw energy */
                for (int i = 0; i < PERIOD; i++, sample_idx++) {
                    double ts = sample_idx / (double)rate;
                    double sig = 0.25 * sin(2 * M_PI * 90.0 * ts) + treb_amp * sin(2 * M_PI * 3000.0 * ts);
                    short s16 = (short)lround(sig * 20000.0);
                    pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
                }
                fs = ana_block(&a, pcm_buf, PERIOD, t);
            }
            if (dyn) dyn_band = fs.band; else static_band = fs.band;
        }
        assert(static_band == 0);   /* manual boost 1:1 stays pinned on louder 90 Hz bass */
        assert(dyn_band == 1);      /* spectrum_dynamic=1 switches cleanly to the newly-entering 3 kHz lead */
        {   /* 115 ms dwell check: mild non-beat alternation at +50 ms is held back, then switches cleanly at >= 115 ms */
            C.spectrum_dynamic = 0;
            ana_init(&a);
            sample_idx = 0;
            Frame fd0 = {0}, fd50 = {0}, fd130 = {0};
            for (int blk = 0; blk < 20; blk++) {
                long long t = blk * 1000LL * PERIOD / rate;
                int hi_on = blk >= 4;   /* switch from 90 Hz bass to mildly louder 3 kHz at t=42 ms */
                for (int i = 0; i < PERIOD; i++, sample_idx++) {
                    double ts = sample_idx / (double)rate;
                    double sig = hi_on ? 0.22 * sin(2 * M_PI * 3000.0 * ts) : 0.20 * sin(2 * M_PI * 90.0 * ts);
                    pcm_buf[i * CH] = pcm_buf[i * CH + 1] = (short)lround(sig * 18000.0);
                }
                Frame fx = ana_block(&a, pcm_buf, PERIOD, t);
                if (blk == 3) fd0 = fx;
                if (blk == 6) fd50 = fx;    /* ~64 ms after initial lock at t=0: still within 115 ms dwell */
                if (blk == 13) fd130 = fx;  /* ~138 ms: past 115 ms dwell, switches cleanly */
            }
            assert(fd0.band == 0 && fd50.band == 0 && fd130.band == 1);
        }

        /* 6. Real-time Auto-BPM detection, tempo-linked timing, and >=450 ms CarPlay track-skip silence reset */
        C.auto_bpm = 1; C.flash_ms = 125; C.min_beat_ms = 200; C.pulse_decay = 35;
        Ana a90, a140, asine;
        ana_init(&a90); ana_init(&a140); ana_init(&asine);
        double bpms[2] = {90.0, 140.0};
        Ana *aptrs[2] = {&a90, &a140};
        for (int bi = 0; bi < 2; bi++) {
            double period_s = 60.0 / bpms[bi];
            long long sidx = 0;
            for (int blk = 0; blk < 620; blk++) {   /* ~6.6 s */
                long long t = blk * 1000LL * PERIOD / rate;
                for (int i = 0; i < PERIOD; i++, sidx++) {
                    double ts = sidx / (double)rate;
                    double ph = fmod(ts, period_s);
                    double sig = 0.06 * sin(2 * M_PI * 220.0 * ts) + 0.65 * sin(2 * M_PI * 68.0 * ph) * exp(-ph * 22.0);
                    short s16 = (short)lround(sig * 20000.0);
                    pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
                }
                ana_block(aptrs[bi], pcm_buf, PERIOD, t);
            }
        }
        assert(fabs(a90.bpm - 90.0) < 2.5 && a90.bpm_conf > 0.50);
        assert(fabs(a140.bpm - 140.0) < 2.5 && a140.bpm_conf > 0.50);
        assert(eff_flash_ms(&a90) > eff_flash_ms(&a140) && eff_flash_ms(&a140) >= 105);
        assert(eff_min_beat_ms(&a90) > eff_min_beat_ms(&a140));
        assert(eff_pulse_hold_ms(&a90) > eff_pulse_hold_ms(&a140));
        assert(eff_pulse_decay_frac(&a90) > eff_pulse_decay_frac(&a140));
        C.auto_bpm = 0;   /* manual timing override ignores locked BPM */
        assert(eff_flash_ms(&a90) == 125 && eff_min_beat_ms(&a90) == 200 && eff_pulse_hold_ms(&a90) == PULSE_HOLD_MS);
        C.auto_bpm = 1;
        /* brief 200 ms pause keeps BPM lock, while >=450 ms CarPlay track-skip silence resets BPM cleanly */
        memset(pcm_buf, 0, sizeof pcm_buf);
        for (int blk = 620; blk < 638; blk++) ana_block(&a90, pcm_buf, PERIOD, blk * 1000LL * PERIOD / rate);
        assert(a90.bpm > 85.0 && a90.bpm_conf > 0.35);
        for (int blk = 638; blk < 670; blk++) ana_block(&a90, pcm_buf, PERIOD, blk * 1000LL * PERIOD / rate);
        assert(a90.bpm == 0.0 && a90.bpm_conf == 0.0 && a90.flux_filled == 0);
        /* beatless steady tone does not hallucinate BPM */
        sample_idx = 0;
        for (int blk = 0; blk < 400; blk++) {
            long long t = blk * 1000LL * PERIOD / rate;
            for (int i = 0; i < PERIOD; i++, sample_idx++) {
                short s16 = (short)lround(0.25 * sin(2 * M_PI * 110.0 * sample_idx / rate) * 20000.0);
                pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
            }
            ana_block(&asine, pcm_buf, PERIOD, t);
        }
        assert(bpm_weight(&asine) == 0.0);
        /* 3-band BPM tracker locks 115 BPM snare/hi-hat transients even over a continuous 65 Hz sub-bass drone */
        Ana asub; ana_init(&asub);
        sample_idx = 0;
        for (int blk = 0; blk < 680; blk++) {
            long long t = blk * 1000LL * PERIOD / rate;
            for (int i = 0; i < PERIOD; i++, sample_idx++) {
                double ts = sample_idx / (double)rate;
                double ph = fmod(ts, 60.0 / 115.0);
                double sig = 0.28 * sin(2 * M_PI * 65.0 * ts)
                           + (0.42 * sin(2 * M_PI * 520.0 * ph) + 0.30 * sin(2 * M_PI * 2600.0 * ph)) * exp(-ph * 28.0);
                short s16 = (short)lround(sig * 18000.0);
                pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
            }
            ana_block(&asub, pcm_buf, PERIOD, t);
        }
        assert(fabs(asub.bpm - 115.0) < 2.5 && asub.bpm_conf > 0.35);

        /* 7. Visualizer modes on Hyundai Ioniq 5 palette (legacy drop migration, Duet pair walk, Synesthesia, Ember sparks, Phase Drift, Phase Pulse, Night Drive) */
        {   /* Legacy mode=drop config migrates cleanly to synesthesia */
            char saved_svc[sizeof C.service_bin], saved_lc[sizeof C.logcat_bin], mdir[128], mfile[160];
            snprintf(saved_svc, sizeof saved_svc, "%s", C.service_bin);
            snprintf(saved_lc, sizeof saved_lc, "%s", C.logcat_bin);
            snprintf(mdir, sizeof mdir, "%s/mig", lamp_dir);
            mkdir(mdir, 0755);
            snprintf(mfile, sizeof mfile, "%s/mode", mdir);
            write_file(mfile, "drop\n");
            load_config(mdir);
            assert(!strcmp(C.mode, "synesthesia"));
            snprintf(C.service_bin, sizeof C.service_bin, "%s", saved_svc);
            snprintf(C.logcat_bin, sizeof C.logcat_bin, "%s", saved_lc);
        }
        set_palette("hyundai");
        C.min_interval_ms = 20; C.bright_min = 1; C.bright_max = 10; C.flash_ms = 110; C.flash_on_beat = -1; C.bright_fixed = 0;
        {   /* Duet locked (duet_beats=0) & beat-synced pair walk (duet_beats=2) */
            snprintf(C.mode, sizeof C.mode, "duet");
            C.duet_bed = 68; C.duet_accent = -1; C.duet_beats = 0;
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 68;
            Frame fd = {0}; fd.db = -18; fd.beat = 1; fd.mid_norm = 0.6;
            drive(&d, &asub, fd, 1000);
            int opp = vivid_at_hue(wheel_hue(68) + 180.0);
            assert(d.want_r == WHEEL_R[opp] && d.sent_level == 10);
            fd.beat = 0;
            drive(&d, &asub, fd, 1250);
            assert(d.want_r == WHEEL_R[68] && d.sent_level >= 1 && d.sent_level <= 6);

            /* Beat-synced pair walk: steps complementary bed+accent pair around FULL_WHEEL_HUES every 2 beats */
            C.duet_bed = -1; C.duet_accent = -1; C.duet_beats = 2;
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
            fd.beat = 0; drive(&d, &asub, fd, 2000);
            int bed0 = d.idx;
            fd.beat = 1; drive(&d, &asub, fd, 2100);   /* beat 1: stays on pair 0, flashes complement */
            int acc0 = d.idx;
            fd.beat = 0; drive(&d, &asub, fd, 2300);
            assert(d.idx == bed0 && acc0 != bed0);
            fd.beat = 1; drive(&d, &asub, fd, 2500);   /* beat 2: advances to pair 1! */
            int acc1 = d.idx;
            fd.beat = 0; drive(&d, &asub, fd, 2700);
            int bed1 = d.idx;
            assert(bed0 == 68 && bed1 == 67 && acc1 != acc0);   /* Mindful Teal (#68) pair -> Ocean Blue (#67) pair */
        }
        {   /* Synesthesia: continuous pitch centroid maps 90 Hz bass vs 3200 Hz lead to distinct vivid colours */
            snprintf(C.mode, sizeof C.mode, "synesthesia");
            C.syn_shift = 0; C.band_smoothing_ms = 25; C.flash_on_beat = 0;
            Ana asyn; ana_init(&asyn);
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
            Frame fs = {0};
            sample_idx = 0;
            for (int blk = 0; blk < 60; blk++) {
                for (int i = 0; i < PERIOD; i++, sample_idx++) {
                    short s16 = (short)lround(0.28 * sin(2 * M_PI * 90.0 * sample_idx / rate) * 18000.0);
                    pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
                }
                fs = ana_block(&asyn, pcm_buf, PERIOD, blk * 11LL);
            }
            drive(&d, &asyn, fs, 700);
            int low_idx = d.idx;
            for (int blk = 60; blk < 120; blk++) {
                for (int i = 0; i < PERIOD; i++, sample_idx++) {
                    short s16 = (short)lround(0.28 * sin(2 * M_PI * 3200.0 * sample_idx / rate) * 18000.0);
                    pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
                }
                fs = ana_block(&asyn, pcm_buf, PERIOD, blk * 11LL);
            }
            drive(&d, &asyn, fs, 1400);
            int high_idx = d.idx;
            assert(fs.pitch_norm > 0.70 && low_idx != high_idx && wheel_saturated(low_idx) && wheel_saturated(high_idx));
            C.flash_on_beat = -1;
        }
        {   /* Ember: beat ignites Marigold Gold flare (#70), cools through crimson/rose to purple, and snare spark re-ignites! */
            snprintf(C.mode, sizeof C.mode, "ember");
            C.ember_theme = 0; C.pulse_decay = 38;
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
            Frame fe = {0}; fe.db = -16; fe.beat = 1; fe.onset_ratio = 3.0;
            drive(&d, &asub, fe, 2000);
            assert(d.idx == 70 && d.sent_level == 10);   /* Fire peak flare hits #70 Marigold Gold! */
            fe.beat = 0; fe.onset_ratio = 0;
            int cool_idx = ember_wheel_at(0, 0.0), mid_seen = 0;
            for (long long te = 2120; te <= 3400; te += 20) {
                drive(&d, &asub, fe, te);
                if (d.idx != 70 && d.idx != cool_idx) mid_seen = 1;   /* sweeps through intermediate crimson/rose stops */
            }
            assert(mid_seen && d.idx == cool_idx && d.sent_level == 1);
            fe.spark_norm = 0.92;                        /* off-beat snare/clap transient re-ignites cooled ember! */
            drive(&d, &asub, fe, 3460);
            assert(d.idx != cool_idx && d.sent_level >= 5);
        }
        {   /* Phase Drift (8-beat steady) & Phase Pulse (2-beat Full Wheel + smooth beat-pulse brightness) */
            snprintf(C.mode, sizeof C.mode, "drift");
            C.drift_theme = 0; C.drift_beats = 8; C.bright_fixed = 1; C.bright_min = 10; C.bright_max = 10; C.flash_on_beat = 0;
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
            Frame fdr = {0}; fdr.db = -18; fdr.beat = 1;
            for (int b = 1; b <= 7; b++) drive(&d, &asub, fdr, 1000 + b * 400LL);
            int c7 = d.idx;
            drive(&d, &asub, fdr, 4200);
            int c8 = d.idx;
            assert(c7 == 69 && c8 != 69 && d.want_level == 10);   /* holds #69 Emerald for 7 beats, steps on beat 8 at steady 10 */

            /* Phase Pulse: Full Wheel (theme 4) visits all 6 vivid Hyundai stock colours + beat-pulse brightness (flash_on_beat=2) */
            C.drift_theme = 4; C.drift_beats = 2; C.bright_fixed = 0; C.bright_min = 1; C.bright_max = 10; C.flash_on_beat = 2; C.pulse_decay = 40;
            int seen_stock = 0;
            for (int st = 0; st < 10; st++) {
                int w = drift_wheel_at(4, st);
                assert(wheel_saturated(w));
                if (w >= 67) seen_stock++;
            }
            assert(seen_stock >= 6);
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1;
            fdr.beat = 1; drive(&d, &asub, fdr, 5000);
            int p0 = d.idx;
            assert(d.sent_level == 10);
            fdr.beat = 0;
            for (long long tp = 5120; tp <= 5400; tp += 20) drive(&d, &asub, fdr, tp);
            assert(d.idx == p0 && d.sent_level < 6);     /* brightness pulses down smoothly between beats */
            fdr.beat = 1; drive(&d, &asub, fdr, 5500);   /* beat 2 steps Full Wheel colour and pulses back to 10! */
            assert(d.idx != p0 && d.sent_level == 10);
            C.flash_on_beat = -1;
        }
        {   /* Night Drive: EV regen/cruise/power drives colour (#69 -> #67 -> #22) while music beats pulse brightness */
            snprintf(C.mode, sizeof C.mode, "nightdrive");
            snprintf(C.nd_source, sizeof C.nd_source, "power");
            C.power_low = -45; C.power_high = 140;
            C.power_color_low = 69; C.power_color_zero = 67; C.power_color_high = 22;
            C.pulse_decay = 45;
            memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 67;
            pthread_mutex_lock(&en_mu); en.kw = -45.0; en.kw_t = now_ms(); en.kw_ok = 1; pthread_mutex_unlock(&en_mu);
            Frame fn = {0}; fn.db = -16; fn.beat = 1; fn.onset_ratio = 3.0;
            drive(&d, &asub, fn, 3000);
            assert(d.idx == 69 && d.want_r == 382 && d.sent_level == 10);   /* Regen + Beat -> Emerald Green at max! */
            pthread_mutex_lock(&en_mu); en.kw = 0.0; en.kw_t = now_ms(); pthread_mutex_unlock(&en_mu);
            fn.beat = 0; fn.onset_ratio = 0;
            drive(&d, &asub, fn, 3300);
            assert(d.idx == 67 && d.want_r == 280);                         /* Cruise -> Ocean Blue! */
            pthread_mutex_lock(&en_mu); en.kw = 140.0; en.kw_t = now_ms(); pthread_mutex_unlock(&en_mu);
            drive(&d, &asub, fn, 3500);
            assert(d.idx == 22 && d.want_r == 255);                         /* Full Power -> Vivid Red! */
            pthread_mutex_lock(&en_mu); en.kw_ok = 0; pthread_mutex_unlock(&en_mu);
        }
        set_palette("kia");
        snprintf(C.mode, sizeof C.mode, "spectrum");

        /* 8. USB tuning clip capture (WAV + CSV + session.txt) & analyze_file() WAV round-trip */
        usb_roots[0] = usb; usb_any_folder = 1;
        tune.in_pcm = 1;
        snprintf(dpath, sizeof dpath, "%s/diag/request", lamp_dir);
        write_file(dpath, "capture 1\n");
        assert(diag_poll() == 0);   /* starts capture in-place without interrupting PCM loop */
        assert(tune.active == 1);
        char saved_dir[340]; snprintf(saved_dir, sizeof saved_dir, "%s", tune.dir);
        ana_init(&a); memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
        sample_idx = 0;
        int ncap_blks = 0;
        while (tune.active && ncap_blks < 200) {
            long long t = ncap_blks * 1000LL * PERIOD / rate;
            for (int i = 0; i < PERIOD; i++, sample_idx++) {
                double ts = sample_idx / (double)rate, ph = fmod(ts, 0.5);
                short s16 = (short)lround((0.1 * sin(2 * M_PI * 440.0 * ts) + 0.5 * sin(2 * M_PI * 70.0 * ph) * exp(-ph * 20.0)) * 18000.0);
                pcm_buf[i * CH] = pcm_buf[i * CH + 1] = s16;
            }
            tune_write_pcm(pcm_buf, PERIOD);
            Frame fc = ana_block(&a, pcm_buf, PERIOD, t);
            drive(&d, &a, fc, t);
            tune_write_frame(t, &a, &fc, &d);
            ncap_blks++;
        }
        assert(tune.active == 0 && ncap_blks == 94);   /* 94 * 512 = 48,128 frames (>= 1.0 s at 48 kHz) */
        tune.in_pcm = 0;
        char wpath[400], cpath[400], spath2[400], ts_path[400];
        snprintf(wpath, sizeof wpath, "%s/capture.wav", saved_dir);
        snprintf(cpath, sizeof cpath, "%s/frames.csv", saved_dir);
        snprintf(spath2, sizeof spath2, "%s/session.txt", saved_dir);
        snprintf(ts_path, sizeof ts_path, "%s/tune_status", rep);
        struct stat wst, cst, sst, tst;
        assert(!stat(wpath, &wst) && wst.st_size == 44 + 94 * PERIOD * CH * (long)sizeof(short));
        assert(!stat(cpath, &cst) && cst.st_size > 1000);
        assert(!stat(spath2, &sst) && sst.st_size > 200);
        assert(!stat(ts_path, &tst) && tst.st_size > 10);
        assert(analyze_file(wpath) == 0);              /* --analyze parses the captured RIFF/WAVE file directly */
    }

    char rm[120]; snprintf(rm, sizeof rm, "rm -rf %s", lamp_dir); assert(!system(rm));
    remove(path);

    puts("lamp tests passed");
    return 0;
}
