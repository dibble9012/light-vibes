/*
 * lightvibesd — light-vibes
 * Makes the Kia EV6 (dAudio Gen5W) ambient light react to the music that is playing.
 *
 * Audio source (no microphone, nothing inside CarPlay touched):
 *   CarPlay media plays on ALSA card0 device 2 (I2S_3) at 48 kHz.  The I2S controller's
 *   "Device2 Tx to Rx Internal Loopback Mode" mixer switch copies what is sent to the
 *   speakers back into that link's capture side (pcmC0D2c).  We switch it on, read
 *   pcmC0D2c, and switch it off again when we stop.  Siri/phone use pcmC0D0c, so they
 *   are unaffected.  We only hold pcmC0D2c while the playback side runs at 48 kHz
 *   (music); Siri/calls switch it to 24/16 kHz, and we let go immediately.
 *
 * Light control (confirmed on-car):
 *   com.hkmc.micom.system  (com.hkmc.micom.ISystemService)
 *     76 requestSMLStatus()                    -> logcat "SYS_SML_STATUS_M_RESP: rH rL gH gL bH bL bright dim drivemode"
 *     77 setSMLConfiguration(mode, data)        mode 5 = brightness (raw = UI level + 1)
 *                                               mode 9 = drive-mode-linked colour (1 = off, 2 = on)
 *     78 setSMLRGB(byte[2] r, byte[2] g, byte[2] b)  16-bit big-endian per channel
 *   Custom colours only apply while drive-mode-linked colour is off, so we switch it off
 *   while the music drives the light and put the user's setting back afterwards.
 *
 * Analysis: 2048-point FFT (Hann) every 1024 samples (21.3 ms, 23.4 Hz per bin).
 *   - beat: energy in [beat_low_hz, beat_high_hz] vs its running average
 *   - spectrum: user-defined bands (range, wheel colour, boost); loudest weighted band wins
 *   - brightness: overall loudness vs its running average
 * Settings are one-value files in the state dir and are re-read every 2 s (no restart).
 *
 * usage:  lightvibesd <state-dir>              run the service
 *         lightvibesd --status [state-dir]     show state
 *         lightvibesd --stop-all [state-dir]   stop every running copy, loopback off
 *         lightvibesd --off [state-dir]        loopback off
 *         lightvibesd --testcall R G B [level] send one colour (and brightness)
 *         lightvibesd --analyze <file.raw> [state-dir]  dry run over raw S16LE stereo 48 kHz
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <tinyalsa/asoundlib.h>

#define VERSION "0.5.0"
#define MAX_RATE 48000
static int rate = MAX_RATE;   /* the capture side only runs at the rate playback is using */
#define SOURCE_START_MS 2000  /* Android-mixer audio must last this long: it also carries short system sounds */
#define CH 2
#define PERIOD 1024                 /* 21.3 ms per analysis step */
#define NFFT 2048
#define BIN_HZ ((double)rate / NFFT)
#define BLOCK_MS (1000.0 * PERIOD / rate)
#define MAXBANDS 12

extern char **environ;

/* ---------- factory 64-colour custom wheels (Kia EV6 & Hyundai Ioniq 5 VehicleSettings) ---------- */
static const unsigned char KIA_R[64] = {16,16,16,33,47,64,82,100,120,136,155,169,183,193,204,213,221,228,235,239,246,250,254,255,255,255,255,255,255,255,255,255,255,255,254,255,246,233,220,202,166,168,149,132,117,104,93,81,71,62,53,44,37,30,25,20,16,16,16,16,16,16,16,16};
static const unsigned char KIA_G[64] = {67,46,32,28,28,28,28,28,28,29,33,35,35,33,31,27,23,19,14,10,6,4,1,50,68,80,105,120,143,166,195,205,218,232,243,255,255,255,255,252,226,243,235,229,223,221,222,225,231,238,245,250,250,250,250,250,244,230,210,187,163,138,111,89};
static const unsigned char KIA_B[64] = {244,242,239,239,239,239,239,239,239,239,239,239,234,224,210,192,174,153,153,116,96,81,69,29,35,39,48,53,62,70,88,118,158,200,230,255,246,221,188,147,91,98,98,98,98,98,105,118,133,152,169,189,207,225,237,249,252,252,252,252,252,252,252,248};

static const unsigned char HYUNDAI_R[64] = {176,195,205,232,255,255,255,255,255,255,255,255,254,255,254,254,255,255,255,255,255,255,255,254,255,255,255,255,255,255,255,255,255,254,255,254,247,230,210,175,119,58,10,0,0,0,0,0,0,0,0,0,25,55,81,100,109,122,125,128,130,143,156,167};
static const unsigned char HYUNDAI_G[64] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,107,73,88,113,125,3,34,48,60,65,137,148,160,172,177,191,207,216,237,255,254,255,254,254,255,255,217,180,161,140,125,115,99,85,62,0,0,0,0,42,52,25,0,0,0,0,0};
static const unsigned char HYUNDAI_B[64] = {255,254,254,254,241,195,158,137,117,111,106,101,95,72,58,30,106,116,73,88,113,115,10,0,23,42,49,107,119,111,113,108,123,138,147,165,182,186,188,191,196,198,235,245,255,255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};

static const unsigned char *WHEEL_R = KIA_R, *WHEEL_G = KIA_G, *WHEEL_B = KIA_B;
static const char *active_palette = "kia";
static const char *build_prop_path = "/system/build.prop";
static const char *hmgapi_json_path = "/data/hmgapi/settings/hmgapi.json";

/* ---------- config ---------- */
static const char *log_path = "/data/hmgapi/logs/light-vibes/lightvibesd.log";

typedef struct { int lo, hi, color; double gain; int r, g, b; } Band;   /* color = wheel index, or -1 for #RRGGBB */

static struct {
    char dir[256];
    int enabled, debug, dry;
    char palette[16];           /* auto | kia | hyundai */
    char mode[16];              /* beat | spectrum | pulse | speed */
    int speed_low, speed_high, speed_color_low, speed_color_high;   /* speed mode: in speed_unit, wheel indexes */
    char speed_unit[8];         /* mph | kmh */
    char preset[16];            /* standard | repo */
    char bright_curve[16];      /* range | repo */
    int min_interval_ms;        /* per command type */
    int bright_min, bright_max; /* UI levels 0..10 */
    int loudness_range_db;      /* dB above/below average that spans the brightness range */
    int sensitivity;            /* beat: percent above the running beat-band average */
    int beat_low_hz, beat_high_hz;
    int beat_avg_ms;            /* running-average window for the beat band */
    int min_beat_ms;
    int flash_on_beat;          /* full brightness for flash_ms after each beat */
    int flash_ms;
    int beat_step;              /* wheel steps per beat (beat mode) */
    int skip_white;
    int pulse_decay;            /* pulse: percent of brightness kept per 100 ms */
    int switch_margin;          /* spectrum: percent a new band must beat the current one by */
    int band_smoothing_ms;
    Band bands[MAXBANDS]; int nbands;
    char bands_text[256];
    int silence_db, idle_seconds;
    int override_drive_mode;
    int card, device;
    char loop_ctl[96];
    char service_bin[96], logcat_bin[96], svc_name[64];
} C;

static int cfg_int(const char *k, int def) {
    char p[400], b[64]; snprintf(p, sizeof p, "%s/%s", C.dir, k);
    FILE *f = fopen(p, "r"); if (!f) return def;
    int ok = fgets(b, sizeof b, f) != NULL; fclose(f);
    if (!ok) return def;
    char *e; long v = strtol(b, &e, 10); return e == b ? def : (int)v;
}
static void cfg_str(const char *k, char *out, size_t n, const char *def) {
    char p[400]; snprintf(p, sizeof p, "%s/%s", C.dir, k);
    snprintf(out, n, "%s", def);
    FILE *f = fopen(p, "r"); if (!f) return;
    char b[256];
    if (fgets(b, sizeof b, f)) { b[strcspn(b, "\r\n")] = 0; if (b[0]) snprintf(out, n, "%.*s", (int)n - 1, b); }
    fclose(f);
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void str_lower(char *s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 'a' - 'A';
}
static const char *match_make(const char *s) {
    if (strstr(s, "neev") || strstr(s, "ceev") || strstr(s, "ioniq") || strstr(s, "ecohmc") || strstr(s, "hyundai")) return "hyundai";
    if (strstr(s, "cvev") || strstr(s, "dl3") || strstr(s, "ev6") || strstr(s, "ecokia") || strstr(s, "kia")) return "kia";
    return NULL;
}
static const char *detect_palette(void) {
    if (!strcmp(C.palette, "hyundai") || !strcmp(C.palette, "kia")) return C.palette;
    static const char *const keys[] = {
        "ro.build.product=", "ro.product.device=", "ro.product.name=",
        "ro.product.model=", "ro.build.fingerprint=", "ro.mobis.onebindir=", NULL
    };
    FILE *f = fopen(build_prop_path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            str_lower(line);
            for (int i = 0; keys[i]; i++) {
                size_t klen = strlen(keys[i]);
                if (!strncmp(line, keys[i], klen)) {
                    const char *m = match_make(line + klen);
                    if (m) { fclose(f); return m; }
                }
            }
        }
        fclose(f);
    }
    f = fopen(hmgapi_json_path, "r");
    if (f) {
        char buf[2048];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0; str_lower(buf);
        const char *m = match_make(buf);
        if (m) return m;
    }
    return "kia";
}
static void set_palette(const char *name) {
    if (!strcmp(name, "hyundai")) {
        WHEEL_R = HYUNDAI_R; WHEEL_G = HYUNDAI_G; WHEEL_B = HYUNDAI_B;
        active_palette = "hyundai";
    } else {
        WHEEL_R = KIA_R; WHEEL_G = KIA_G; WHEEL_B = KIA_B;
        active_palette = "kia";
    }
    for (int i = 0; i < C.nbands; i++) {
        int col = C.bands[i].color;
        if (col >= 0 && col < 64) {
            C.bands[i].r = WHEEL_R[col];
            C.bands[i].g = WHEEL_G[col];
            C.bands[i].b = WHEEL_B[col];
        }
    }
}
static int palette_has_rgb(const unsigned char *r_tab, const unsigned char *g_tab, const unsigned char *b_tab, int r, int g, int b) {
    for (int i = 0; i < 64; i++)
        if (r_tab[i] == r && g_tab[i] == g && b_tab[i] == b) return 1;
    return 0;
}

/* bands: "lo-hi:colour:boost ..." e.g. "40-150:22:1 150-500:28:1.5" (colour = wheel index 0-63) */
static const char *DEFAULT_BANDS = "30-150:22:1 150-500:28:2 500-2000:44:4 2000-6000:56:10 6000-16000:8:30";
static int parse_bands(const char *txt, Band *out) {
    int n = 0; const char *p = txt;
    while (*p && n < MAXBANDS) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        int lo, hi, col = 0; unsigned hex = 0; double g = 1; int used = 0, k, is_hex = 0;
        if (sscanf(p, "%d-%d:#%6x%n", &lo, &hi, &hex, &used) == 3) {           /* lo-hi:#RRGGBB[:boost] */
            is_hex = 1; k = 3;
            const char *q = p + used; if (*q == ':') { int u2 = 0; if (sscanf(q, ":%lf%n", &g, &u2) == 1) used += u2; }
        } else {
            k = sscanf(p, "%d-%d:%d:%lf%n", &lo, &hi, &col, &g, &used);
            if (k < 3) { k = sscanf(p, "%d-%d:%d%n", &lo, &hi, &col, &used); g = 1; }
        }
        if (k < 3 || used == 0 || lo < 0 || hi <= lo || hi > MAX_RATE / 2 || g < 0 || (!is_hex && (col < 0 || col > 63))) return -1;
        out[n].lo = lo; out[n].hi = hi; out[n].gain = g;
        if (is_hex) { out[n].color = -1; out[n].r = (hex >> 16) & 255; out[n].g = (hex >> 8) & 255; out[n].b = hex & 255; }
        else { out[n].color = col; out[n].r = WHEEL_R[col]; out[n].g = WHEEL_G[col]; out[n].b = WHEEL_B[col]; }
        n++;
        p += used;
        while (*p && *p != ' ' && *p != ',' && *p != '\t') p++;
    }
    return n;
}

/* preset "repo" = defaults of github.com/nmz787/gen5w-ambient-sound (colours mapped to the nearest factory-wheel entry) */
static const char *REPO_BANDS = "60-700:23:1.5 700-1000:28:0.2 1000-2000:38:0.9 2000-3000:51:4.5 3000-4000:61:1.7 4000-8000:8:20";
static void load_config(const char *dir) {
    char d[sizeof C.dir]; snprintf(d, sizeof d, "%s", dir);   /* dir may be C.dir itself */
    snprintf(C.dir, sizeof C.dir, "%s", d);
    cfg_str("palette", C.palette, sizeof C.palette, "auto");
    set_palette(detect_palette());
    cfg_str("preset", C.preset, sizeof C.preset, "standard");
    int repo = !strcmp(C.preset, "repo");
    C.enabled = cfg_int("enabled", 1);
    C.debug = cfg_int("debug", 0);
    cfg_str("mode", C.mode, sizeof C.mode, repo ? "spectrum" : "beat");
    C.min_interval_ms = clampi(cfg_int("min_interval_ms", 250), 150, 5000);
    C.speed_low = clampi(cfg_int("speed_low", 0), 0, 300);
    C.speed_high = clampi(cfg_int("speed_high", 80), 0, 300);
    C.speed_color_low = clampi(cfg_int("speed_color_low", 44), 0, 63);
    C.speed_color_high = clampi(cfg_int("speed_color_high", 22), 0, 63);
    cfg_str("speed_unit", C.speed_unit, sizeof C.speed_unit, "mph");
    C.bright_min = clampi(cfg_int("brightness_min", repo ? 1 : 3), 0, 10);
    C.bright_max = clampi(cfg_int("brightness_max", 10), 0, 10);
    if (C.bright_max < C.bright_min) C.bright_max = C.bright_min;
    C.loudness_range_db = clampi(cfg_int("loudness_range_db", 12), 3, 40);
    C.sensitivity = clampi(cfg_int("sensitivity", 150), 105, 500);
    C.beat_low_hz = clampi(cfg_int("beat_low_hz", 40), 20, 20000);
    C.beat_high_hz = clampi(cfg_int("beat_high_hz", 150), C.beat_low_hz + 20, 22000);
    C.beat_avg_ms = clampi(cfg_int("beat_avg_ms", 1500), 200, 10000);
    C.min_beat_ms = clampi(cfg_int("min_beat_ms", 300), 100, 3000);
    C.flash_on_beat = cfg_int("flash_on_beat", repo ? 0 : -1);   /* -1 = mode default */
    C.flash_ms = clampi(cfg_int("flash_ms", 350), 0, 2000);
    C.beat_step = clampi(cfg_int("beat_step", 5), 1, 32);
    C.skip_white = cfg_int("skip_white", 1);
    C.pulse_decay = clampi(cfg_int("pulse_decay", 60), 0, 99);
    C.switch_margin = clampi(cfg_int("switch_margin", 30), 0, 500);
    C.band_smoothing_ms = clampi(cfg_int("band_smoothing_ms", repo ? 128 : 120), 0, 3000);
    cfg_str("brightness_curve", C.bright_curve, sizeof C.bright_curve, repo ? "repo" : "range");
    cfg_str("bands", C.bands_text, sizeof C.bands_text, repo ? REPO_BANDS : DEFAULT_BANDS);
    C.nbands = parse_bands(C.bands_text, C.bands);
    if (C.nbands <= 0) { snprintf(C.bands_text, sizeof C.bands_text, "%s", DEFAULT_BANDS); C.nbands = parse_bands(DEFAULT_BANDS, C.bands); }
    C.silence_db = clampi(cfg_int("silence_db", repo ? -70 : -55), -90, -10);
    C.idle_seconds = clampi(cfg_int("idle_seconds", 3), 1, 60);
    C.override_drive_mode = cfg_int("override_drive_mode", 1);
    C.card = cfg_int("card", 0);
    C.device = cfg_int("device", 2);
    cfg_str("loopback_control", C.loop_ctl, sizeof C.loop_ctl, "Device2 Tx to Rx Internal Loopback Mode");
    snprintf(C.service_bin, sizeof C.service_bin, "/system/bin/service");
    snprintf(C.logcat_bin, sizeof C.logcat_bin, "/system/bin/logcat");
    cfg_str("light_service", C.svc_name, sizeof C.svc_name, "com.hkmc.micom.system");
}
static int flash_enabled(void) {
    if (C.flash_on_beat >= 0) return C.flash_on_beat;
    return !strcmp(C.mode, "beat");
}

/* the app's settings folder, or the shipped defaults until the app has made it */
static const char *settings_dir = "", *fallback_dir = "";
static const char *active_dir(void) {
    struct stat st;
    return (!fallback_dir[0] || !stat(settings_dir, &st)) ? settings_dir : fallback_dir;
}

/* re-read settings when any file in the state dir changes (nanoseconds: the app can change two things within a second) */
#ifdef __APPLE__
#define MTIME_NS(st) ((st).st_mtimespec.tv_sec * 1000000000LL + (st).st_mtimespec.tv_nsec)
#else
#define MTIME_NS(st) ((st).st_mtim.tv_sec * 1000000000LL + (st).st_mtim.tv_nsec)
#endif
static long long config_stamp(void) {
    const char *dir = active_dir();
    long long newest = 0; DIR *d = opendir(dir); struct dirent *e; struct stat st; char p[512];
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!stat(p, &st) && MTIME_NS(st) > newest) newest = MTIME_NS(st);
    }
    if (d) closedir(d);
    if (!stat(dir, &st) && MTIME_NS(st) > newest) newest = MTIME_NS(st);
    return newest;
}

/* ---------- logging: own capped log file + non-blocking stdout ---------- */
#define LOG_MAX (512 * 1024)
static FILE *log_fp = NULL;
static int log_to_file = 0;
static void log_open(void) {
    char dir[300]; snprintf(dir, sizeof dir, "%s", log_path);
    char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdir("/data/hmgapi/logs", 0755); mkdir(dir, 0755); }
    log_fp = fopen(log_path, "a");
    if (log_fp) setvbuf(log_fp, NULL, _IOLBF, 0);
    int fl = fcntl(1, F_GETFL); if (fl >= 0) fcntl(1, F_SETFL, fl | O_NONBLOCK);   /* never block on stdout */
    log_to_file = 1;
}
static void say(const char *fmt, ...) {
    char ts[32], msg[1024]; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%m-%d %H:%M:%S", &tm);
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (!log_to_file) { printf("%s %s\n", ts, msg); fflush(stdout); return; }
    char line[1100]; int n = snprintf(line, sizeof line, "%s %s\n", ts, msg);
    if (n > (int)sizeof line - 1) n = sizeof line - 1;
    ssize_t w = write(1, line, n); (void)w;
    if (log_fp) {
        fputs(line, log_fp);
        if (ftell(log_fp) > LOG_MAX) {
            fclose(log_fp);
            char old[320]; snprintf(old, sizeof old, "%s.1", log_path);
            rename(log_path, old);
            log_fp = fopen(log_path, "a"); if (log_fp) setvbuf(log_fp, NULL, _IOLBF, 0);
        }
    }
}
/* print the last n lines of the log (optionally only lines containing filt) — the head unit has no tail */
static int show_log(int n, const char *filt) {
    FILE *f = fopen(log_path, "r");
    if (!f) { printf("(no log yet: %s)\n", log_path); return 1; }
    static char ring[400][600]; int cnt = 0;
    if (n > 400) n = 400;
    if (n < 1) n = 40;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        if (filt && !strstr(line, filt)) continue;
        snprintf(ring[cnt % n], sizeof ring[0], "%s", line); cnt++;
    }
    fclose(f);
    int start = cnt > n ? cnt - n : 0;
    for (int i = start; i < cnt; i++) fputs(ring[i % n], stdout);
    if (!cnt) printf("(nothing%s%s in %s yet)\n", filt ? " matching " : "", filt ? filt : "", log_path);
    return 0;
}
static long long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void show_config(void) {
    say("settings: palette=%s (%s) preset=%s brightness_curve=%s", active_palette, C.palette, C.preset, C.bright_curve);
    say("settings: mode=%s brightness %d..%d loudness_range=%ddB | beat %d-%dHz sensitivity=%d%% avg=%dms min=%dms flash=%d/%dms step=%d | pulse_decay=%d | spectrum margin=%d%% smoothing=%dms bands=\"%s\" | interval=%dms idle=%ds silence=%ddB drive_mode_override=%d",
        C.mode, C.bright_min, C.bright_max, C.loudness_range_db, C.beat_low_hz, C.beat_high_hz, C.sensitivity, C.beat_avg_ms,
        C.min_beat_ms, flash_enabled(), C.flash_ms, C.beat_step, C.pulse_decay, C.switch_margin, C.band_smoothing_ms, C.bands_text,
        C.min_interval_ms, C.idle_seconds, C.silence_db, C.override_drive_mode);
    if (!strcmp(C.mode, "speed")) say("settings: speed %d %s = colour #%d, %d %s = colour #%d", C.speed_low, C.speed_unit, C.speed_color_low, C.speed_high, C.speed_unit, C.speed_color_high);
}

/* ---------- child processes (service call / logcat) ---------- */
static pid_t pending_pid = 0;
static void reap(void) {
    int st; pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) if (p == pending_pid) pending_pid = 0;
}
static void wait_pending(void) { reap(); if (pending_pid) { waitpid(pending_pid, NULL, 0); pending_pid = 0; } }
static pid_t spawn(char *const argv[], int out_fd, int wait_for_it) {
    pid_t p = fork();
    if (p == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, 0);
        dup2(out_fd >= 0 ? out_fd : nul, 1);
        dup2(nul, 2);
        execve(argv[0], argv, environ);
        _exit(127);
    }
    if (p > 0 && wait_for_it) { int st; waitpid(p, &st, 0); }
    return p;
}
/* encode a 16-bit channel value as service-call's packed byte[2]: [hi, lo] */
static void enc(char *buf, size_t n, int v) { snprintf(buf, n, "%d", ((v >> 8) & 0xff) | ((v & 0xff) << 8)); }

static void light_rgb(int r, int g, int b, int wait_for_it) {
    if (C.dry) { say("DRY rgb %d %d %d", r, g, b); return; }
    char er[16], eg[16], eb[16];
    enc(er, sizeof er, r); enc(eg, sizeof eg, g); enc(eb, sizeof eb, b);
    /* service call <svc> 78  i32 2 i32 <r>  i32 2 i32 <g>  i32 2 i32 <b>   (byte[2] = length, packed bytes) */
    char *a[] = {C.service_bin, "call", C.svc_name, "78",
                 "i32", "2", "i32", er, "i32", "2", "i32", eg, "i32", "2", "i32", eb, NULL};
    pid_t p = spawn(a, -1, wait_for_it);
    if (!wait_for_it) pending_pid = p;
}
static void light_config(int mode, int data, int wait_for_it) {
    if (C.dry) { say("DRY config mode %d data %d", mode, data); return; }
    char m[16], v[16]; snprintf(m, sizeof m, "%d", mode); snprintf(v, sizeof v, "%d", data);
    char *a[] = {C.service_bin, "call", C.svc_name, "77", "i32", m, "i32", v, NULL};
    pid_t p = spawn(a, -1, wait_for_it);
    if (!wait_for_it) pending_pid = p;
}
static void light_bright_raw(int raw, int wait_for_it) { light_config(5, raw, wait_for_it); }

/* ---------- read the user's current light settings back from the lamp ---------- */
static struct { int valid, r, g, b, bright_raw, drive_mode; } base;

static int parse_status_line(const char *s, int *v, int n) {
    const char *p = strstr(s, "SYS_SML_STATUS_M_RESP: 0x");
    if (!p) return 0;
    p += strlen("SYS_SML_STATUS_M_RESP: ");
    for (int i = 0; i < n; i++) {
        while (*p == ' ') p++;
        if (p[0] != '0' || p[1] != 'x') return i;
        v[i] = (int)strtol(p, (char **)&p, 16);
    }
    return n;
}

/* The user's own lamp setting, also on disk: "r g b brightness drive_mode dirty". dirty = we were driving
   the lamp when it was written. If that's still set at the next start, the car was switched off or we
   crashed mid-song, and the lamp is showing our colour, not theirs: the file is the truth, not the lamp. */
static const char *light_state_path = "/data/hmgapi/logs/light-vibes/user_light";
static int saved_load(int *s) {
    FILE *f = fopen(light_state_path, "r"); if (!f) return 0;
    int k = fscanf(f, "%d %d %d %d %d %d", &s[0], &s[1], &s[2], &s[3], &s[4], &s[5]);
    fclose(f);
    if (k != 6) return 0;
    for (int i = 0; i < 3; i++) if (s[i] < 0 || s[i] > 65535) return 0;
    return s[3] >= 0 && s[3] <= 255 && s[4] >= 0 && s[4] <= 255;
}
/* flushed to the disk before we touch the lamp: the car can lose power at any moment */
static void saved_write(int dirty) {
    int s[6];
    if (saved_load(s) && s[0] == base.r && s[1] == base.g && s[2] == base.b && s[3] == base.bright_raw && s[4] == base.drive_mode && s[5] == dirty) return;
    char tmp[320], b[96]; snprintf(tmp, sizeof tmp, "%s.tmp", light_state_path);
    int n = snprintf(b, sizeof b, "%d %d %d %d %d %d\n", base.r, base.g, base.b, base.bright_raw, base.drive_mode, dirty);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { say("can't save the user's colour: %s", strerror(errno)); return; }
    int ok = write(fd, b, n) == n && !fsync(fd);
    close(fd);
    if (!ok || rename(tmp, light_state_path)) { say("can't save the user's colour: %s", strerror(errno)); return; }
    char dir[320]; snprintf(dir, sizeof dir, "%s", light_state_path);
    char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
    int d = open(dir, O_RDONLY); if (d >= 0) { fsync(d); close(d); }
}
static void base_from(const int *s) {
    base.valid = 1; base.r = s[0]; base.g = s[1]; base.b = s[2]; base.bright_raw = s[3]; base.drive_mode = s[4];
}
/* r g b brightness drive_mode, as the lamp reports them */
static int read_lamp(int *out) {
    char *req[] = {C.service_bin, "call", C.svc_name, "76", NULL};
    spawn(req, -1, 1);
    usleep(400 * 1000);
    int fd[2]; if (pipe(fd)) return 0;
    char *lc[] = {C.logcat_bin, "-d", "-v", "brief", "-s", "SystemService:D", NULL};
    pid_t p = spawn(lc, fd[1], 0);
    close(fd[1]);
    FILE *f = fdopen(fd[0], "r");
    char line[512]; int v[9], got = 0, last[9];
    while (f && fgets(line, sizeof line, f)) {
        int k = parse_status_line(line, v, 9);
        if (k >= 7) { memcpy(last, v, sizeof v); if (k < 9) last[8] = 0; got = 1; }
    }
    if (f) fclose(f);
    if (p > 0) waitpid(p, NULL, 0);
    if (!got) return 0;
    out[0] = (last[0] << 8) | last[1]; out[1] = (last[2] << 8) | last[3]; out[2] = (last[4] << 8) | last[5];
    out[3] = last[6]; out[4] = last[8];
    return 1;
}
static void snapshot_base(void) {
    if (C.dry) { base.valid = 0; return; }
    int s[6], lamp[6], have = saved_load(s);
    if (have && s[5]) {
        base_from(s);
        say("using the saved colour rgb %d %d %d, brightness raw %d: the lamp may still show one of ours", base.r, base.g, base.b, base.bright_raw);
    } else if (read_lamp(lamp)) {
        base_from(lamp);
        say("user settings read back: rgb %d %d %d, brightness raw %d, drive-mode colour %d", base.r, base.g, base.b, base.bright_raw, base.drive_mode);
        saved_write(0);
    } else if (have) {
        base_from(s);
        say("no status reply in logcat; using the saved colour rgb %d %d %d, brightness raw %d", base.r, base.g, base.b, base.bright_raw);
    } else {
        base.valid = 0;
        say("could not read the user's colour back (no status reply in logcat); will not restore");
    }
    if (base.valid && !strcmp(C.palette, "auto")) {
        if (palette_has_rgb(HYUNDAI_R, HYUNDAI_G, HYUNDAI_B, base.r, base.g, base.b) && strcmp(active_palette, "hyundai")) {
            set_palette("hyundai");
            say("lamp colour (%d,%d,%d) matched hyundai palette", base.r, base.g, base.b);
        } else if (palette_has_rgb(KIA_R, KIA_G, KIA_B, base.r, base.g, base.b) && strcmp(active_palette, "kia")) {
            set_palette("kia");
            say("lamp colour (%d,%d,%d) matched kia palette", base.r, base.g, base.b);
        }
    }
}
static void take_over_lamp(void) {
    if (base.valid && !C.dry) saved_write(1);
    if (C.override_drive_mode && (!base.valid || base.drive_mode != 1)) {
        light_config(9, 1, 1);
        say("drive-mode-linked colour switched off while we drive the lamp");
    }
}
static void apply_base(void) {
    light_rgb(base.r, base.g, base.b, 1);
    light_bright_raw(base.bright_raw, 1);
    if (C.override_drive_mode && base.drive_mode >= 1 && base.drive_mode != 1) light_config(9, base.drive_mode, 1);
}
static void restore_base(void) {
    wait_pending();
    if (!base.valid) return;
    apply_base();
    if (!C.dry) saved_write(0);
    say("restored user colour %d %d %d, brightness raw %d, drive-mode colour %d", base.r, base.g, base.b, base.bright_raw, base.drive_mode);
}

/* ---------- the loopback mixer switch ---------- */
static int set_loopback(int on) {
    struct mixer *m = mixer_open(C.card);
    if (!m) { say("mixer card%d open failed", C.card); return -1; }
    struct mixer_ctl *c = mixer_get_ctl_by_name(m, C.loop_ctl);
    int r = -1;
    if (!c) say("mixer control \"%s\" not found — refusing to touch anything", C.loop_ctl);
    else if (mixer_ctl_get_type(c) != MIXER_CTL_TYPE_BOOL) say("mixer control \"%s\" is not a switch — refusing", C.loop_ctl);
    else if (strstr(C.loop_ctl, "Tx to Rx") == NULL) say("loopback_control must be a \"Tx to Rx\" switch — refusing");
    else {
        r = mixer_ctl_set_value(c, 0, on ? 1 : 0);
        int back = mixer_ctl_get_value(c, 0);
        say("loopback \"%s\" -> %d (%s, reads back %d)", C.loop_ctl, on, r ? "FAILED" : "ok", back);
    }
    mixer_close(m);
    return r;
}

/* ---------- is music playing on device 2? (state RUNNING and 48 kHz) ---------- */
static int file_has(const char *path, const char *needle) {
    char b[1024]; int fd = open(path, O_RDONLY); if (fd < 0) return 0;
    int n = read(fd, b, sizeof b - 1); close(fd);
    if (n <= 0) return 0;
    b[n] = 0; return strstr(b, needle) != NULL;
}
/* the rate if music can be playing, else 0: 48 kHz is CarPlay, 44.1 kHz Android's mixer (Sounds of Nature...).
   Siri and calls use lower rates, and we let go for those. */
static int music_rate(void) {
    char st[96], hw[96];
    snprintf(st, sizeof st, "/proc/asound/card%d/pcm%dp/sub0/status", C.card, C.device);
    snprintf(hw, sizeof hw, "/proc/asound/card%d/pcm%dp/sub0/hw_params", C.card, C.device);
    if (!file_has(st, "RUNNING")) return 0;
    return file_has(hw, "rate: 48000") ? 48000 : file_has(hw, "rate: 44100") ? 44100 : 0;
}

/* ---------- FFT analysis ---------- */
typedef struct {
    float ring[NFFT]; int filled;
    float win[NFFT], re[NFFT], im[NFFT];
    double beat_fast, beat_slow;
    double band_e[MAXBANDS];
    double loud_ref_db;
    double rep_ref;             /* repo curve: running loudness reference (linear, 16-bit units) */
    long long last_beat;
    int sel_band;
} Ana;

typedef struct { double db; int beat; int band; double beat_ratio; double band_db[MAXBANDS]; } Frame;

static void ana_init(Ana *a) {
    memset(a, 0, sizeof *a);
    for (int i = 0; i < NFFT; i++) a->win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (NFFT - 1));
    a->sel_band = -1;
    a->last_beat = -100000;
    a->loud_ref_db = -30;
    a->rep_ref = 50;
}
static void fft(float *re, float *im) {
    for (int i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= NFFT; len <<= 1) {
        double ang = -2 * M_PI / len; float wr0 = (float)cos(ang), wi0 = (float)sin(ang);
        for (int i = 0; i < NFFT; i += len) {
            float wr = 1, wi = 0;
            for (int j = 0; j < len / 2; j++) {
                int a = i + j, b = a + len / 2;
                float br = re[b] * wr - im[b] * wi, bi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - br; im[b] = im[a] - bi; re[a] += br; im[a] += bi;
                float nw = wr * wr0 - wi * wi0; wi = wr * wi0 + wi * wr0; wr = nw;
            }
        }
    }
}
static double band_energy(const float *re, const float *im, int lo_hz, int hi_hz) {
    int lo = (int)ceil(lo_hz / BIN_HZ), hi = (int)floor(hi_hz / BIN_HZ);
    if (lo < 1) lo = 1;
    if (hi > NFFT / 2 - 1) hi = NFFT / 2 - 1;
    double e = 0; for (int k = lo; k <= hi; k++) e += (double)re[k] * re[k] + (double)im[k] * im[k];
    return e / ((double)NFFT * NFFT);
}
static double db10(double e) { return e > 1e-14 ? 10 * log10(e) : -140; }

static Frame ana_block(Ana *a, const short *buf, int frames, long long t) {
    Frame f; memset(&f, 0, sizeof f); f.band = a->sel_band;
    /* slide the mono ring buffer */
    memmove(a->ring, a->ring + frames, sizeof(float) * (NFFT - frames));
    double et = 0;
    for (int i = 0; i < frames; i++) {
        float x = (buf[i * CH] + buf[i * CH + 1]) / 65536.0f;
        a->ring[NFFT - frames + i] = x; et += (double)x * x;
    }
    if (a->filled < NFFT) a->filled += frames;
    double rms = sqrt(et / frames);
    f.db = rms > 1e-9 ? 20 * log10(rms) : -120;
    for (int i = 0; i < NFFT; i++) { a->re[i] = a->ring[i] * a->win[i]; a->im[i] = 0; }
    fft(a->re, a->im);

    /* beat: beat-band energy, short average vs long average */
    double eb = band_energy(a->re, a->im, C.beat_low_hz, C.beat_high_hz);
    a->beat_fast = 0.5 * a->beat_fast + 0.5 * eb;
    double alpha_slow = BLOCK_MS / (double)C.beat_avg_ms;
    if (t < 1500) alpha_slow = alpha_slow * 4 > 0.5 ? 0.5 : alpha_slow * 4;   /* settle fast at start */
    if (a->beat_slow <= 0) a->beat_slow = eb;
    f.beat_ratio = a->beat_slow > 1e-14 ? a->beat_fast / a->beat_slow : 0;
    if (f.db > C.silence_db && f.beat_ratio * 100 > C.sensitivity && t - a->last_beat >= C.min_beat_ms) { f.beat = 1; a->last_beat = t; }
    a->beat_slow += alpha_slow * (eb - a->beat_slow);

    /* spectrum bands (smoothed, boosted); loudest wins, with a switching margin */
    double alpha_b = C.band_smoothing_ms > 0 ? BLOCK_MS / (BLOCK_MS + C.band_smoothing_ms) : 1.0;
    int best = 0;
    for (int i = 0; i < C.nbands; i++) {
        double e = band_energy(a->re, a->im, C.bands[i].lo, C.bands[i].hi) * C.bands[i].gain;
        a->band_e[i] += alpha_b * (e - a->band_e[i]);
        f.band_db[i] = db10(a->band_e[i]);
        if (a->band_e[i] > a->band_e[best]) best = i;
    }
    if (a->sel_band < 0 || a->sel_band >= C.nbands) a->sel_band = best;
    else if (best != a->sel_band && a->band_e[best] > a->band_e[a->sel_band] * (1 + C.switch_margin / 100.0)) a->sel_band = best;
    f.band = a->sel_band;

    if (f.db > C.silence_db) a->loud_ref_db += (BLOCK_MS / 4000.0) * (f.db - a->loud_ref_db);
    {   /* repo: reference += 0.025 * (max(rms, 20) - reference) per 128 ms window */
        double r16 = rms * 32768.0, alpha = 0.025 * BLOCK_MS / 128.0;
        a->rep_ref += alpha * ((r16 > 20 ? r16 : 20) - a->rep_ref);
    }
    return f;
}

/* loudness -> UI brightness level (average loudness sits in the middle of the range) */
static int loud_level(Ana *a, double db) {
    if (db <= C.silence_db) return C.bright_min;
    if (!strcmp(C.bright_curve, "repo")) {     /* level = round(4 + 3*log2(rms / reference)) */
        double r16 = pow(10, db / 20) * 32768.0, ref = a->rep_ref > 20 ? a->rep_ref : 20;
        int lvl = (int)lround(4 + 3 * log2(r16 / ref));
        return clampi(lvl, C.bright_min, C.bright_max);
    }
    double x = 0.5 + (db - a->loud_ref_db) / (2.0 * C.loudness_range_db);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    return clampi((int)lround(C.bright_min + x * (C.bright_max - C.bright_min)), C.bright_min, C.bright_max);
}

/* ---------- light driver ---------- */
typedef struct {
    int idx;
    int sent_idx, sent_level;   /* sent_idx holds the packed RGB last sent */
    long long last_color, last_bright;
    double pulse;
    long long flash_until;
} Drv;

static int wheel_pale(int i) {   /* the pastel/white stretch of the wheel */
    int m = WHEEL_R[i] < WHEEL_G[i] ? WHEEL_R[i] : WHEEL_G[i];
    if (WHEEL_B[i] < m) m = WHEEL_B[i];
    return m > 150;
}
static int wheel_ok(int i) { return !C.skip_white || !wheel_pale(i); }
static int wheel_next(int i, int step) {
    for (int k = 0; k < 64; k++) { i = (i + step) % 64; if (wheel_ok(i)) return i; step = 1; }
    return i;
}

static void drive(Drv *d, Ana *a, Frame f, long long t) {
    int want_idx, want_level, wr = 0, wg = 0, wb = 0;
    if (f.beat && flash_enabled()) d->flash_until = t + C.flash_ms;
    int flashing = t < d->flash_until;

    if (!strcmp(C.mode, "spectrum")) {
        if (f.band >= 0) { wr = C.bands[f.band].r; wg = C.bands[f.band].g; wb = C.bands[f.band].b; want_idx = 1000 + f.band; }
        else want_idx = d->idx;
        want_level = flashing ? C.bright_max : loud_level(a, f.db);
    } else if (!strcmp(C.mode, "pulse")) {
        double keep = pow(C.pulse_decay / 100.0, BLOCK_MS / 100.0);
        double hit = f.beat_ratio > 1 ? (f.beat_ratio - 1) / (C.sensitivity / 100.0 - 1 + 1e-6) : 0;
        if (hit > 1) hit = 1;
        d->pulse = d->pulse * keep; if (hit > d->pulse) d->pulse = hit;
        if (f.beat) d->pulse = 1;
        want_idx = -1;   /* keep the user's colour */
        want_level = C.bright_min + (int)lround((C.bright_max - C.bright_min) * d->pulse);
    } else {   /* beat */
        if (f.beat) d->idx = wheel_next(d->idx, C.beat_step);
        want_idx = d->idx;
        want_level = flashing ? C.bright_max : loud_level(a, f.db);
    }

    reap();
    if (pending_pid) return;   /* one command in flight at a time */

    if (want_idx >= 0 && want_idx < 64) { wr = WHEEL_R[want_idx]; wg = WHEEL_G[want_idx]; wb = WHEEL_B[want_idx]; }
    int key = want_idx < 0 ? -1 : (wr << 16) | (wg << 8) | wb;
    if (key >= 0 && key != d->sent_idx && t - d->last_color >= C.min_interval_ms) {
        light_rgb(wr, wg, wb, 0);
        d->sent_idx = key; d->last_color = t;
        if (C.debug) {
            if (want_idx >= 1000) say("colour band %d-%dHz (%d,%d,%d)", C.bands[want_idx - 1000].lo, C.bands[want_idx - 1000].hi, wr, wg, wb);
            else say("colour #%d (%d,%d,%d)%s", want_idx, wr, wg, wb, f.beat ? " [beat]" : "");
        }
        return;
    }
    int diff = want_level - d->sent_level; if (diff < 0) diff = -diff;
    int edge = want_level == C.bright_max || want_level == C.bright_min;
    if (want_level != d->sent_level && (d->sent_level < 0 || diff >= 2 || edge) && t - d->last_bright >= C.min_interval_ms) {
        light_bright_raw(want_level + 1, 0);
        d->sent_level = want_level; d->last_bright = t;
        if (C.debug >= 3) say("brightness %d", want_level);
    }
}

/* once a second with debug=2: what the analysis hears */
typedef struct { long long last; int beats; double peak_ratio; } Meter;
static void meter(Meter *m, Ana *a, Frame f, long long t) {
    if (f.beat) m->beats++;
    if (f.beat_ratio > m->peak_ratio) m->peak_ratio = f.beat_ratio;
    if (C.debug < 2 || t - m->last < 1000) return;
    char b[512]; int n = snprintf(b, sizeof b, "meter: level %.0fdB (avg %.0f) | beat %d-%dHz peak %.0f%% of avg, %d beat(s) | bands:",
                                   f.db, a->loud_ref_db, C.beat_low_hz, C.beat_high_hz, m->peak_ratio * 100, m->beats);
    for (int i = 0; i < C.nbands && n < (int)sizeof b - 40; i++)
        n += snprintf(b + n, sizeof b - n, " %s%d-%d=%.0f", i == f.band ? "*" : "", C.bands[i].lo, C.bands[i].hi, f.band_db[i]);
    say("%s", b);
    m->last = t; m->beats = 0; m->peak_ratio = 0;
}

/* ---------- live meter for the app: lines on an abstract socket, ~10 a second (no disk writes) ---------- */
#define METER_SOCKET "light-vibes.meter"
#define METER_CLIENTS 4
static int meter_srv = -1, meter_cl[METER_CLIENTS] = {-1, -1, -1, -1}, meter_oldest;
static void meter_listen(void) {
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa); sa.sun_family = AF_UNIX;
    size_t len = strlen(METER_SOCKET);
    memcpy(sa.sun_path + 1, METER_SOCKET, len);       /* sun_path[0] = 0: abstract namespace */
    socklen_t alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0 || bind(s, (struct sockaddr *)&sa, alen) || listen(s, METER_CLIENTS)) {
        say("meter socket unavailable: %s", strerror(errno));
        if (s >= 0) close(s);
        return;
    }
    fcntl(s, F_SETFL, O_NONBLOCK); fcntl(s, F_SETFD, FD_CLOEXEC);
    meter_srv = s;
}
static void meter_accept(void) {
    if (meter_srv < 0) return;
    for (int c; (c = accept(meter_srv, NULL, NULL)) >= 0; ) {
        fcntl(c, F_SETFL, O_NONBLOCK); fcntl(c, F_SETFD, FD_CLOEXEC);
        int slot = -1;
        for (int i = 0; i < METER_CLIENTS; i++) if (meter_cl[i] < 0) { slot = i; break; }
        if (slot < 0) { slot = meter_oldest; close(meter_cl[slot]); meter_oldest = (slot + 1) % METER_CLIENTS; }
        meter_cl[slot] = c;
    }
}
static void meter_broadcast(const char *b, int n) {
    for (int i = 0; i < METER_CLIENTS; i++) {
        if (meter_cl[i] < 0) continue;
        if (send(meter_cl[i], b, n, MSG_DONTWAIT | MSG_NOSIGNAL) < 0 && errno != EAGAIN) { close(meter_cl[i]); meter_cl[i] = -1; }
    }
}
typedef struct { long long last; int beats; double peak; } MeterStream;
/* db average_db beat_peak beats selected_band nbands band_db... */
static void meter_stream(MeterStream *m, Ana *a, Frame f, long long t) {
    if (f.beat) m->beats++;
    if (f.beat_ratio > m->peak) m->peak = f.beat_ratio;
    if (t - m->last < 100) return;
    meter_accept();
    char b[512];
    int n = snprintf(b, sizeof b, "%.1f %.1f %.2f %d %d %d", f.db, a->loud_ref_db, m->peak, m->beats, f.band, C.nbands);
    for (int i = 0; i < C.nbands; i++) n += snprintf(b + n, sizeof b - n, " %.1f", f.band_db[i]);
    n += snprintf(b + n, sizeof b - n, "\n");
    meter_broadcast(b, n);
    m->last = t; m->beats = 0; m->peak = 0;
}

/* ---------- speed mode: colour follows the car's speed, no audio involved ---------- */
#define SPEED_POLL_MS 500            /* each read is one connection to HMG-API's vehicle service */
#define SPEED_LOST_MS 15000          /* no readings this long: give the lamp back */
#define KMH_PER_MPH 1.609344

/* "speed 34.5\n" -> km/h; the service answers "-" when it has no value */
static int parse_speed_reply(const char *b, double *kmh) {
    const char *p = strstr(b, "speed ");
    if (!p) return 0;
    char *e; double v = strtod(p + 6, &e);
    if (e == p + 6 || v < 0 || v > 400) return 0;
    *kmh = v;
    return 1;
}
/* one question on HMG-API's vehicle socket (@hmgapi-vehicle; it serves one line per connection) */
static int read_speed(double *kmh) {
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa); sa.sun_family = AF_UNIX;
    const char *name = "hmgapi-vehicle"; size_t len = strlen(name);
    memcpy(sa.sun_path + 1, name, len);
    socklen_t alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len);
    int s = socket(AF_UNIX, SOCK_STREAM, 0); if (s < 0) return 0;
    struct timeval tv = {1, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    char b[128]; int n = 0, ok = 0;
    if (!connect(s, (struct sockaddr *)&sa, alen) && send(s, "read speed\n", 11, MSG_NOSIGNAL) == 11) {
        int k;
        while (n < (int)sizeof b - 1 && (k = recv(s, b + n, sizeof b - 1 - n, 0)) > 0) {
            n += k; b[n] = 0;
            if (strchr(b, '\n')) break;
        }
        b[n] = 0;
        ok = n > 0 && parse_speed_reply(b, kmh);
    }
    close(s);
    return ok;
}
/* the wheel colour for a speed: walks the short way round the factory wheel from the low colour to the high
   one, because the lamp only takes table colours. Pale colours are left out of the walk unless the user picked
   them, and the speeds are shared evenly over the colours left, so no stretch of road sits on one colour. */
static int speed_wheel_index(double kmh) {
    double k = strcmp(C.speed_unit, "kmh") ? KMH_PER_MPH : 1, lo = C.speed_low * k, hi = C.speed_high * k;
    double x = hi > lo ? (kmh - lo) / (hi - lo) : (kmh >= lo ? 1 : 0);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    int a = C.speed_color_low, b = C.speed_color_high, d = (b - a + 64) % 64;
    if (d > 32) d -= 64;
    int steps = d < 0 ? -d : d, dir = d < 0 ? -1 : 1, path[64], n = 0;
    path[n++] = a;
    for (int i = 1; i <= steps; i++) {
        int j = (a + dir * i + 64) % 64;
        if (j == b || !wheel_pale(j)) path[n++] = j;
    }
    return path[(int)lround(x * (n - 1))];
}
typedef struct { long long last_ok, last_sent; int sent; } SpeedDrv;
static void speed_step(SpeedDrv *sd, int *active) {
    double kmh; long long t = now_ms();
    if (!read_speed(&kmh)) {
        if (*active && t - sd->last_ok > SPEED_LOST_MS) {
            say("no speed reading for %d s — lights back to normal", SPEED_LOST_MS / 1000);
            restore_base(); *active = 0; base.valid = 0;
        }
        return;
    }
    sd->last_ok = t;
    if (!*active) {
        snapshot_base();
        take_over_lamp();
        *active = 1; sd->sent = -1;
        say("speed mode — lights following speed (%.1f km/h now)", kmh);
    }
    int idx = speed_wheel_index(kmh);
    reap();
    if (idx != sd->sent && !pending_pid && t - sd->last_sent >= C.min_interval_ms) {
        light_rgb(WHEEL_R[idx], WHEEL_G[idx], WHEEL_B[idx], 0);
        sd->sent = idx; sd->last_sent = t;
        if (C.debug) say("speed %.1f km/h: colour #%d", kmh, idx);
    }
    char b[48]; int n = snprintf(b, sizeof b, "speed %.1f %d\n", kmh, idx);
    meter_broadcast(b, n);
}

/* ---------- single instance ---------- */
static int is_other_daemon(pid_t pid) {
    char p[64], cl[512]; snprintf(p, sizeof p, "/proc/%d/cmdline", pid);
    int fd = open(p, O_RDONLY); if (fd < 0) return 0;
    int n = read(fd, cl, sizeof cl - 1); close(fd);
    if (n <= 0) return 0;
    cl[n] = 0;
    const char *exe = cl, *arg1 = cl + strlen(cl) + 1;
    if (arg1 >= cl + n) arg1 = "";
    if (!strstr(exe, "lightvibesd")) return 0;
    return arg1[0] != '-';            /* --status / --off / --analyze ... are one-shot tools */
}
static int same_binary(pid_t pid) {
    struct stat a, b; char p[64]; snprintf(p, sizeof p, "/proc/%d/exe", pid);
    if (stat(p, &a) || stat("/proc/self/exe", &b)) return 0;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
static volatile sig_atomic_t stop_flag;
static void on_sig(int s) { (void)s; stop_flag = 1; }
static int list_daemons(pid_t *out, int max) {
    int n = 0; DIR *d = opendir("/proc"); struct dirent *e;
    while (d && (e = readdir(d)) && n < max) { pid_t p = atoi(e->d_name); if (p > 1 && p != getpid() && is_other_daemon(p)) out[n++] = p; }
    if (d) closedir(d);
    return n;
}
static void stop_pids(pid_t *f, int n) {
    for (int i = 0; i < n; i++) kill(f[i], SIGTERM);
    for (int w = 0; w < 30 && n; w++) {
        int alive = 0;
        for (int i = 0; i < n; i++) { if (f[i] && kill(f[i], 0) == 0) alive = 1; else f[i] = 0; }
        if (!alive) return;
        usleep(100 * 1000);
    }
    for (int i = 0; i < n; i++) if (f[i]) { say("pid %d did not stop — killing it", f[i]); kill(f[i], SIGKILL); }
    usleep(300 * 1000);
}
/* Outdated copies (e.g. left over from an earlier install) are stopped.  If an identical
   copy is already running we stand by instead of fighting it, and take over if it exits. */
static void stop_other_instances(void) {
    for (;;) {
        pid_t all[32], old[32]; int n = list_daemons(all, 32), no = 0; pid_t twin = 0;
        for (int i = 0; i < n; i++) { if (same_binary(all[i])) twin = all[i]; else old[no++] = all[i]; }
        for (int i = 0; i < no; i++) say("outdated lightvibesd running (pid %d) — stopping it", old[i]);
        if (no) stop_pids(old, no);
        if (!twin) return;
        say("an identical lightvibesd is already running (pid %d) — standing by", twin);
        while (!stop_flag && kill(twin, 0) == 0) sleep(5);
        if (stop_flag) return;
        say("pid %d exited — taking over", twin);
    }
}

/* The car was switched off or we crashed while driving the lamp: put the user's colour back now, music or not. */
static void startup_restore(void) {
    int s[6], lamp[6];
    if (!saved_load(s) || !s[5]) {     /* clean: note the user's colour now */
        snapshot_base(); base.valid = 0;
        return;
    }
    base_from(s);
    say("the last run didn't finish: restoring the saved colour rgb %d %d %d, brightness raw %d", base.r, base.g, base.b, base.bright_raw);
    for (int i = 0; i < 20 && !stop_flag; i++) {   /* the lamp's service may still be coming up */
        apply_base();
        /* with drive-mode colour on, the lamp reports that colour instead of the saved one */
        if (read_lamp(lamp) && (s[4] == 2 ? lamp[4] == 2 : lamp[0] == s[0] && lamp[1] == s[1] && lamp[2] == s[2])) {
            saved_write(0);
            say("saved colour restored");
            base.valid = 0;
            return;
        }
        sleep(3);
    }
    say("the lamp didn't confirm the saved colour; will use it again when we next let go of the lamp");
    base.valid = 0;
}

/* ---------- one-shot tools ---------- */
static int analyze_file(const char *path) {
    C.dry = 1;
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 1; }
    Ana a; ana_init(&a); Drv d; memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
    Meter m; memset(&m, 0, sizeof m);
    short buf[PERIOD * CH]; long long t = 0; int beats = 0;
    show_config();
    while (fread(buf, sizeof(short) * CH, PERIOD, f) == PERIOD) {
        Frame fr = ana_block(&a, buf, PERIOD, t);
        if (fr.beat) { beats++; if (C.debug >= 1) say("t=%6lld ms BEAT ratio=%.2f", t, fr.beat_ratio); }
        meter(&m, &a, fr, t);
        drive(&d, &a, fr, t);
        t += (long long)BLOCK_MS;
    }
    fclose(f);
    say("analyzed %lld ms, %d beats", t, beats);
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *defdir = "/data/data/lightvibes.app/files/settings";

    if (argc >= 2 && (!strcmp(argv[1], "--off") || !strcmp(argv[1], "--stop-all"))) {
        load_config(argc >= 3 ? argv[2] : defdir);
        if (!strcmp(argv[1], "--stop-all")) {
            pid_t f[32]; int n = list_daemons(f, 32);
            for (int i = 0; i < n; i++) say("stopping lightvibesd pid %d", f[i]);
            stop_pids(f, n);
        }
        return set_loopback(0) ? 1 : 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--log")) {      /* --log [lines] [filter] */
        load_config(defdir);
        return show_log(argc >= 3 ? atoi(argv[2]) : 40, argc >= 4 ? argv[3] : NULL);
    }
    if (argc >= 2 && !strcmp(argv[1], "--status")) {
        load_config(argc >= 3 ? argv[2] : defdir);
        struct mixer *m = mixer_open(C.card);
        struct mixer_ctl *c = m ? mixer_get_ctl_by_name(m, C.loop_ctl) : NULL;
        printf("loopback switch \"%s\" = %s\n", C.loop_ctl, c ? (mixer_ctl_get_value(c, 0) ? "ON" : "off") : "(not found)");
        if (m) mixer_close(m);
        int mr = music_rate();
        printf("music playing on device %d (running at 44.1 or 48 kHz) = %s\n", C.device, mr ? (mr == 48000 ? "yes, 48 kHz" : "yes, 44.1 kHz") : "no");
        char st[96]; snprintf(st, sizeof st, "/proc/asound/card%d/pcm%dc/sub0/status", C.card, C.device);
        printf("capture side: %s\n", file_has(st, "RUNNING") ? "RUNNING (service listening)" : file_has(st, "PREPARED") ? "PREPARED" : file_has(st, "XRUN") ? "XRUN" : "closed");
        pid_t f[32]; int n = list_daemons(f, 32);
        for (int i = 0; i < n; i++) printf("service process: pid %d\n", f[i]);
        printf("running service processes: %d%s\n", n, n > 1 ? "  <-- more than one!" : "");
        C.dry = 1; show_config();
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--testcall")) {
        load_config(defdir);
        light_config(9, 1, 1);   /* custom colours only show with drive-mode colour off */
        char er[16], eg[16], eb[16];
        enc(er, sizeof er, atoi(argv[2])); enc(eg, sizeof eg, atoi(argv[3])); enc(eb, sizeof eb, atoi(argv[4]));
        char *a[] = {C.service_bin, "call", C.svc_name, "78", "i32", "2", "i32", er, "i32", "2", "i32", eg, "i32", "2", "i32", eb, NULL};
        printf("running:"); for (int i = 0; a[i]; i++) printf(" %s", a[i]); printf("\n"); fflush(stdout);
        spawn(a, 1, 1);
        if (argc >= 6) light_bright_raw(atoi(argv[5]) + 1, 1);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--analyze")) {
        load_config(argc >= 4 ? argv[3] : "/nonexistent");
        if (!C.debug) C.debug = 2;
        return analyze_file(argv[2]);
    }
    if (argc < 2 || argv[1][0] == '-') { fprintf(stderr, "usage: lightvibesd <state-dir> [fallback-dir] | --status | --log [lines] [filter] | --stop-all | --off | --testcall R G B [level] | --analyze file.raw [dir]\n"); return 2; }

    settings_dir = argv[1];
    if (argc >= 3) fallback_dir = argv[2];
    load_config(active_dir());
    log_open();
    say("lightvibesd %s starting", VERSION);
    show_config();

    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_sig;
    sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL); sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    stop_other_instances();
    if (stop_flag) { say("stopped while standing by"); return 0; }
    meter_listen();
    startup_restore();

    short *buf = malloc(sizeof(short) * PERIOD * CH);
    int lights_active = 0;
    long long stamp = config_stamp();
    long long last_cfg = 0;
    int tapped = 0;
    SpeedDrv sd; memset(&sd, 0, sizeof sd);

    while (!stop_flag) {
        meter_accept();
        long long s = config_stamp();
        if (s != stamp) { stamp = s; load_config(active_dir()); say("settings changed"); show_config(); }
        if (lights_active && (!C.enabled || strcmp(C.mode, "speed"))) {   /* left speed mode */
            restore_base(); lights_active = 0; base.valid = 0;
        }
        if (!C.enabled) {
            if (tapped) { set_loopback(0); tapped = 0; say("switched off in settings"); }
            usleep(500 * 1000); continue;
        }
        if (!strcmp(C.mode, "speed")) {
            if (tapped) { set_loopback(0); tapped = 0; }
            speed_step(&sd, &lights_active);
            usleep(SPEED_POLL_MS * 1000); continue;
        }
        if (!tapped) {
            if (set_loopback(1)) { say("cannot enable loopback; exiting"); return 1; }
            tapped = 1;
        }
        int playing = music_rate();
        if (!playing) { usleep(500 * 1000); continue; }
        rate = playing;

        struct pcm_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.channels = CH; cfg.rate = rate; cfg.period_size = PERIOD; cfg.period_count = 16;
        cfg.format = PCM_FORMAT_S16_LE;
        struct pcm *pcm = pcm_open(C.card, C.device, PCM_IN, &cfg);
        if (!pcm || !pcm_is_ready(pcm)) {
            say("capture open failed: %s", pcm ? pcm_get_error(pcm) : "?");
            if (pcm) pcm_close(pcm);
            sleep(2); continue;
        }
        set_loopback(1);                          /* the driver may reset it when streams reopen */
        if (!lights_active) snapshot_base();      /* read the user's settings before we touch them */
        if (pcm_start(pcm)) say("capture start failed: %s", pcm_get_error(pcm));
        say("music detected — listening on pcmC%dD%dc at %d Hz", C.card, C.device, rate);

        Ana a; ana_init(&a); Drv d; memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
        Meter mtr; memset(&mtr, 0, sizeof mtr);
        MeterStream ms; memset(&ms, 0, sizeof ms);
        int zero_blocks = 0, stalls = 0, xruns = 0;
        long long t0 = now_ms(), quiet_since = -1, sound_since = -1, last_check = 0, last_fix = -100000;
        while (!stop_flag) {
            long long t = now_ms() - t0;
            if (t - last_check >= 80) {           /* Siri / call / source change: let go fast */
                last_check = t;
                if (music_rate() != rate) { say("playback stopped or changed rate — releasing capture"); break; }
            }
            if (t - last_cfg >= 500 || t < last_cfg) {   /* live settings */
                last_cfg = t;
                long long s2 = config_stamp();
                if (s2 != stamp) {
                    stamp = s2; load_config(active_dir()); say("settings changed"); show_config();
                    if (!C.enabled || !strcmp(C.mode, "speed")) break;
                    ana_init(&a);                 /* bands and beat settings may have changed */
                    d.sent_idx = d.sent_level = -1;
                    if (lights_active && base.valid && !strcmp(C.mode, "pulse")) { wait_pending(); light_rgb(base.r, base.g, base.b, 1); }   /* pulse keeps the user's colour */
                }
            }
            int w = pcm_wait(pcm, 150);
            if (w < 0) {                          /* overrun: recover in place */
                if (++xruns == 1 || xruns % 50 == 0) say("capture overrun (%d) — recovering", xruns);
                pcm_prepare(pcm); pcm_start(pcm);
                continue;
            }
            if (w == 0) {
                if (++stalls == 1 || stalls % 20 == 0) say("capture stalled (%d times)", stalls);
                if (stalls >= 20) { say("capture not delivering audio — reopening"); break; }
                continue;
            }
            stalls = 0;
            if (pcm_readi(pcm, buf, PERIOD) < 0) {
                if (++xruns == 1 || xruns % 50 == 0) say("capture read error (%d): %s — recovering", xruns, pcm_get_error(pcm));
                pcm_prepare(pcm); pcm_start(pcm);
                if (xruns > 500) { say("too many capture errors — reopening"); break; }
                continue;
            }
            int nz = 0; for (int i = 0; i < PERIOD * CH; i++) if (buf[i]) { nz = 1; break; }
            if (!nz) {
                if (++zero_blocks >= 47 && t - last_fix >= 5000) {   /* ~1 s of digital zeros */
                    say("capture is all zeros while music plays — re-enabling loopback");
                    set_loopback(1); last_fix = t; zero_blocks = 0;
                }
            } else zero_blocks = 0;

            Frame f = ana_block(&a, buf, PERIOD, t);
            meter(&mtr, &a, f, t);
            meter_stream(&ms, &a, f, t);
            if (f.db > C.silence_db) {
                quiet_since = -1;
                if (sound_since < 0) sound_since = t;
                if (!lights_active && rate != MAX_RATE && t - sound_since < SOURCE_START_MS) continue;
                if (!lights_active) {
                    if (!base.valid) snapshot_base();
                    take_over_lamp();
                    lights_active = 1; say("music started — lights following (%s)", C.mode);
                }
                drive(&d, &a, f, t);
            } else if (!lights_active) {
                sound_since = -1;
            } else {
                sound_since = -1;
                if (quiet_since < 0) quiet_since = t;
                if (t - quiet_since >= C.idle_seconds * 1000LL) {
                    restore_base(); lights_active = 0; base.valid = 0; say("music quiet — lights back to normal");
                }
            }
        }
        pcm_close(pcm);
        if (lights_active) { restore_base(); lights_active = 0; base.valid = 0; }
        if (!stop_flag) usleep(300 * 1000);
    }

    say("stopping");
    if (lights_active) restore_base();
    set_loopback(0);
    return 0;
}
