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
 *
 * The app's Report a problem page asks for a lamp test through files/diag/ (see "lamp test" below).
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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <tinyalsa/asoundlib.h>

#define VERSION "0.7.0"
#define MAX_RATE 48000
static int rate = MAX_RATE;   /* the capture side only runs at the rate playback is using */
#define SOURCE_START_MS 2000  /* Android-mixer audio must last this long: it also carries short system sounds */
#define CH 2
#define PERIOD 512                  /* 10.7 ms per analysis step at 48 kHz */
#define NFFT 2048
#define BIN_HZ ((double)rate / NFFT)
#define BLOCK_MS (1000.0 * PERIOD / rate)
#define MAXBANDS 12
#define MAXWHEEL 74

extern char **environ;

/* ---------- factory 64-colour custom wheels + Hyundai Ioniq 5 0x01xx stock mood colours (64..73) ---------- */
static const unsigned short KIA_R[64] = {16,16,16,33,47,64,82,100,120,136,155,169,183,193,204,213,221,228,235,239,246,250,254,255,255,255,255,255,255,255,255,255,255,255,254,255,246,233,220,202,166,168,149,132,117,104,93,81,71,62,53,44,37,30,25,20,16,16,16,16,16,16,16,16};
static const unsigned short KIA_G[64] = {67,46,32,28,28,28,28,28,28,29,33,35,35,33,31,27,23,19,14,10,6,4,1,50,68,80,105,120,143,166,195,205,218,232,243,255,255,255,255,252,226,243,235,229,223,221,222,225,231,238,245,250,250,250,250,250,244,230,210,187,163,138,111,89};
static const unsigned short KIA_B[64] = {244,242,239,239,239,239,239,239,239,239,239,239,234,224,210,192,174,153,153,116,96,81,69,29,35,39,48,53,62,70,88,118,158,200,230,255,246,221,188,147,91,98,98,98,98,98,105,118,133,152,169,189,207,225,237,249,252,252,252,252,252,252,252,248};

/* 0..63: Gen5W custom wheel; 64..73: verified Ioniq 5 0x01xx stock themes (Polar, Soft, Ice, Ocean, Teal, Emerald, Gold, Coral, Orchid, Purple) */
static const unsigned short HYUNDAI_R[74] = {
    176,195,205,232,255,255,255,255,255,255,255,255,254,255,254,254,255,255,255,255,255,255,255,254,255,255,255,255,255,255,255,255,255,254,255,254,247,230,210,175,119,58,10,0,0,0,0,0,0,0,0,0,25,55,81,100,109,122,125,128,130,143,156,167,
    485,500,439,280,259,382,510,489,443,394
};
static const unsigned short HYUNDAI_G[74] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,107,73,88,113,125,3,34,48,60,65,137,148,160,172,177,191,207,216,237,255,254,255,254,254,255,255,217,180,161,140,125,115,99,85,62,0,0,0,0,42,52,25,0,0,0,0,0,
    498,489,484,398,455,471,424,318,271,293
};
static const unsigned short HYUNDAI_B[74] = {
    255,254,254,254,241,195,158,137,117,111,106,101,95,72,58,30,106,116,73,88,113,115,10,0,23,42,49,107,119,111,113,108,123,138,147,165,182,186,188,191,196,198,235,245,255,255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    506,479,508,509,448,277,273,318,492,508
};

static const unsigned short *WHEEL_R = KIA_R, *WHEEL_G = KIA_G, *WHEEL_B = KIA_B;
static int wheel_n = 64;
static int vivid_ring[MAXWHEEL], vivid_n = 0;
static double vivid_hue[MAXWHEEL];
static const char *active_palette = "kia";
static const char *build_prop_path = "/system/build.prop";
static const char *hmgapi_json_path = "/data/hmgapi/settings/hmgapi.json";

/* ---------- config ---------- */
static const char *log_path = "/data/hmgapi/logs/light-vibes/lightvibesd.log";

typedef struct { int lo, hi, color, hex_rgb; double gain; int r, g, b; } Band;   /* color = wheel index, or -1 for #RRGGBB snapped to active vivid wheel */

static struct {
    char dir[256];
    int enabled, debug, dry;
    char palette[16];           /* auto | kia | hyundai */
    char mode[16];              /* synesthesia | spectrum | beat | pulse | duet | ember | drift | nightdrive | speed | power | efficiency */
    int speed_low, speed_high, speed_color_low, speed_color_high;   /* speed mode: in speed_unit, wheel indexes */
    char speed_unit[8];         /* mph | kmh */
    int power_low, power_high, power_color_low, power_color_zero, power_color_high;   /* power mode: kW, negative = regen */
    int eff_low, eff_high, eff_color_low, eff_color_high;           /* efficiency mode: tenths, in eff_unit */
    char eff_unit[8];           /* mi (mi/kWh) | km (km/kWh) */
    char eff_source[12];        /* average: the car's own average | trip: this drive, worked out here */
    char gauge_bright[8];       /* speed/power/efficiency brightness: 1-10, or own (0.8.0's "auto" = 10 with dim on) */
    int gauge_dim;              /* and the car's "dimmed in dark" with it: 1 on, 0 off */
    int bright_fixed;           /* spectrum/beat/synesthesia/drift: one level instead of following the music */
    char preset[16];            /* standard | repo */
    char bright_curve[16];      /* range | repo */
    int min_interval_ms;        /* per command type */
    int bright_min, bright_max; /* UI levels 0..10 */
    int loudness_range_db;      /* dB above/below average that spans the brightness range */
    int sensitivity;            /* beat: percent above the running beat-band average */
    int beat_low_hz, beat_high_hz;
    int beat_avg_ms;            /* running-average window for the beat band */
    int min_beat_ms;
    int flash_on_beat;          /* -1 mode default, 0 loudness, 1 flash on beat, 2 beat pulse */
    int flash_ms;
    int beat_step;              /* wheel steps per beat (beat mode) */
    int skip_white;
    int auto_bpm;               /* 1 = detect track BPM and scale flash, pulse hold/decay, and beat cooldown */
    int pulse_decay;            /* pulse/ember/nightdrive/beat-pulse: percent of brightness kept per 100 ms */
    int pulse_color;            /* pulse: the wheel colour it pulses in, or -1 for the owner's own colour */
    int pulse_set_car;          /* pulse: also make pulse_color the car's ambient colour (it stays after the music) */
    int duet_bed, duet_accent, duet_beats; /* duet: melody bed (-1 = wheel), beat accent (-1 = 180 deg complement), beats per pair (0 = locked) */
    int syn_shift;              /* synesthesia: hue rotation in degrees (0..359) */
    int ember_theme;            /* ember: thermal palette (0=fire, 1=plasma, 2=aurora) */
    int drift_theme, drift_beats; /* drift: mood theme (0..4) and musical beats per colour step (2..16) */
    char nd_source[12];         /* nightdrive: "power" (motor kW & regen) or "speed" (vehicle speed) */
    int switch_margin;          /* spectrum: percent a new band must beat the current one by */
    int band_smoothing_ms;
    int spectrum_dynamic;       /* spectrum: 1 = auto-adapt each band to its running baseline, 0 = fixed boosts only */
    Band bands[MAXBANDS]; int nbands;
    char bands_text[256];
    int silence_db, idle_seconds;
    int override_drive_mode;
    int card, device;
    char loop_ctl[96];
    char service_bin[96], logcat_bin[96], svc_name[64];
} C;

/* the car's own distance unit, from the EV service's TRIP_UNIT (flag 12): 2 miles, 1 km (checked by switching the
   EV6 to km, 2026-11-07); -1 until the sampler has read it. Settings say "auto" to follow it. */
static volatile int car_mi = -1;
static int speed_in_kmh(void) { return !strcmp(C.speed_unit, "kmh") || (!strcmp(C.speed_unit, "auto") && car_mi == 0); }
static int eff_in_km(void) { return !strcmp(C.eff_unit, "km") || (!strcmp(C.eff_unit, "auto") && car_mi == 0); }

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
    if (strstr(s, "cvev") || strstr(s, "sg2ev") || strstr(s, "dl3") || strstr(s, "ev6") || strstr(s, "ecokia") || strstr(s, "kia")) return "kia";
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
static double rgb_hue_deg(int r, int g, int b) {
    r &= 0xff; g &= 0xff; b &= 0xff;
    int mx = r > g ? r : g; if (b > mx) mx = b;
    int mn = r < g ? r : g; if (b < mn) mn = b;
    int d = mx - mn;
    if (d <= 0) return 0.0;
    double h;
    if (mx == r) h = 60.0 * ((g - b) / (double)d);
    else if (mx == g) h = 60.0 * (2.0 + (b - r) / (double)d);
    else h = 60.0 * (4.0 + (r - g) / (double)d);
    if (h < 0.0) h += 360.0;
    return h;
}
static int wheel_saturated(int i) {   /* >=78% sat, or >=52% on true leaf greens (g-b>=75) so Kia greens stay vivid while pale mints are excluded */
    int r = WHEEL_R[i] & 0xff, g = WHEEL_G[i] & 0xff, b = WHEEL_B[i] & 0xff;
    int mx = r > g ? r : g; if (b > mx) mx = b;
    int mn = r < g ? r : g; if (b < mn) mn = b;
    int min_sat = (g >= r && g >= b && g - r >= 55 && g - b >= 75) ? 52 : 78;
    return mx > 0 && (mx - mn) * 100 >= min_sat * mx;
}
static int nearest_vivid_wheel(int r, int g, int b) {
    r &= 0xff; g &= 0xff; b &= 0xff;
    int best = 0, best_d = 1000000000;
    for (int i = 0; i < wheel_n; i++) {
        if (!wheel_saturated(i)) continue;
        int dr = (int)(WHEEL_R[i] & 0xff) - r, dg = (int)(WHEEL_G[i] & 0xff) - g, db = (int)(WHEEL_B[i] & 0xff) - b;
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}
static double wheel_hue(int idx) {
    if (idx < 0 || idx >= wheel_n) idx = 0;
    return rgb_hue_deg(WHEEL_R[idx], WHEEL_G[idx], WHEEL_B[idx]);
}
static int vivid_at_hue(double deg) {
    if (vivid_n <= 0) return 0;
    deg = fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    int best = vivid_ring[0];
    double best_d = 999.0;
    for (int k = 0; k < vivid_n; k++) {
        double d = fabs(vivid_hue[k] - deg);
        if (d > 180.0) d = 360.0 - d;
        if (d < best_d) { best_d = d; best = vivid_ring[k]; }
    }
    return best;
}
static void set_palette(const char *name) {
    if (!strcmp(name, "hyundai")) {
        WHEEL_R = HYUNDAI_R; WHEEL_G = HYUNDAI_G; WHEEL_B = HYUNDAI_B;
        wheel_n = 74;
        active_palette = "hyundai";
    } else {
        WHEEL_R = KIA_R; WHEEL_G = KIA_G; WHEEL_B = KIA_B;
        wheel_n = 64;
        active_palette = "kia";
    }
    vivid_n = 0;
    for (int i = 0; i < wheel_n; i++) {
        if (!wheel_saturated(i)) continue;
        double h = wheel_hue(i);
        int pos = vivid_n++;
        while (pos > 0 && vivid_hue[pos - 1] > h) {
            vivid_hue[pos] = vivid_hue[pos - 1];
            vivid_ring[pos] = vivid_ring[pos - 1];
            pos--;
        }
        vivid_hue[pos] = h;
        vivid_ring[pos] = i;
    }
    for (int i = 0; i < C.nbands; i++) {
        int col = C.bands[i].color;
        if (C.bands[i].hex_rgb >= 0) {
            int hx = C.bands[i].hex_rgb;
            col = nearest_vivid_wheel((hx >> 16) & 255, (hx >> 8) & 255, hx & 255);
        } else if (col >= wheel_n) {
            col = nearest_vivid_wheel(HYUNDAI_R[col], HYUNDAI_G[col], HYUNDAI_B[col]);
        }
        if (col >= 0 && col < wheel_n) {
            C.bands[i].r = WHEEL_R[col];
            C.bands[i].g = WHEEL_G[col];
            C.bands[i].b = WHEEL_B[col];
        }
    }
}
static int palette_has_rgb(const unsigned short *r_tab, const unsigned short *g_tab, const unsigned short *b_tab, int r, int g, int b) {
    int n = (r_tab == HYUNDAI_R) ? 74 : 64;
    for (int i = 0; i < n; i++)
        if (r_tab[i] == r && g_tab[i] == g && b_tab[i] == b) return 1;
    return 0;
}
static int cfg_color(const char *k, int def, int min_special) {
    char s[64];
    cfg_str(k, s, sizeof s, "");
    if (!s[0]) return def;
    unsigned hex = 0;
    if (s[0] == '#' && sscanf(s + 1, "%6x", &hex) == 1)
        return nearest_vivid_wheel((hex >> 16) & 255, (hex >> 8) & 255, hex & 255);
    char *e; long v = strtol(s, &e, 10);
    if (e == s) return def;
    if (v < min_special) return min_special;
    if (v >= wheel_n) {
        if (v < 74) return nearest_vivid_wheel(HYUNDAI_R[v], HYUNDAI_G[v], HYUNDAI_B[v]);
        return wheel_n - 1;
    }
    return (int)v;
}

/* bands: "lo-hi:colour:boost ..." e.g. "40-150:22:1 150-500:#FF3C0F:2" (wheel index 0..73 or #RRGGBB snapped to active vivid wheel) */
static const char *DEFAULT_BANDS = "40-130:#FF0020:1.6 130-450:#FF3C0F:2.0 450-2000:#00FF40:3.2 2000-6000:#00AAFF:9 6000-16000:#9100FF:32";
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
        if (k < 3 || used == 0 || lo < 0 || hi <= lo || hi > MAX_RATE / 2 || g < 0 || (!is_hex && (col < 0 || col >= MAXWHEEL))) return -1;
        out[n].lo = lo; out[n].hi = hi; out[n].gain = g;
        if (is_hex) {
            int vi = nearest_vivid_wheel((hex >> 16) & 255, (hex >> 8) & 255, hex & 255);
            out[n].color = -1; out[n].hex_rgb = (int)hex;
            out[n].r = WHEEL_R[vi]; out[n].g = WHEEL_G[vi]; out[n].b = WHEEL_B[vi];
        } else {
            int ec = col >= wheel_n ? nearest_vivid_wheel(HYUNDAI_R[col], HYUNDAI_G[col], HYUNDAI_B[col]) : col;
            out[n].color = col; out[n].hex_rgb = -1;
            out[n].r = WHEEL_R[ec]; out[n].g = WHEEL_G[ec]; out[n].b = WHEEL_B[ec];
        }
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
    if (!strcmp(C.mode, "drop")) snprintf(C.mode, sizeof C.mode, "synesthesia");
    C.min_interval_ms = clampi(cfg_int("min_interval_ms", repo ? 100 : 50), 20, 5000);
    C.speed_low = clampi(cfg_int("speed_low", 0), 0, 300);
    C.speed_high = clampi(cfg_int("speed_high", 80), 0, 300);
    C.speed_color_low = cfg_color("speed_color_low", 44, 0);
    C.speed_color_high = cfg_color("speed_color_high", 22, 0);
    cfg_str("speed_unit", C.speed_unit, sizeof C.speed_unit, "auto");   /* auto: the car's own units */
    C.power_low = clampi(cfg_int("power_low", -50), -300, 400);
    C.power_high = clampi(cfg_int("power_high", 150), -300, 400);
    C.power_color_low = cfg_color("power_color_low", 0, 0);
    C.power_color_zero = cfg_color("power_color_zero", 44, 0);
    C.power_color_high = cfg_color("power_color_high", 22, 0);
    C.eff_low = clampi(cfg_int("eff_low", 20), 0, 200);
    C.eff_high = clampi(cfg_int("eff_high", 45), 0, 200);
    C.eff_color_low = cfg_color("eff_color_low", 22, 0);
    C.eff_color_high = cfg_color("eff_color_high", 44, 0);
    cfg_str("eff_unit", C.eff_unit, sizeof C.eff_unit, "auto");
    cfg_str("gauge_brightness", C.gauge_bright, sizeof C.gauge_bright, "10");   /* as the app shows it until changed */
    C.gauge_dim = cfg_int("gauge_dim", 0) ? 1 : 0;
    if (!strcmp(C.gauge_bright, "auto")) { snprintf(C.gauge_bright, sizeof C.gauge_bright, "10"); C.gauge_dim = 1; }
    cfg_str("eff_source", C.eff_source, sizeof C.eff_source, "average");
    C.bright_min = clampi(cfg_int("brightness_min", 1), 0, 10);
    C.bright_max = clampi(cfg_int("brightness_max", 10), 0, 10);
    if (C.bright_max < C.bright_min) C.bright_max = C.bright_min;
    C.loudness_range_db = clampi(cfg_int("loudness_range_db", 6), 2, 40);
    C.sensitivity = clampi(cfg_int("sensitivity", 150), 105, 500);
    C.beat_low_hz = clampi(cfg_int("beat_low_hz", 40), 20, 20000);
    C.beat_high_hz = clampi(cfg_int("beat_high_hz", 180), C.beat_low_hz + 20, 22000);
    C.beat_avg_ms = clampi(cfg_int("beat_avg_ms", 1500), 200, 10000);
    C.min_beat_ms = clampi(cfg_int("min_beat_ms", 200), 60, 3000);
    C.flash_on_beat = cfg_int("flash_on_beat", repo ? 0 : -1);   /* -1 = mode default */
    C.flash_ms = clampi(cfg_int("flash_ms", 120), 0, 2000);
    C.beat_step = clampi(cfg_int("beat_step", 19), 1, 32);
    C.skip_white = cfg_int("skip_white", 1);
    C.auto_bpm = cfg_int("auto_bpm", repo ? 0 : 1) ? 1 : 0;
    C.pulse_decay = clampi(cfg_int("pulse_decay", 35), 0, 99);
    C.pulse_color = cfg_color("pulse_color", -1, -2);   /* -1 the car's own colour, -2 the drive-mode colour */
    C.pulse_set_car = cfg_int("pulse_set_car", 0) ? 1 : 0;
    C.duet_bed = cfg_color("duet_bed", -1, -1);
    C.duet_accent = cfg_color("duet_accent", -1, -1);
    C.duet_beats = clampi(cfg_int("duet_beats", C.duet_bed >= 0 ? 0 : 4), 0, 16);
    C.syn_shift = ((cfg_int("syn_shift", 0) % 360) + 360) % 360;
    C.ember_theme = clampi(cfg_int("ember_theme", 0), 0, 2);
    C.drift_theme = clampi(cfg_int("drift_theme", 0), 0, 4);
    C.drift_beats = clampi(cfg_int("drift_beats", 4), 2, 16);
    cfg_str("nd_source", C.nd_source, sizeof C.nd_source, "power");
    if (strcmp(C.nd_source, "speed")) snprintf(C.nd_source, sizeof C.nd_source, "power");
    C.switch_margin = clampi(cfg_int("switch_margin", 15), 0, 500);
    C.band_smoothing_ms = clampi(cfg_int("band_smoothing_ms", repo ? 128 : (!strcmp(C.mode, "synesthesia") ? 115 : 20)), 0, 3000);
    C.spectrum_dynamic = cfg_int("spectrum_dynamic", repo ? 0 : 1) ? 1 : 0;
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
    int can_fix = !strcmp(C.mode, "spectrum") || !strcmp(C.mode, "beat") || !strcmp(C.mode, "synesthesia") || !strcmp(C.mode, "drift");
    C.bright_fixed = cfg_int("brightness_fixed", 0) && can_fix;
    if (C.bright_fixed) {        /* the colour still follows the music; the level stays put, with no flashes */
        int lvl = !strcmp(C.gauge_bright, "own") ? 10 : clampi(atoi(C.gauge_bright), 1, 10);
        C.bright_min = C.bright_max = lvl;
        C.flash_on_beat = 0;
    }
}
/* modes driven solely by a reading from the car rather than the music */
static int gauge_mode(void) {
    return !strcmp(C.mode, "speed") || !strcmp(C.mode, "power") || !strcmp(C.mode, "efficiency");
}
static int flash_enabled(void) {
    if (C.flash_on_beat >= 0) return C.flash_on_beat == 1;
    return !strcmp(C.mode, "beat") || !strcmp(C.mode, "duet");
}
static int beat_pulse_enabled(void) {
    return !C.bright_fixed && C.flash_on_beat == 2
        && (!strcmp(C.mode, "spectrum") || !strcmp(C.mode, "beat") || !strcmp(C.mode, "synesthesia") || !strcmp(C.mode, "drift"));
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
static int log_tail(FILE *out, int n, const char *filt) {
    FILE *f = fopen(log_path, "r");
    if (!f) { fprintf(out, "(no log yet: %s)\n", log_path); return 1; }
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
    for (int i = start; i < cnt; i++) fputs(ring[i % n], out);
    if (!cnt) fprintf(out, "(nothing%s%s in %s yet)\n", filt ? " matching " : "", filt ? filt : "", log_path);
    return 0;
}
static long long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void show_config(void) {
    say("settings: palette=%s (%s) preset=%s brightness_curve=%s", active_palette, C.palette, C.preset, C.bright_curve);
    say("settings: mode=%s brightness %d..%d loudness_range=%ddB | beat %d-%dHz auto_bpm=%d sensitivity=%d%% avg=%dms min=%dms flash=%d/%dms pulse=%d step=%d | pulse_decay=%d | spectrum dynamic=%d margin=%d%% smoothing=%dms bands=\"%s\" | interval=%dms idle=%ds silence=%ddB drive_mode_override=%d",
        C.mode, C.bright_min, C.bright_max, C.loudness_range_db, C.beat_low_hz, C.beat_high_hz, C.auto_bpm, C.sensitivity, C.beat_avg_ms,
        C.min_beat_ms, flash_enabled(), C.flash_ms, beat_pulse_enabled(), C.beat_step, C.pulse_decay, C.spectrum_dynamic, C.switch_margin, C.band_smoothing_ms, C.bands_text,
        C.min_interval_ms, C.idle_seconds, C.silence_db, C.override_drive_mode);
    if (!strcmp(C.mode, "duet")) say("settings: duet bed=%d accent=%d beats=%d", C.duet_bed, C.duet_accent, C.duet_beats);
    if (!strcmp(C.mode, "synesthesia")) say("settings: synesthesia shift=%d deg smoothing=%dms", C.syn_shift, C.band_smoothing_ms);
    if (!strcmp(C.mode, "ember")) say("settings: ember theme=%d decay=%d%%", C.ember_theme, C.pulse_decay);
    if (!strcmp(C.mode, "drift")) say("settings: drift theme=%d beats=%d fixed=%d", C.drift_theme, C.drift_beats, C.bright_fixed);
    if (!strcmp(C.mode, "nightdrive")) {
        if (!strcmp(C.nd_source, "speed")) say("settings: nightdrive (speed) %d..%d %s = #%d..#%d", C.speed_low, C.speed_high, C.speed_unit, C.speed_color_low, C.speed_color_high);
        else say("settings: nightdrive (power) %d kW = #%d, 0 kW = #%d, %d kW = #%d", C.power_low, C.power_color_low, C.power_color_zero, C.power_high, C.power_color_high);
    }
    if (!strcmp(C.mode, "speed")) say("settings: speed %d %s = colour #%d, %d %s = colour #%d", C.speed_low, C.speed_unit, C.speed_color_low, C.speed_high, C.speed_unit, C.speed_color_high);
    if (!strcmp(C.mode, "power")) say("settings: power %d kW = colour #%d, 0 kW = colour #%d, %d kW = colour #%d", C.power_low, C.power_color_low, C.power_color_zero, C.power_high, C.power_color_high);
    if (!strcmp(C.mode, "efficiency")) say("settings: efficiency (%s) %.1f %s/kWh = colour #%d, %.1f = colour #%d", C.eff_source, C.eff_low / 10.0, C.eff_unit, C.eff_color_low, C.eff_high / 10.0, C.eff_color_high);
}

static volatile sig_atomic_t stop_flag;

/* ---------- child processes (service call / logcat) ---------- */
static pid_t pending_pid = 0;
static pid_t check_pid = 0;                 /* the lamp check running in the background, if any */
static void check_finished(int status);
/* A persistent lamp helper (LampHelper in the app's APK, run with app_process) keeps the system service's binder and
   sends each command in well under a millisecond, instead of starting /system/bin/service every time. If it can't
   start, or stops answering, commands go through service call as before. */
static pid_t helper_pid = 0;
static int helper_in = -1, helper_out = -1, helper_failed = 0;

static void reap(void) {
    int st; pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        if (p == pending_pid) pending_pid = 0;
        if (p == check_pid) { check_pid = 0; check_finished(st); }
        if (p == helper_pid) {
            helper_pid = 0;
            if (helper_in >= 0) { close(helper_in); helper_in = -1; }
            if (helper_out >= 0) { close(helper_out); helper_out = -1; }
        }
    }
}
static void wait_pending(void) { reap(); if (pending_pid) { waitpid(pending_pid, NULL, 0); pending_pid = 0; } }
static void helper_stop(void) {
    if (helper_in >= 0) { close(helper_in); helper_in = -1; }
    if (helper_out >= 0) { close(helper_out); helper_out = -1; }
    if (helper_pid > 0) { kill(helper_pid, SIGTERM); waitpid(helper_pid, NULL, 0); helper_pid = 0; }
}
static int helper_wait_line(int fd, int timeout_ms, const char *want) {
    char buf[128]; int n = 0;
    long long deadline = now_ms() + timeout_ms;
    while (n < (int)sizeof buf - 1) {
        long long rem = deadline - now_ms();
        if (rem <= 0) return 0;
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        struct timeval tv = { (int)(rem / 1000), (int)((rem % 1000) * 1000) };
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) return 0;
        ssize_t r = read(fd, buf + n, 1);
        if (r <= 0) return 0;
        if (buf[n] == '\n') { buf[n] = 0; return !want || strstr(buf, want) != NULL; }
        n++;
    }
    return 0;
}
static int helper_start(void) {
    if (helper_pid > 0 && helper_in >= 0) return 1;
    if (helper_failed || C.dry) return 0;
    if (access("/system/bin/app_process", X_OK) != 0) { helper_failed = 1; return 0; }
    char apk[320] = "/data/hmgapi/mods/light-vibes/app/LightVibes.apk";
    char exe[256]; ssize_t elen = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (elen > 0) {
        exe[elen] = 0;
        char *sl = strrchr(exe, '/');
        if (sl) { *sl = 0; snprintf(apk, sizeof apk, "%s/app/LightVibes.apk", exe); }
    }
    if (access(apk, R_OK) != 0) snprintf(apk, sizeof apk, "/data/hmgapi/mods/light-vibes/app/LightVibes.apk");
    if (access(apk, R_OK) != 0) { helper_failed = 1; return 0; }
    mkdir("/data/hmgapi/logs", 0755);
    mkdir("/data/hmgapi/logs/light-vibes", 0755);
    mkdir("/data/hmgapi/logs/light-vibes/dalvik-cache", 0755);
    int in_p[2], out_p[2];
    if (pipe(in_p) != 0) return 0;
    if (pipe(out_p) != 0) { close(in_p[0]); close(in_p[1]); return 0; }
    pid_t p = fork();
    if (p < 0) { close(in_p[0]); close(in_p[1]); close(out_p[0]); close(out_p[1]); return 0; }
    if (p == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(in_p[0], 0);
        dup2(out_p[1], 1);
        if (nul >= 0) { dup2(nul, 2); close(nul); }
        close(in_p[0]); close(in_p[1]); close(out_p[0]); close(out_p[1]);
        setenv("CLASSPATH", apk, 1);
        setenv("ANDROID_DATA", "/data/hmgapi/logs/light-vibes", 1);
        unsetenv("LD_PRELOAD");
        char *a[] = {"/system/bin/app_process", "/system/bin", "lightvibes.app.LampHelper", C.svc_name, NULL};
        execve(a[0], a, environ);
        _exit(127);
    }
    close(in_p[0]); close(out_p[1]);
    fcntl(in_p[1], F_SETFD, FD_CLOEXEC);
    fcntl(out_p[0], F_SETFD, FD_CLOEXEC);
    if (!helper_wait_line(out_p[0], 2500, "ready")) {
        kill(p, SIGKILL); waitpid(p, NULL, 0);
        close(in_p[1]); close(out_p[0]);
        helper_failed = 1;
        say("lamp helper did not start — using service call fallback");
        return 0;
    }
    int fl = fcntl(in_p[1], F_GETFL);
    if (fl >= 0) fcntl(in_p[1], F_SETFL, fl | O_NONBLOCK);
    helper_pid = p; helper_in = in_p[1]; helper_out = out_p[0];
    say("lamp helper ready (pid %d)", (int)p);
    return 1;
}
static int helper_send(const char *cmd, int wait_for_it) {
    reap();
    if (!helper_start()) return 0;
    size_t len = strlen(cmd);
    if (write(helper_in, cmd, len) != (ssize_t)len) {
        helper_stop();
        return 0;
    }
    if (wait_for_it && !helper_wait_line(helper_out, 1000, "ok")) {
        helper_stop();
        return 0;
    }
    return 1;
}
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

/* what we've asked of the lamp, for the report and the lamp check */
static struct { int colours, brights, sessions; double peak_db; } totals = {0, 0, 0, -200};
static int last_rgb[3] = {-1, -1, -1}, last_raw = -1;    /* since we last took the lamp over */

static void light_rgb(int r, int g, int b, int wait_for_it) {
    if (C.dry) { say("DRY rgb %d %d %d", r, g, b); return; }
    totals.colours++; last_rgb[0] = r; last_rgb[1] = g; last_rgb[2] = b;
    char cmd[64];
    snprintf(cmd, sizeof cmd, "rgb %d %d %d%s\n", r, g, b, wait_for_it ? " sync" : "");
    if (helper_send(cmd, wait_for_it)) return;
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
    char cmd[64];
    snprintf(cmd, sizeof cmd, "cfg %d %d%s\n", mode, data, wait_for_it ? " sync" : "");
    if (helper_send(cmd, wait_for_it)) return;
    char m[16], v[16]; snprintf(m, sizeof m, "%d", mode); snprintf(v, sizeof v, "%d", data);
    char *a[] = {C.service_bin, "call", C.svc_name, "77", "i32", m, "i32", v, NULL};
    pid_t p = spawn(a, -1, wait_for_it);
    if (!wait_for_it) pending_pid = p;
}
static void light_bright_raw(int raw, int wait_for_it) {
    if (!C.dry) { totals.brights++; last_raw = raw; }
    light_config(5, raw, wait_for_it);
}

/* ---------- read the user's current light settings back from the lamp ---------- */
static struct { int valid, r, g, b, bright_raw, drive_mode, dim; } base;   /* dim: 1 off, 2 "dimmed in dark"; -1 unknown */
static int we_set_dim;      /* we changed the car's dim setting, so it goes back with the colour */

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

/* The user's own lamp setting, also on disk: "r g b brightness drive_mode dirty dim" (older files have no dim). dirty = we were driving
   the lamp when it was written. If that's still set at the next start, the car was switched off or we
   crashed mid-song, and the lamp is showing our colour, not theirs: the file is the truth, not the lamp. */
static const char *light_state_path = "/data/hmgapi/logs/light-vibes/user_light";
static int light_file_load(const char *path, int *s) {
    FILE *f = fopen(path, "r"); if (!f) return 0;
    int k = fscanf(f, "%d %d %d %d %d %d %d", &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6]);
    fclose(f);
    if (k < 6) return 0;
    if (k < 7 || s[6] < 1 || s[6] > 2) s[6] = -1;
    for (int i = 0; i < 3; i++) if (s[i] < 0 || s[i] > 65535) return 0;
    return s[3] >= 0 && s[3] <= 255 && s[4] >= 0 && s[4] <= 255;
}
static int saved_load(int *s) { return light_file_load(light_state_path, s); }
/* written whole to a temporary file and flushed before it replaces the old one: the car can lose power at any moment */
static int light_file_write(const char *path, const int *s) {
    char tmp[320], b[96]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int n = snprintf(b, sizeof b, "%d %d %d %d %d %d %d\n", s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    int ok = write(fd, b, n) == n && !fsync(fd);
    close(fd);
    if (!ok || rename(tmp, path)) return 0;
    char dir[320]; snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
    int d = open(dir, O_RDONLY); if (d >= 0) { fsync(d); close(d); }
    return 1;
}
/* the owner's colour, for the app's Pulse colour card (world-readable, like the units) */
static const char *public_colour_path = "/data/hmgapi/public/light-vibes/user_color";
static void publish_user_colour(void) {
    char dir[300], tmp[320]; snprintf(dir, sizeof dir, "%s", public_colour_path);
    char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdir(dir, 0755); chmod(dir, 0755); }
    snprintf(tmp, sizeof tmp, "%s.tmp", public_colour_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d\n", base.r, base.g, base.b, base.drive_mode);
    fclose(f);
    chmod(tmp, 0644);
    rename(tmp, public_colour_path);
}
/* flushed to the disk before we touch the lamp: the car can lose power at any moment */
static void saved_write(int dirty) {
    if (base.valid && !C.dry) publish_user_colour();
    int s[7];
    if (saved_load(s) && s[0] == base.r && s[1] == base.g && s[2] == base.b && s[3] == base.bright_raw && s[4] == base.drive_mode && s[5] == dirty && s[6] == base.dim) return;
    int v[7] = {base.r, base.g, base.b, base.bright_raw, base.drive_mode, dirty, base.dim};
    if (!light_file_write(light_state_path, v)) say("can't save the user's colour: %s", strerror(errno));
}
static void base_from(const int *s) {
    base.valid = 1; base.r = s[0]; base.g = s[1]; base.b = s[2]; base.bright_raw = s[3]; base.drive_mode = s[4]; base.dim = s[6];
}
/* the last status line in logcat. lamp_fresh says whether it's newer than the last one we saw: a lamp that has
   stopped answering otherwise looks fine, because the old line is still in the log. */
static char lamp_stamp[24];
static int lamp_fresh;
static int read_wait_ms = 400;
/* The status line is in the "system" log buffer. Reading only that one takes well under a second on the EV6; a plain
   logcat -d reads every buffer (3 MB) and took 7-8 s, which was the delay before the lights took over. If a head unit
   puts the line elsewhere, the full read is used from then on. */
static int lamp_log_all;
static int lamp_last_status_in(int all, int *out);
static int lamp_last_status(int *out) {
    if (!lamp_log_all) {
        if (lamp_last_status_in(0, out)) return 1;
        if (!lamp_last_status_in(1, out)) return 0;
        lamp_log_all = 1;
        say("the lamp's status isn't in the system log buffer here: reading all of them (slower)");
        return 1;
    }
    return lamp_last_status_in(1, out);
}
static int lamp_last_status_in(int all, int *out) {
    int fd[2]; if (pipe(fd)) return 0;
    fcntl(fd[0], F_SETFD, FD_CLOEXEC); fcntl(fd[1], F_SETFD, FD_CLOEXEC);
    char *lc_all[] = {C.logcat_bin, "-d", "-v", "time", "-s", "SystemService:D", NULL};
    char *lc_sys[] = {C.logcat_bin, "-b", "system", "-d", "-v", "time", "-s", "SystemService:D", NULL};
    char **lc = all ? lc_all : lc_sys;
    pid_t p = spawn(lc, fd[1], 0);
    close(fd[1]);
    FILE *f = fdopen(fd[0], "r");
    char line[512], stamp[24] = ""; int v[9], got = 0, last[9];
    while (f && fgets(line, sizeof line, f)) {
        int k = parse_status_line(line, v, 9);
        if (k >= 7) {
            memcpy(last, v, sizeof v); if (k < 9) last[8] = 0;
            if (k < 8) last[7] = 0;
            snprintf(stamp, sizeof stamp, "%.18s", line);
            got = 1;
        }
    }
    if (f) fclose(f);
    if (p > 0) waitpid(p, NULL, 0);
    lamp_fresh = got && strcmp(stamp, lamp_stamp) != 0;
    if (!got) return 0;
    snprintf(lamp_stamp, sizeof lamp_stamp, "%s", stamp);
    out[0] = (last[0] << 8) | last[1]; out[1] = (last[2] << 8) | last[3]; out[2] = (last[4] << 8) | last[5];
    out[3] = last[6]; out[4] = last[8]; out[5] = last[7];
    return 1;
}
/* r g b brightness drive_mode dim, as the lamp reports them */
static int read_lamp(int *out) {
    if (!helper_send("req sync\n", 1)) {
        char *req[] = {C.service_bin, "call", C.svc_name, "76", NULL};
        spawn(req, -1, 1);
    }
    usleep(read_wait_ms * 1000);
    return lamp_last_status(out);
}
static void snapshot_base(void) {
    if (C.dry) { base.valid = 0; return; }
    int s[7], lamp[7], have = saved_load(s);
    if (have && s[5]) {
        base_from(s);
        say("using the saved colour rgb %d %d %d, brightness raw %d: the lamp may still show one of ours", base.r, base.g, base.b, base.bright_raw);
    } else if (read_lamp(lamp)) {
        lamp[6] = lamp[5] >= 1 && lamp[5] <= 2 ? lamp[5] : -1;   /* the lamp reports dim in its 6th field */
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
/* Commands get no answer, so "colour #12" in the log only proves we sent it. A few seconds after taking the lamp over,
   a child process asks the lamp what it's showing, so the capture never waits on it. The result is one line in the log. */
#define CHECK_AFTER_MS 5000
enum { CHECK_NO_REPLY = 1, CHECK_COLOUR_STUCK = 2, CHECK_BRIGHT_STUCK = 4 };
static long long check_due;
static struct { int ok, no_reply, colour_stuck, bright_stuck; } chk;
static void check_finished(int status) {
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code < 0 || code > 7) return;
    if (code == 0) { chk.ok++; say("lamp check: ok"); return; }
    if (code & CHECK_NO_REPLY) { chk.no_reply++; say("lamp check: the lamp didn't answer"); }
    if (code & CHECK_COLOUR_STUCK) { chk.colour_stuck++; say("lamp check: colours were sent, but the lamp still reports the user's colour"); }
    if (code & CHECK_BRIGHT_STUCK) { chk.bright_stuck++; say("lamp check: brightness was sent, but the lamp still reports the user's level"); }
}
static void check_start(void) {
    int moved_colour = last_rgb[0] >= 0 && (last_rgb[0] != base.r || last_rgb[1] != base.g || last_rgb[2] != base.b);
    int moved_bright = last_raw >= 0 && last_raw != base.bright_raw;
    pid_t p = fork();
    if (p == 0) {
        int l[6], code = 0;
        if (!read_lamp(l) || !lamp_fresh) _exit(CHECK_NO_REPLY);
        if (moved_colour && l[0] == base.r && l[1] == base.g && l[2] == base.b) code |= CHECK_COLOUR_STUCK;
        if (moved_bright && l[3] == base.bright_raw) code |= CHECK_BRIGHT_STUCK;
        _exit(code);
    }
    if (p > 0) check_pid = p;
}
static void check_tick(void) {
    reap();
    if (!check_due || now_ms() < check_due || check_pid || !base.valid || C.dry) return;
    check_due = 0;
    check_start();
}
static void check_cancel(void) {     /* we're about to put the user's colour back, which would look like a stuck lamp */
    check_due = 0;
    if (check_pid) { kill(check_pid, SIGKILL); waitpid(check_pid, NULL, 0); check_pid = 0; }
}
/* Pulse in the drive-mode colour: the car's drive-mode-linked colour stays on and picks the colour (Eco, Normal, Sport...,
   also when the drive mode changes mid-song); we only pulse the brightness. Every other mode sets colours itself, which
   the lamp ignores while the link is on, so it's off for them. */
static int we_set_drive;       /* we changed the drive-mode link, so it goes back with the colour */
static int pulse_drive_colour(void) { return !strcmp(C.mode, "pulse") && C.pulse_color == -2; }
static void drive_link_sync(void) {
    if (C.dry || !C.override_drive_mode) return;
    int want = pulse_drive_colour() ? 2 : 1, now = we_set_drive ? (base.drive_mode == 2 ? 1 : 2) : (base.valid ? base.drive_mode : 0);
    if (want == now) return;
    light_config(9, want, 1);
    we_set_drive = !base.valid || want != base.drive_mode;
    say(want == 2 ? "drive-mode-linked colour on: the car picks pulse's colour by drive mode" : "drive-mode-linked colour switched off while we drive the lamp");
}
static void take_over_lamp(void) {
    last_rgb[0] = last_rgb[1] = last_rgb[2] = -1; last_raw = -1;
    check_due = now_ms() + CHECK_AFTER_MS;
    if (base.valid && !C.dry) saved_write(1);
    we_set_drive = 0;
    drive_link_sync();
}
static int music_dim_sent;     /* the dim setting last sent for a music mode, 0 none */
static void apply_base(void) {
    if (we_set_dim && base.dim >= 1) light_config(8, base.dim, 1);
    we_set_dim = 0;
    music_dim_sent = 0;
    /* The owner's drive-mode link setting, always sent: "off" goes before the colour (the car only keeps it off when a
       colour follows, as in its own Settings; seen 2026-11-08, off alone went back on), "on" after it. */
    if (base.drive_mode == 1) light_config(9, 1, 1);
    light_rgb(base.r, base.g, base.b, 1);
    light_bright_raw(base.bright_raw, 1);
    if (base.drive_mode == 2) light_config(9, 2, 1);
    we_set_drive = 0;
}
static void restore_base(void) {
    check_cancel();
    wait_pending();
    if (!base.valid) return;
    apply_base();
    if (!C.dry) saved_write(0);
    say("restored user colour %d %d %d, brightness raw %d, drive-mode colour %d", base.r, base.g, base.b, base.bright_raw, base.drive_mode);
}

/* The owner's ambient light settings as they were when Light Vibes was turned on (installed, turned on in HMG-API, or
   switched on in the app): colour, brightness, dimmed in dark and Link to Drive Mode. Put back when it's turned off,
   uninstalled or updated (HMG-API stops the service, with a few seconds' warning) or switched off in the app, whatever
   Light Vibes changed meanwhile. Only Pulse's "also set as car's color" changes it, as the owner's new colour. A record
   left by a power cut is kept, so it still holds the settings from before Light Vibes. Same format as user_light. */
static const char *original_path = "/data/hmgapi/logs/light-vibes/original_light";
static void original_record(void) {
    int s[7];
    if (C.dry || light_file_load(original_path, s)) return;
    if (!saved_load(s) || s[5]) { snapshot_base(); base.valid = 0; }
    if (!saved_load(s) || s[5]) { say("couldn't read the ambient light settings to keep for when Light Vibes is turned off"); return; }
    s[5] = 0;
    if (light_file_write(original_path, s))
        say("kept the ambient light settings to put back when Light Vibes is turned off: rgb %d %d %d, brightness raw %d, link to drive mode %d, dimmed in dark %d", s[0], s[1], s[2], s[3], s[4], s[6]);
}
static void original_restore(void) {
    int s[7];
    if (C.dry || !light_file_load(original_path, s)) return;
    wait_pending();
    base_from(s);
    we_set_dim = base.dim >= 1;
    apply_base();
    saved_write(0);
    base.valid = 0;
    unlink(original_path);
    say("put back the ambient light settings from before Light Vibes: rgb %d %d %d, brightness raw %d, link to drive mode %d, dimmed in dark %d", s[0], s[1], s[2], s[3], s[4], s[6]);
}
/* Pulse's "also set as car's color": the owner's new colour, with Link to Drive Mode off so it shows */
static void original_set_colour(int r, int g, int b) {
    int s[7];
    if (C.dry || !light_file_load(original_path, s)) return;
    s[0] = r; s[1] = g; s[2] = b; s[4] = 1; s[5] = 0;
    light_file_write(original_path, s);
}

/* Pulse with "set as the car's ambient colour": the picked colour becomes the owner's own colour, the one restored
   after the music and kept in user_light, as if they had picked it in Settings. Done when the setting changes. */
static int pulse_car_applied = -1;
/* Pulse in the drive-mode colour: that's the car's own setting too. The drive-mode link goes on at once, music or not,
   and stays on afterwards (it's what the owner chose), as the Settings app's drive-mode option does. */
static void pulse_drive_setting(int active) {
    if (pulse_car_applied == -2) return;
    wait_pending();
    if (active && base.valid) {
        /* sent here, not left to drive_link_sync: it compares with base.drive_mode, which this has just changed, and
           would think the lamp has it already (seen 2026-11-08: the link only came on when the music stopped) */
        light_config(9, 2, 1);
        base.drive_mode = 2;
        we_set_drive = 0;
        saved_write(1);
    } else if (!active) {
        light_config(9, 2, 1);                           /* instant, then the owner's saved setting follows */
        int sv[7];
        if (saved_load(sv) && !sv[5]) base_from(sv); else snapshot_base();
        if (base.valid) { base.drive_mode = 2; saved_write(0); }
        base.valid = 0;
    } else return;
    pulse_car_applied = -2;
    say("pulse: drive-mode-linked colour is now on in the car's settings");
}
/* A picked colour (not set as the car's own) still only shows with the drive-mode link off, so picking one turns the car's
   link off for good, as picking a colour in the car's Settings does. */
#define PULSE_LINK_OFF (-3)
static void pulse_drive_off(int active) {
    if (pulse_car_applied == PULSE_LINK_OFF) return;
    int sv[7], have = saved_load(sv) && !sv[5];
    /* always sent: the saved copy, or base, may say "off" while the lamp has it on (drive_link_sync turns it on for
       Pulse's drive-mode colour during music, or the owner turned it on since). The car ignores an "off" it has already. */
    {
        wait_pending();
        if (active && base.valid) {
            light_config(9, 1, 1);                         /* sent here, as in pulse_drive_setting */
            base.drive_mode = 1;
            we_set_drive = 0;
            saved_write(1);
        } else if (!active) {
            if (have) base_from(sv); else snapshot_base();
            light_config(9, 1, 1);
            if (base.valid) {
                light_rgb(base.r, base.g, base.b, 1);      /* a colour after it, or the car turns the link back on */
                base.drive_mode = 1;
                saved_write(0);
            }
            base.valid = 0;
        } else return;
        say("pulse: a colour was picked, so drive-mode-linked colour is now off in the car's settings");
    }
    pulse_car_applied = PULSE_LINK_OFF;
}
static void pulse_car_colour(int active) {
    if (!C.enabled) { pulse_car_applied = -1; return; }     /* switched off: the lamp is the owner's */
    if (!strcmp(C.mode, "pulse") && C.pulse_color == -2 && !C.dry) { pulse_drive_setting(active); return; }
    if (!strcmp(C.mode, "pulse") && C.pulse_color >= 0 && !C.pulse_set_car && !C.dry) { pulse_drive_off(active); return; }
    if (strcmp(C.mode, "pulse") || !C.pulse_set_car || C.pulse_color < 0 || C.dry) { pulse_car_applied = -1; return; }
    if (pulse_car_applied == C.pulse_color) return;
    int i = C.pulse_color, r = WHEEL_R[i], g = WHEEL_G[i], b = WHEEL_B[i];
    wait_pending();
    /* A custom colour only shows with drive-mode-linked colour off (the lamp keeps the drive-mode colour otherwise, and
       ignores the new one: seen 2026-11-08). Picking a colour in the car's Settings turns the link off too, so do the
       same: the owner's setting becomes "this colour, drive-mode colour off", kept after the music. */
    if (active && base.valid) {          /* during music: it's what we restore to, and what we pulse in already */
        light_config(9, 1, 1);           /* sent here: drive_link_sync would compare with the base just changed */
        base.r = r; base.g = g; base.b = b; base.drive_mode = 1;
        we_set_drive = 0;
        saved_write(1);
    } else if (!active) {
        /* instant: the colour goes out first. The rest of the owner's setting comes from the saved copy when there is a
           clean one (reading the lamp first took about a second); otherwise from the lamp, after. */
        int sv[7], have = saved_load(sv) && !sv[5];
        int drive = have ? sv[4] : 0;
        if (drive != 1) light_config(9, 1, 1);          /* a picked colour only shows with drive-mode colour off */
        light_rgb(r, g, b, 1);
        if (have) base_from(sv); else snapshot_base();
        if (base.valid) {
            if (base.drive_mode == 2) say("pulse: drive-mode-linked colour turned off, as the car's Settings does for a picked colour");
            base.r = r; base.g = g; base.b = b; base.drive_mode = 1;
            saved_write(0);
        }
        base.valid = 0;
    } else return;
    pulse_car_applied = i;
    original_set_colour(r, g, b);
    say("pulse: colour #%d (%d %d %d) is now the car's ambient colour", i, r, g, b);
}

/* Music modes: the car's "dimmed in dark" as chosen with the mode's Dimmed in Dark switch, back to the owner's after. */
static void music_dim(void) {
    int own = 1;     /* every music mode has its own Dimmed in Dark switch, as the other modes do */
    int want = own ? (C.gauge_dim ? 2 : 1) : (we_set_dim && base.dim >= 1 ? base.dim : 0);
    if (!want || want == music_dim_sent || (want == base.dim && !we_set_dim)) { if (want == base.dim) music_dim_sent = want; return; }
    wait_pending();
    light_config(8, want, 1);
    music_dim_sent = want;
    we_set_dim = want != base.dim;
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

/* ---------- FFT, real-time BPM & transient analysis ---------- */
#define NWIN 1024                /* right-aligned 21.3 ms Hann window zero-padded into 2048-pt FFT: halves spectral lag */
#define BPM_HIST 640             /* ~6.8-7.4 s multi-band onset history for comb autocorrelation */
typedef struct {
    float ring[NFFT]; int filled;
    float win[NWIN], re[NFFT], im[NFFT];
    double bp_hp_x1, bp_hp_y1;   /* causal time-domain beat bandpass: zero window lag on drum transients */
    double bp_lp1, bp_lp2;
    double beat_prev, beat_env, beat_fast, beat_slow, beat_ref_floor, beat_density, last_eb;
    int beat_armed;
    double bpm_prev_log[3];      /* log-energy history across kick (40-160), snare/body (160-900), snap/hat (900-6500) */
    int bpm_warmup;
    float flux_hist[BPM_HIST];
    int flux_pos, flux_filled, bpm_tick;
    double flux_mean, flux_lp, bpm, bpm_conf, quiet_ms;
    long long bpm_anchor_ms;
    double band_e[MAXBANDS], band_ref[MAXBANDS], band_score[MAXBANDS];
    long long last_band_sw;
    double mid_env, mid_ref;     /* duet/ember: vocal/harmonic midrange presence (250-4200 Hz) */
    double spark_prev, spark_fast, spark_slow; /* ember: snare/clap/treble spark transients (250-8000 Hz) */
    double cent_smooth, cent_ref;/* synesthesia: continuous octave spectral centroid */
    double loud_ref_db;
    double rep_ref;              /* repo curve: running loudness reference (linear, 16-bit units) */
    long long last_beat;
    int sel_band;
} Ana;

typedef struct {
    double db; int beat; int band;
    double beat_ratio, onset_ratio;
    double mid_norm, pitch_norm, spark_norm;
    double band_db[MAXBANDS];
} Frame;

static int tune_rem_sec(void);

static void ana_init(Ana *a) {
    memset(a, 0, sizeof *a);
    for (int i = 0; i < NWIN; i++) a->win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (NWIN - 1));
    a->sel_band = -1;
    a->last_band_sw = -100000;
    a->last_beat = -100000;
    a->bpm_anchor_ms = -100000;
    a->beat_armed = 1;
    a->cent_smooth = 2.8;
    a->cent_ref = 2.8;
    a->loud_ref_db = -30;
    a->rep_ref = 50;
}

/* Autocorrelation comb filter over ~7 s of 3-band pulse-smoothed log-spectral onset flux (65..174 BPM). */
static void ana_update_bpm(Ana *a) {
    int N = a->flux_filled;
    if (N < 260) return;
    float x[BPM_HIST];
    double mean = 0;
    for (int i = 0; i < N; i++) {
        x[i] = a->flux_hist[(a->flux_pos - N + i + BPM_HIST) % BPM_HIST];
        mean += x[i];
    }
    mean /= N;
    double r0 = 0;
    for (int i = 0; i < N; i++) { x[i] -= (float)mean; r0 += (double)x[i] * x[i]; }
    if (r0 / N < 8e-4) {
        a->bpm_conf *= 0.85;
        if (a->bpm_conf < 0.08) { a->bpm_conf = 0; a->bpm = 0; }
        return;
    }
    int lag_lo = (int)ceil(345.0 / BLOCK_MS);    /* ~174 BPM */
    int lag_hi = (int)floor(930.0 / BLOCK_MS);   /* ~64.5 BPM */
    if (lag_lo < 8) lag_lo = 8;
    if (lag_hi > N / 2 - 2) lag_hi = N / 2 - 2;
    if (lag_hi <= lag_lo + 2) return;
    int max_lag = 3 * lag_hi + 1;
    if (max_lag > N - 16) max_lag = N - 16;

    double r[BPM_HIST], score[BPM_HIST];
    memset(r, 0, sizeof(double) * (max_lag + 1));
    for (int L = lag_lo - 1; L <= max_lag; L++) {
        double s = 0; int cnt = N - L;
        for (int i = 0; i < cnt; i++) s += (double)x[i] * x[i + L];
        r[L] = (s / cnt) / (r0 / N);
    }
    int best_L = lag_lo;
    for (int L = lag_lo - 1; L <= lag_hi + 1 && L <= max_lag; L++) {
        double c2 = (2 * L <= max_lag && r[2 * L] > 0) ? 0.42 * r[2 * L] : 0.0;
        double c3 = (3 * L <= max_lag && r[3 * L] > 0) ? 0.18 * r[3 * L] : 0.0;
        double cand = 60000.0 / (L * BLOCK_MS);
        double lp = log2(cand / 120.0) / 0.58;
        double prior = 0.84 + 0.16 * exp(-0.5 * lp * lp);
        if (a->bpm_conf >= 0.60 && a->bpm > 50.0 && fabs(cand - a->bpm) < 5.5) prior *= 1.18;
        else if (a->bpm_conf >= 0.35 && a->bpm > 50.0 && fabs(cand - a->bpm) < 5.0) prior *= 1.08;
        score[L] = (r[L] + c2 + c3) * prior;
        if (L >= lag_lo && L <= lag_hi && score[L] > score[best_L]) best_L = L;
    }
    /* subharmonic guard: if 1:2 octave or 2:3 dotted-quarter lands in 96..160 BPM and is nearly as strong, prefer primary grid */
    static const int sh_num[2] = {1, 2}, sh_den[2] = {2, 3};
    for (int si = 0; si < 2; si++) {
        int sub_L = (int)lround((double)best_L * sh_num[si] / sh_den[si]);
        if (sub_L >= lag_lo && sub_L <= lag_hi) {
            double cand_fast = 60000.0 / (sub_L * BLOCK_MS);
            if (cand_fast >= 96.0 && cand_fast <= 160.0 && r[sub_L] >= 0.16 && score[sub_L] >= 0.84 * score[best_L]) {
                best_L = sub_L;
                break;
            }
        }
    }
    double raw_pk = r[best_L] + ((2 * best_L <= max_lag && r[2 * best_L] > 0) ? 0.35 * r[2 * best_L] : 0.0);
    if (r[best_L] < 0.16 || raw_pk < 0.22) {
        a->bpm_conf *= 0.82;
        if (a->bpm_conf < 0.08) { a->bpm_conf = 0; a->bpm = 0; }
        return;
    }
    double delta = 0.0;
    if (best_L > lag_lo && best_L < lag_hi) {
        double y1 = score[best_L - 1], y2 = score[best_L], y3 = score[best_L + 1];
        double denom = y1 - 2.0 * y2 + y3;
        if (fabs(denom) > 1e-9) delta = 0.5 * (y1 - y3) / denom;
        if (delta < -0.5) delta = -0.5;
        if (delta > 0.5) delta = 0.5;
    }
    double est_bpm = 60000.0 / ((best_L + delta) * BLOCK_MS);
    double conf = (raw_pk - 0.14) / 0.40;
    if (conf < 0) conf = 0;
    if (conf > 1) conf = 1;
    if (a->bpm <= 0 || a->bpm_conf < 0.20 || fabs(est_bpm - a->bpm) > 12.0) a->bpm = est_bpm;
    else a->bpm += 0.35 * (est_bpm - a->bpm);
    a->bpm_conf = conf > a->bpm_conf ? 0.55 * conf + 0.45 * a->bpm_conf : 0.25 * conf + 0.75 * a->bpm_conf;
}

static double bpm_weight(const Ana *a) {
    if (!C.auto_bpm || a->bpm < 55.0 || a->bpm_conf < 0.28) return 0.0;
    double w = (a->bpm_conf - 0.28) / 0.35;
    return w > 1.0 ? 1.0 : w;
}
static int eff_min_beat_ms(const Ana *a) {
    double w = bpm_weight(a);
    if (w <= 0.0) return C.min_beat_ms;
    double T = 60000.0 / a->bpm;
    double tgt = C.min_beat_ms * (T / 500.0);
    if (tgt < 0.56 * T) tgt = 0.56 * T;   /* suppress weak eighth-note double-triggers between beats */
    if (tgt < 185.0) tgt = 185.0;
    if (tgt > 400.0) tgt = 400.0;
    return (int)lround((1.0 - w) * C.min_beat_ms + w * tgt);
}
static double eff_ref_tau_ms(const Ana *a) {
    double w = bpm_weight(a);
    if (w <= 0.0) return 160.0;
    double tgt = 160.0 * (60000.0 / a->bpm / 500.0);
    if (tgt < 105.0) tgt = 105.0;
    if (tgt > 240.0) tgt = 240.0;
    return (1.0 - w) * 160.0 + w * tgt;
}
static int eff_flash_ms(const Ana *a) {
    if (C.flash_ms <= 0) return 0;
    double w = bpm_weight(a);
    if (w <= 0.0) return C.flash_ms;
    double T = 60000.0 / a->bpm;
    double tgt = C.flash_ms * (T / 500.0);
    if (tgt > 0.34 * T) tgt = 0.34 * T;   /* leave >=66% dark valley between quarter-note flashes */
    if (tgt < 105.0) tgt = 105.0;         /* car LIN lamp fade-up ramp needs >=105 ms to reach 100% PWM output */
    if (tgt > 175.0) tgt = 175.0;
    return (int)lround((1.0 - w) * C.flash_ms + w * tgt);
}
#define PULSE_HOLD_MS 105
static int eff_pulse_hold_ms(const Ana *a) {
    double w = bpm_weight(a);
    if (w <= 0.0) return PULSE_HOLD_MS;
    double tgt = PULSE_HOLD_MS * (60000.0 / a->bpm / 500.0);
    if (tgt < 85.0) tgt = 85.0;
    if (tgt > 155.0) tgt = 155.0;
    return (int)lround((1.0 - w) * PULSE_HOLD_MS + w * tgt);
}
static double eff_pulse_decay_frac(const Ana *a) {
    double d0 = C.pulse_decay / 100.0;
    if (d0 <= 0.001) return 0.001;
    double w = bpm_weight(a);
    if (w <= 0.0) return d0;
    double rem = 60000.0 / a->bpm - eff_pulse_hold_ms(a);
    if (rem < 180.0) rem = 180.0;
    if (rem > 700.0) rem = 700.0;
    double tgt = pow(d0, (500.0 - PULSE_HOLD_MS) / rem);
    if (tgt < 0.05) tgt = 0.05;
    if (tgt > 0.92) tgt = 0.92;
    return (1.0 - w) * d0 + w * tgt;
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
    return e / ((double)NWIN * NWIN);
}
static double db10(double e) { return e > 1e-14 ? 10 * log10(e) : -140; }
static double clamp01(double x) { return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; }

static Frame ana_block(Ana *a, const short *buf, int frames, long long t) {
    Frame f; memset(&f, 0, sizeof f); f.band = a->sel_band;
    /* slide the mono ring buffer and filter newest samples directly for zero-lag beat onset */
    memmove(a->ring, a->ring + frames, sizeof(float) * (NFFT - frames));
    double dt = 1.0 / rate;
    double rc_hp = 1.0 / (2.0 * M_PI * C.beat_low_hz);
    double a_hp = rc_hp / (rc_hp + dt);
    double wc_lp = 2.0 * M_PI * (C.beat_high_hz / 0.6436) * dt;
    double a_lp = wc_lp / (1.0 + wc_lp);
    double et = 0, eb_sum = 0;
    for (int i = 0; i < frames; i++) {
        float x = (buf[i * CH] + buf[i * CH + 1]) / 65536.0f;
        a->ring[NFFT - frames + i] = x;
        et += (double)x * x;
        double hp = a_hp * (a->bp_hp_y1 + (double)x - a->bp_hp_x1);
        a->bp_hp_x1 = x; a->bp_hp_y1 = hp;
        a->bp_lp1 += a_lp * (hp - a->bp_lp1);
        a->bp_lp2 += a_lp * (a->bp_lp1 - a->bp_lp2);
        eb_sum += a->bp_lp2 * a->bp_lp2;
    }
    if (a->filled < NFFT) a->filled += frames;
    double rms = sqrt(et / frames);
    f.db = rms > 1e-9 ? 20 * log10(rms) : -120;
    memset(a->re, 0, sizeof(float) * (NFFT - NWIN));
    for (int i = 0; i < NWIN; i++) a->re[NFFT - NWIN + i] = a->ring[NFFT - NWIN + i] * a->win[i];
    memset(a->im, 0, sizeof a->im);
    fft(a->re, a->im);

    /* beat: zero-lag beat-band energy + onset flux vs spike-clamped running baseline + refractory tail rejection */
    double eb = eb_sum / frames;
    a->last_eb = eb;
    double env_prev = a->beat_env;
    if (a->beat_env <= 0) a->beat_env = eb;
    else a->beat_env += (BLOCK_MS / (BLOCK_MS + 35.0)) * (eb - a->beat_env);
    double flux = eb > a->beat_prev ? eb - a->beat_prev : 0.0;
    a->beat_prev = eb;

    /* 3-band Hann-windowed log-spectral onset flux (kick 40-160, snare/body 160-900, snap/hat 900-6500) + ~20 ms pulse smooth */
    double bpm_es[3] = { band_energy(a->re, a->im, 40, 160), band_energy(a->re, a->im, 160, 900), band_energy(a->re, a->im, 900, 6500) };
    static const double bpm_wts[3] = { 0.42, 0.33, 0.25 }, bpm_floors[3] = { 1e-7, 1e-8, 1e-9 };
    double raw_on = 0;
    for (int k = 0; k < 3; k++) {
        double lg = log(bpm_es[k] + bpm_floors[k]);
        double dl = a->bpm_warmup >= 3 ? lg - a->bpm_prev_log[k] : 0.0;
        a->bpm_prev_log[k] = lg;
        if (dl > 0.06) raw_on += bpm_wts[k] * (dl - 0.06);   /* sidelobe-immune per-band deadzone */
    }
    if (a->bpm_warmup < 4) a->bpm_warmup++;
    if (f.db > C.silence_db) {
        a->quiet_ms = 0;
        a->flux_mean += (BLOCK_MS / (BLOCK_MS + 220.0)) * (raw_on - a->flux_mean);
        double on = raw_on > a->flux_mean ? raw_on - a->flux_mean : 0.0;
        a->flux_lp += (BLOCK_MS / (BLOCK_MS + 15.0)) * (on - a->flux_lp);   /* ~20 ms pulse kernel tolerates human micro-timing */
        a->flux_hist[a->flux_pos] = (float)a->flux_lp;
        a->flux_pos = (a->flux_pos + 1) % BPM_HIST;
        if (a->flux_filled < BPM_HIST) a->flux_filled++;
        if (++a->bpm_tick >= 24 && a->flux_filled >= 260) { a->bpm_tick = 0; ana_update_bpm(a); }
    } else {
        a->quiet_ms += BLOCK_MS;
        if (a->quiet_ms >= 450.0) {   /* CarPlay track-skip gap: start the next song with a clean tempo slate */
            a->flux_pos = 0; a->flux_filled = 0; a->bpm_tick = 0;
            a->flux_mean = 0; a->flux_lp = 0; a->bpm = 0; a->bpm_conf = 0;
        } else {
            a->flux_lp *= 0.5;
            a->flux_hist[a->flux_pos] = 0.0f;
            a->flux_pos = (a->flux_pos + 1) % BPM_HIST;
            if (a->flux_filled < BPM_HIST) a->flux_filled++;
            a->bpm_conf *= exp(-BLOCK_MS / 2500.0);
            if (a->bpm_conf < 0.05) { a->bpm_conf = 0; a->bpm = 0; }
        }
    }

    double accent = eb + 0.65 * flux;   /* sharp percussive attacks punch cleanly above sustained sub-bass */
    a->beat_fast = accent > a->beat_fast ? accent : 0.45 * a->beat_fast + 0.55 * eb;
    a->beat_density *= exp(-BLOCK_MS / 2400.0);
    if (t - a->last_beat < 45) {
        if (0.50 * eb > a->beat_ref_floor) a->beat_ref_floor = 0.50 * eb;
    } else {
        a->beat_ref_floor *= exp(-BLOCK_MS / eff_ref_tau_ms(a));
    }
    double alpha_slow = BLOCK_MS / (double)C.beat_avg_ms;
    if (t < 1500) alpha_slow = alpha_slow * 4 > 0.5 ? 0.5 : alpha_slow * 4;   /* settle fast at start */
    if (a->beat_slow <= 0) a->beat_slow = eb > 1e-12 ? eb : 1e-12;
    if (eb <= 1.12 * a->beat_slow || (env_prev > 0 && eb > 1.70 * env_prev && flux > 0.38 * eb)) a->beat_armed = 1;
    int rising = env_prev <= 1e-14 || eb > 1.18 * env_prev;
    f.beat_ratio = a->beat_slow > 1e-14 ? a->beat_fast / a->beat_slow : 0;
    f.onset_ratio = (a->beat_slow > 1e-14 && eb > env_prev && accent > a->beat_ref_floor) ? (eb + 1.2 * flux) / a->beat_slow : 0;

    double eff_sens = C.sensitivity;
    double bw = bpm_weight(a);
    if (bw > 0.0 && a->bpm_anchor_ms >= 0 && t > a->bpm_anchor_ms) {
        double T = 60000.0 / a->bpm;
        double cyc = fmod((double)(t - a->bpm_anchor_ms), T) / T;
        eff_sens = 100.0 + (C.sensitivity - 100.0) * (1.0 - 0.15 * bw * cos(2.0 * M_PI * cyc));
        if (eff_sens < 105.0) eff_sens = 105.0;
    }
    /* adaptive beat-density governor: keep firing rate near the primary beat grid so dark valleys stay crisp */
    double tgt_bps = (bw > 0.30 && a->bpm > 55.0) ? (a->bpm / 60.0) * 1.08 : 2.05;
    double excess_bps = (a->beat_density / 2.4) - tgt_bps;
    if (excess_bps > 0.0) eff_sens *= (1.0 + 0.50 * excess_bps);

    if (f.db > C.silence_db && a->beat_armed && rising && accent > a->beat_ref_floor
        && f.beat_ratio * 100 > eff_sens && t - a->last_beat >= eff_min_beat_ms(a)) {
        f.beat = 1; a->last_beat = t; a->bpm_anchor_ms = t; a->beat_armed = 0;
        a->beat_density += 1.0;
        if (0.50 * eb > a->beat_ref_floor) a->beat_ref_floor = 0.50 * eb;
    }
    double eb_capped = eb > 4.0 * a->beat_slow ? 4.0 * a->beat_slow : eb;
    a->beat_slow += alpha_slow * (eb_capped - a->beat_slow);

    /* duet midrange/vocal presence (250-4200 Hz) */
    double e_mid = band_energy(a->re, a->im, 250, 4200);
    a->mid_env += (BLOCK_MS / (BLOCK_MS + 180.0)) * (e_mid - a->mid_env);
    if (f.db > C.silence_db) {
        if (a->mid_ref <= 0) a->mid_ref = e_mid > 1e-11 ? e_mid : 1e-11;
        else a->mid_ref += (BLOCK_MS / 2600.0) * ((e_mid > 3.0 * a->mid_ref ? 3.0 * a->mid_ref : e_mid) - a->mid_ref);
    }
    f.mid_norm = clamp01(0.45 + 0.32 * log2((a->mid_env + 1e-12) / (a->mid_ref + 1e-12)));

    /* synesthesia continuous log-frequency spectral centroid (55..8000 Hz, pink-tilt compensated) */
    {
        int k_lo = (int)ceil(55.0 / BIN_HZ), k_hi = (int)floor(8000.0 / BIN_HZ);
        if (k_lo < 1) k_lo = 1;
        if (k_hi > NFFT / 2 - 1) k_hi = NFFT / 2 - 1;
        double w_sum = 0, oct_sum = 0;
        for (int k = k_lo; k <= k_hi; k++) {
            double fk = k * BIN_HZ;
            double ek = ((double)a->re[k] * a->re[k] + (double)a->im[k] * a->im[k]) * pow(fk / 250.0, 0.75);
            w_sum += ek;
            oct_sum += ek * log2(fk / 55.0);
        }
        if (f.db > C.silence_db && w_sum > 1e-9) {
            double oct = oct_sum / w_sum;
            double sm_ms = C.band_smoothing_ms > 20 ? C.band_smoothing_ms : 115;
            a->cent_smooth += (BLOCK_MS / (BLOCK_MS + sm_ms)) * (oct - a->cent_smooth);
            a->cent_ref += (BLOCK_MS / 6000.0) * (oct - a->cent_ref);
            if (a->cent_ref < 1.8) a->cent_ref = 1.8;
            if (a->cent_ref > 4.2) a->cent_ref = 4.2;
        }
        f.pitch_norm = clamp01(0.5 + (a->cent_smooth - a->cent_ref) / 2.6);
    }

    /* ember snare/clap/treble spark transients (250-8000 Hz) so fire dances between kick drums */
    {
        double e_spark = e_mid + 1.8 * band_energy(a->re, a->im, 2200, 8000);
        double sp_flux = e_spark > a->spark_prev ? e_spark - a->spark_prev : 0.0;
        a->spark_prev = e_spark;
        double sp_acc = 0.30 * e_spark + 1.15 * sp_flux;
        a->spark_fast = sp_acc > a->spark_fast ? sp_acc : 0.55 * a->spark_fast + 0.45 * (0.30 * e_spark);
        if (f.db > C.silence_db) {
            double sp_base = 0.35 * e_spark;
            if (a->spark_slow <= 0) a->spark_slow = sp_base > 1e-12 ? sp_base : 1e-12;
            else a->spark_slow += (BLOCK_MS / 1600.0) * ((sp_base > 3.0 * a->spark_slow ? 3.0 * a->spark_slow : sp_base) - a->spark_slow);
            double sr = a->spark_slow > 1e-14 ? a->spark_fast / a->spark_slow : 0.0;
            f.spark_norm = clamp01((sr - 1.15) / 1.85);
        }
    }

    /* spectrum bands (smoothed, boosted, optionally auto-adapted per band); 115 ms dwell prevents LIN mid-fade mud */
    double alpha_b = C.band_smoothing_ms > 0 ? BLOCK_MS / (BLOCK_MS + C.band_smoothing_ms) : 1.0;
    double alpha_ref = BLOCK_MS / 2500.0;
    if (t < 1500) alpha_ref = alpha_ref * 4 > 0.4 ? 0.4 : alpha_ref * 4;
    double mean_ref = 0;
    for (int i = 0; i < C.nbands; i++) {
        double e = band_energy(a->re, a->im, C.bands[i].lo, C.bands[i].hi) * C.bands[i].gain;
        a->band_e[i] += alpha_b * (e - a->band_e[i]);
        if (f.db > C.silence_db) {
            if (a->band_ref[i] <= 0) a->band_ref[i] = e > 1e-12 ? e : 1e-12;
            else {
                double ec = e > 2.5 * a->band_ref[i] ? 2.5 * a->band_ref[i] : e;
                a->band_ref[i] += alpha_ref * (ec - a->band_ref[i]);
            }
        }
        mean_ref += a->band_ref[i];
    }
    if (C.nbands > 0) mean_ref /= C.nbands;
    if (mean_ref < 1e-12) mean_ref = 1e-12;
    int best = 0;
    for (int i = 0; i < C.nbands; i++) {
        double sc = a->band_e[i];
        if (C.spectrum_dynamic) {
            double r = a->band_ref[i] > 0.05 * mean_ref ? a->band_ref[i] : 0.05 * mean_ref;
            sc = a->band_e[i] * pow(mean_ref / r, 0.80);   /* evens out spectral tilt while keeping user boost bias */
        }
        a->band_score[i] = sc;
        f.band_db[i] = db10(sc);
        if (sc > a->band_score[best]) best = i;
    }
    if (a->sel_band < 0 || a->sel_band >= C.nbands) {
        a->sel_band = best;
        a->last_band_sw = t;
    } else if (best != a->sel_band) {
        double need = 1.0 + C.switch_margin / 100.0;
        int dwell_ok = (t - a->last_band_sw >= 115) || (f.beat && a->band_score[best] > a->band_score[a->sel_band] * need * 1.35);
        if (dwell_ok && a->band_score[best] > a->band_score[a->sel_band] * need) {
            a->sel_band = best;
            a->last_band_sw = t;
        }
    }
    f.band = a->sel_band;

    if (f.db > C.silence_db) a->loud_ref_db += (BLOCK_MS / 1800.0) * (f.db - a->loud_ref_db);
    {   /* repo: reference += 0.025 * (max(rms, 20) - reference) per 128 ms window */
        double r16 = rms * 32768.0, alpha = 0.025 * BLOCK_MS / 128.0;
        a->rep_ref += alpha * ((r16 > 20 ? r16 : 20) - a->rep_ref);
    }
    return f;
}

/* loudness -> UI brightness level (S-curve contrast with small hysteresis so single-step moves stay smooth) */
static int loud_level(Ana *a, int cur_level, double db) {
    if (db <= C.silence_db) return C.bright_min;
    if (!strcmp(C.bright_curve, "repo")) {     /* level = round(4 + 3*log2(rms / reference)) */
        double r16 = pow(10, db / 20) * 32768.0, ref = a->rep_ref > 20 ? a->rep_ref : 20;
        int lvl = (int)lround(4 + 3 * log2(r16 / ref));
        return clampi(lvl, C.bright_min, C.bright_max);
    }
    double x = 0.5 + (db - a->loud_ref_db) / (2.0 * C.loudness_range_db);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    x = x * x * (3.0 - 2.0 * x);
    double target = C.bright_min + x * (C.bright_max - C.bright_min);
    if (cur_level >= C.bright_min && cur_level <= C.bright_max && fabs(target - cur_level) < 0.65
        && target > C.bright_min + 0.05 && target < C.bright_max - 0.05) {
        return cur_level;
    }
    return clampi((int)lround(target), C.bright_min, C.bright_max);
}

/* ---------- light driver ---------- */
typedef struct {
    int idx;
    int sent_idx, sent_level;   /* sent_idx holds the 27-bit packed RGB last sent */
    int want_r, want_g, want_b, want_level;
    long long last_color, last_bright;
    double pulse;
    long long flash_until, pulse_hold_until, bar_swell_until, last_drift_ms;
    int beat_count, drift_step, was_flashing;
} Drv;

static int gcd_int(int a, int b) { while (b) { int r = a % b; a = b; b = r; } return a; }

static int wheel_pale(int i) {   /* the pastel/white stretch of the wheel (used by Speed/Power/Efficiency fade walk) */
    int r = WHEEL_R[i] & 0xff, g = WHEEL_G[i] & 0xff, b = WHEEL_B[i] & 0xff;
    int m = r < g ? r : g;
    if (b < m) m = b;
    return m > 150;
}
static int wheel_next(int i, int step) {
    if (step < 1) step = 1;
    if (C.skip_white && vivid_n > 2) {
        int pos = 0;
        for (int k = 0; k < vivid_n; k++) if (vivid_ring[k] == i) { pos = k; break; }
        int rstep = (int)lround(step * (double)vivid_n / 64.0);
        if (rstep < 1) rstep = 1;
        if (rstep >= vivid_n) rstep = vivid_n - 1;
        if (step >= 5) {   /* keep wide beat steps coprime to vivid_n so Beat visits every vivid swatch */
            for (int d = 0; d < vivid_n / 2; d++) {
                if (rstep + d < vivid_n && gcd_int(rstep + d, vivid_n) == 1) { rstep += d; break; }
                if (rstep - d >= 1 && gcd_int(rstep - d, vivid_n) == 1) { rstep -= d; break; }
            }
        }
        return vivid_ring[(pos + rstep) % vivid_n];
    }
    return (i + step) % wheel_n;
}

static double pulse_env_step(Drv *d, const Ana *a, Frame f, long long t) {
    double keep = pow(eff_pulse_decay_frac(a), BLOCK_MS / 100.0);
    if (t >= d->pulse_hold_until) d->pulse *= keep;
    double floor_r = 1.20, ceil_r = C.sensitivity / 100.0;
    if (ceil_r < floor_r + 0.15) ceil_r = floor_r + 0.15;
    double hit = f.onset_ratio > floor_r ? (f.onset_ratio - floor_r) / (ceil_r - floor_r) : 0;
    if (hit > 1) hit = 1;
    hit = hit * hit;   /* ignore steady bass swell; only percussive jumps push sub-beat hits */
    if (hit > d->pulse) d->pulse = hit;
    if (f.beat) { d->pulse = 1.0; d->pulse_hold_until = t + eff_pulse_hold_ms(a); }
    return d->pulse;
}

/* 10 perceptually balanced vivid rainbow stops around 360 deg (hits all 6 vivid Hyundai 0x01xx stock colours + rich custom hues) */
static const double FULL_WHEEL_HUES[10] = {
    358.0,  38.0,  88.0, 178.0, 209.0, 246.0, 268.0, 287.0, 314.0, 336.0
};

static double hue_lerp(double a, double b, double x) {
    double d = fmod(b - a + 540.0, 360.0) - 180.0;
    double h = fmod(a + d * x, 360.0);
    return h < 0.0 ? h + 360.0 : h;
}
static int ember_wheel_at(int theme, double heat) {
    /* 4-stop thermal cooling curves spaced across distinct vivid wheel zones so cooling sweeps visibly */
    static const double stops[3][4] = {
        {268.0, 322.0,   6.0,  38.0},   /* 0 Fire:   Dream Purple -> Neon Rose -> Fiery Red -> Marigold Gold */
        {246.0, 287.0, 209.0, 175.0},   /* 1 Plasma: Royal Indigo -> Vivid Orchid -> Ocean Blue -> Electric Cyan */
        {246.0, 178.0,  88.0,  38.0}    /* 2 Aurora: Royal Indigo -> Mindful Teal -> Emerald Green -> Marigold Gold */
    };
    const double *s = stops[clampi(theme, 0, 2)];
    double h;
    if (heat < 0.28) h = hue_lerp(s[0], s[1], heat / 0.28);
    else if (heat < 0.56) h = hue_lerp(s[1], s[2], (heat - 0.28) / 0.28);
    else h = hue_lerp(s[2], s[3], (heat - 0.56) / 0.44);
    return vivid_at_hue(h);
}
static int drift_wheel_at(int theme, int step) {
    static const double themes[4][5] = {
        { 92.0, 178.0, 209.0, 268.0, 145.0},   /* 0 Forest & Aurora: Emerald -> Teal -> Ocean -> Dream Purple -> Jade */
        {186.0, 236.0, 290.0, 330.0, 356.0},   /* 1 Cyberpunk:       Cyan -> Royal Blue -> Orchid -> Hot Pink -> Crimson */
        { 40.0,  16.0, 356.0, 316.0, 270.0},   /* 2 Sunset:          Gold -> Flame -> Crimson -> Magenta -> Violet */
        {176.0, 192.0, 209.0, 232.0, 256.0}    /* 3 Ocean Deep:      Teal -> Cyan -> Ocean Blue -> Royal Blue -> Indigo */
    };
    if (theme >= 4) return vivid_at_hue(FULL_WHEEL_HUES[step % 10]);
    return vivid_at_hue(themes[clampi(theme, 0, 3)][step % 5]);
}

static int power_wheel_index(double kw);
static int speed_wheel_index(double kmh);
static int read_power(double *kw);
static int read_cached_speed(double *kmh);

#define BEAT_GUARD_MS 25
static void drive(Drv *d, Ana *a, Frame f, long long t) {
    int want_idx = d->idx, want_level = C.bright_min, wr = 0, wg = 0, wb = 0;
    int fms = eff_flash_ms(a);
    int use_flash = flash_enabled() && fms > 0;
    int use_bpulse = beat_pulse_enabled();
    int bp_level = C.bright_min;
    if (use_bpulse) {
        double bp = pulse_env_step(d, a, f, t);
        bp_level = C.bright_min + (int)lround((C.bright_max - C.bright_min) * (bp * bp));
    }
    if (f.beat && (use_flash || !strcmp(C.mode, "duet")))
        d->flash_until = t + (fms > 0 ? fms : 120);
    int flashing = t < d->flash_until;
    int flash_ended = d->was_flashing && !flashing;
    d->was_flashing = flashing;

    if (!strcmp(C.mode, "spectrum")) {
        if (f.band >= 0) { wr = C.bands[f.band].r; wg = C.bands[f.band].g; wb = C.bands[f.band].b; want_idx = 1000 + f.band; }
        want_level = C.bright_fixed ? C.bright_max : use_bpulse ? bp_level : (use_flash && flashing) ? C.bright_max : use_flash ? C.bright_min : loud_level(a, d->sent_level, f.db);
    } else if (!strcmp(C.mode, "pulse")) {
        double p = pulse_env_step(d, a, f, t);
        want_idx = C.pulse_color;   /* -1 keeps the user's colour */
        want_level = C.bright_min + (int)lround((C.bright_max - C.bright_min) * (p * p));
    } else if (!strcmp(C.mode, "duet")) {
        if (C.duet_beats > 0) {
            if (f.beat) {
                d->beat_count++;
                d->last_drift_ms = t;
                if (d->beat_count % C.duet_beats == 0) d->drift_step++;
            } else if (t - d->last_drift_ms >= 3800) {
                d->drift_step++;
                d->last_drift_ms = t;
            }
        }
        int step = C.duet_beats > 0 ? d->drift_step : 0;
        int bed = C.duet_bed < 0 ? vivid_at_hue(FULL_WHEEL_HUES[(3 + step) % 10])
                : step == 0 ? C.duet_bed : vivid_at_hue(wheel_hue(C.duet_bed) + step * 36.0);
        int acc = C.duet_accent < 0 ? vivid_at_hue(wheel_hue(bed) + 180.0)
                : (step == 0 || C.duet_bed < 0) ? C.duet_accent : vivid_at_hue(wheel_hue(C.duet_accent) + step * 36.0);
        if (flashing) {
            want_idx = acc;
            want_level = C.bright_max;
        } else {
            want_idx = bed;
            int ceil_b = C.bright_min + (int)lround(0.50 * (C.bright_max - C.bright_min));
            if (ceil_b < C.bright_min) ceil_b = C.bright_min;
            want_level = C.bright_min + (int)lround((ceil_b - C.bright_min) * f.mid_norm);
        }
        d->idx = want_idx;
    } else if (!strcmp(C.mode, "synesthesia")) {
        double hue = fmod(C.syn_shift + f.pitch_norm * 285.0, 360.0);
        want_idx = vivid_at_hue(hue);
        d->idx = want_idx;
        want_level = C.bright_fixed ? C.bright_max : use_bpulse ? bp_level : (use_flash && flashing) ? C.bright_max : use_flash ? C.bright_min : loud_level(a, d->sent_level, f.db);
    } else if (!strcmp(C.mode, "ember")) {
        (void)pulse_env_step(d, a, f, t);
        double spark = 0.74 * f.spark_norm * f.spark_norm + 0.20 * f.mid_norm;
        if (spark > 0.78) spark = 0.78;
        if (f.db <= C.silence_db) spark = 0.0;
        if (t >= d->pulse_hold_until && 0.74 * f.spark_norm * f.spark_norm > d->pulse)
            d->pulse = 0.74 * f.spark_norm * f.spark_norm;   /* snare/clap transients re-ignite cooling embers */
        double heat = d->pulse > spark ? d->pulse : spark;
        want_idx = ember_wheel_at(C.ember_theme, heat);
        d->idx = want_idx;
        want_level = C.bright_min + (int)lround((C.bright_max - C.bright_min) * pow(heat, 1.50));
    } else if (!strcmp(C.mode, "drift")) {
        if (f.beat) {
            d->beat_count++;
            d->last_drift_ms = t;
            if (d->beat_count % C.drift_beats == 0) {
                d->drift_step++;
                d->bar_swell_until = t + 260;
            }
        } else if (t - d->last_drift_ms >= 3800) {
            d->drift_step++;
            d->last_drift_ms = t;
        }
        want_idx = drift_wheel_at(C.drift_theme, d->drift_step);
        d->idx = want_idx;
        if (C.bright_fixed) want_level = C.bright_max;
        else if (use_bpulse) want_level = bp_level;
        else if (use_flash) want_level = flashing ? C.bright_max : C.bright_min;
        else {
            want_level = loud_level(a, d->sent_level, f.db);
            if (t < d->bar_swell_until && want_level < C.bright_max) want_level++;
        }
    } else if (!strcmp(C.mode, "nightdrive")) {
        double p = pulse_env_step(d, a, f, t);
        if (!strcmp(C.nd_source, "speed")) {
            double kmh = 0; read_cached_speed(&kmh);
            want_idx = speed_wheel_index(kmh);
        } else {
            double kw = 0; read_power(&kw);
            want_idx = power_wheel_index(kw);
        }
        d->idx = want_idx;
        want_level = C.bright_min + (int)lround((C.bright_max - C.bright_min) * (p * p));
    } else {   /* beat */
        if (f.beat) d->idx = wheel_next(d->idx, C.beat_step);
        want_idx = d->idx;
        want_level = C.bright_fixed ? C.bright_max : use_bpulse ? bp_level : (use_flash && flashing) ? C.bright_max : use_flash ? C.bright_min : loud_level(a, d->sent_level, f.db);
    }
    if (want_idx >= 0 && want_idx < wheel_n) { wr = WHEEL_R[want_idx]; wg = WHEEL_G[want_idx]; wb = WHEEL_B[want_idx]; }
    d->want_r = wr; d->want_g = wg; d->want_b = wb; d->want_level = want_level;

    reap();
    if (pending_pid) return;   /* a service call (no helper) still in flight: one at a time */

    /* on a beat peak or trough drop, send down to BEAT_GUARD_MS so peaks and dark valleys aren't clipped */
    int pulse_like = !strcmp(C.mode, "pulse") || !strcmp(C.mode, "ember") || !strcmp(C.mode, "nightdrive") || use_bpulse;
    int fast_col_mode = !strcmp(C.mode, "duet");
    int beat_iv = C.min_interval_ms < BEAT_GUARD_MS ? C.min_interval_ms : BEAT_GUARD_MS;
    int base_col_iv = C.min_interval_ms;
    if (!strcmp(C.mode, "synesthesia") && base_col_iv < 110) base_col_iv = 110;
    int col_iv = (f.beat || (fast_col_mode && flash_ended)) ? beat_iv : base_col_iv;
    int key = want_idx < 0 ? -1 : ((wr & 0x1ff) << 18) | ((wg & 0x1ff) << 9) | (wb & 0x1ff);
    if (key >= 0 && key != d->sent_idx && t - d->last_color >= col_iv) {
        light_rgb(wr, wg, wb, 0);
        d->sent_idx = key; d->last_color = t;
        if (C.debug) {
            if (want_idx >= 1000) say("colour band %d-%dHz (%d,%d,%d)", C.bands[want_idx - 1000].lo, C.bands[want_idx - 1000].hi, wr, wg, wb);
            else say("colour #%d (%d,%d,%d)%s", want_idx, wr, wg, wb, f.beat ? " [beat]" : "");
        }
        if (pending_pid) return;   /* through service call: the brightness waits its turn */
    }
    int flash_like = use_flash || !strcmp(C.mode, "duet");
    int snap_bright = (f.beat && (flashing || pulse_like)) || (flash_like && flash_ended) || ((flash_like || pulse_like) && want_level == C.bright_min);
    int br_iv = snap_bright ? beat_iv : C.min_interval_ms;
    if (want_level != d->sent_level && t - d->last_bright >= br_iv) {
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
    char b[576]; int n = snprintf(b, sizeof b, "meter: level %.0fdB (avg %.0f) | beat %d-%dHz peak %.0f%% of avg, %d beat(s)",
                                   f.db, a->loud_ref_db, C.beat_low_hz, C.beat_high_hz, m->peak_ratio * 100, m->beats);
    if (a->bpm_conf >= 0.30 && a->bpm > 50.0)
        n += snprintf(b + n, sizeof b - n, ", %.0f BPM (%.0f%%, flash=%dms min=%dms)", a->bpm, a->bpm_conf * 100, eff_flash_ms(a), eff_min_beat_ms(a));
    n += snprintf(b + n, sizeof b - n, " | bands:");
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
/* the drive history (energy thread) sends on it too */
static pthread_mutex_t meter_mu = PTHREAD_MUTEX_INITIALIZER;
static void history_hello(int fd);
static void meter_accept(void) {
    if (meter_srv < 0) return;
    int fresh[METER_CLIENTS], nf = 0;
    pthread_mutex_lock(&meter_mu);
    for (int c; (c = accept(meter_srv, NULL, NULL)) >= 0; ) {
        fcntl(c, F_SETFL, O_NONBLOCK); fcntl(c, F_SETFD, FD_CLOEXEC);
        int slot = -1;
        for (int i = 0; i < METER_CLIENTS; i++) if (meter_cl[i] < 0) { slot = i; break; }
        if (slot < 0) { slot = meter_oldest; close(meter_cl[slot]); meter_oldest = (slot + 1) % METER_CLIENTS; }
        meter_cl[slot] = c;
        if (nf < METER_CLIENTS) fresh[nf++] = c;
    }
    pthread_mutex_unlock(&meter_mu);
    for (int i = 0; i < nf; i++) history_hello(fresh[i]);
}
static void meter_broadcast(const char *b, int n) {
    pthread_mutex_lock(&meter_mu);
    for (int i = 0; i < METER_CLIENTS; i++) {
        if (meter_cl[i] < 0) continue;
        if (send(meter_cl[i], b, n, MSG_DONTWAIT | MSG_NOSIGNAL) < 0 && errno != EAGAIN) { close(meter_cl[i]); meter_cl[i] = -1; }
    }
    pthread_mutex_unlock(&meter_mu);
}
typedef struct { long long last; int beats; double peak; } MeterStream;
#define KMH_PER_MPH 1.609344
/* db average_db beat_peak beats selected_band nbands band_db... bpm bpm_conf tune_rem_s */
static void meter_stream(MeterStream *m, Ana *a, Frame f, long long t) {
    if (f.beat) m->beats++;
    if (f.beat_ratio > m->peak) m->peak = f.beat_ratio;
    if (t - m->last < 100) return;
    meter_accept();
    char b[576];
    int n = snprintf(b, sizeof b, "%.1f %.1f %.2f %d %d %d", f.db, a->loud_ref_db, m->peak, m->beats, f.band, C.nbands);
    for (int i = 0; i < C.nbands; i++) n += snprintf(b + n, sizeof b - n, " %.1f", f.band_db[i]);
    n += snprintf(b + n, sizeof b - n, " %.1f %.2f %d\n", a->bpm, a->bpm_conf, tune_rem_sec());
    meter_broadcast(b, n);
    if (!strcmp(C.mode, "nightdrive")) {
        double gv = 0; char gb[64];
        if (!strcmp(C.nd_source, "speed") && read_cached_speed(&gv)) {
            int gn = snprintf(gb, sizeof gb, "speed %.2f %d\n", speed_in_kmh() ? gv : gv / KMH_PER_MPH, speed_wheel_index(gv));
            meter_broadcast(gb, gn);
        } else if (strcmp(C.nd_source, "speed") && read_power(&gv)) {
            int gn = snprintf(gb, sizeof gb, "power %.2f %d\n", gv, power_wheel_index(gv));
            meter_broadcast(gb, gn);
        }
    }
    m->last = t; m->beats = 0; m->peak = 0;
}

/* ---------- speed mode: colour follows the car's speed, no audio involved ---------- */
#define SPEED_POLL_MS 500            /* each read is one connection to HMG-API's vehicle service */
#define SPEED_LOST_MS 15000          /* no readings this long: give the lamp back */

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
   them, and the speeds are shared evenly over the colours left, so no stretch of road sits on one colour.
   When a 0x01xx stock colour (64..73) is involved, walks smoothly by hue along vivid_ring[]. */
static int wheel_walk(double v, double lo, double hi, int a, int b) {
    double x = hi > lo ? (v - lo) / (hi - lo) : (v >= lo ? 1 : 0);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    a = clampi(a, 0, wheel_n - 1);
    b = clampi(b, 0, wheel_n - 1);
    if (a == b) return a;
    if ((a >= 64 || b >= 64) && vivid_n > 0) {
        double ha = wheel_hue(a), hb = wheel_hue(b);
        double dh = hb - ha;
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        double span = fabs(dh);
        int mid_idx[MAXWHEEL]; double mid_off[MAXWHEEL]; int mn = 0;
        for (int k = 0; k < vivid_n; k++) {
            int idx = vivid_ring[k];
            if (idx == a || idx == b) continue;
            double off = dh >= 0.0 ? fmod(vivid_hue[k] - ha + 360.0, 360.0) : fmod(ha - vivid_hue[k] + 360.0, 360.0);
            if (off <= 4.0 || off >= span - 4.0) continue;
            int pos = mn++;
            while (pos > 0 && mid_off[pos - 1] > off) {
                mid_off[pos] = mid_off[pos - 1];
                mid_idx[pos] = mid_idx[pos - 1];
                pos--;
            }
            mid_off[pos] = off;
            mid_idx[pos] = idx;
        }
        int path[MAXWHEEL + 2], n = 0;
        path[n++] = a;
        double prev_off = -99.0;
        for (int i = 0; i < mn; i++) {
            if (mid_off[i] - prev_off < 2.5) continue;
            path[n++] = mid_idx[i];
            prev_off = mid_off[i];
        }
        path[n++] = b;
        return path[(int)lround(x * (n - 1))];
    }
    int d = (b - a + 64) % 64;
    if (d > 32) d -= 64;
    int steps = d < 0 ? -d : d, dir = d < 0 ? -1 : 1, path[64], n = 0;
    path[n++] = a;
    for (int i = 1; i <= steps; i++) {
        int j = (a + dir * i + 64) % 64;
        if (j == b || !wheel_pale(j)) path[n++] = j;
    }
    return path[(int)lround(x * (n - 1))];
}
/* power: regen colour .. zero colour .. full-power colour, so regen reads as its own colours */
static int power_wheel_index(double kw) {
    double zero = 0;
    if (zero < C.power_low) zero = C.power_low;
    if (zero > C.power_high) zero = C.power_high;
    if (kw <= zero) return wheel_walk(kw, C.power_low, zero, C.power_color_low, C.power_color_zero);
    return wheel_walk(kw, zero, C.power_high, C.power_color_zero, C.power_color_high);
}
static int speed_wheel_index(double kmh) {
    double k = speed_in_kmh() ? 1 : KMH_PER_MPH;
    return wheel_walk(kmh, C.speed_low * k, C.speed_high * k, C.speed_color_low, C.speed_color_high);
}
/* ---------- lamp test, for the app's Report a problem page ----------
   The app and this service talk through files in the app's files/diag/. The app writes "request", which we delete
   when we've read it: "test" runs the test, "save colour=yes bright=no" writes the report with the owner's answers.
   We write "result", key=value lines, replaced whole after each step. The page's QR code is built from those. */
#define DIAG_RESULT_MAX 2048
static char diag_dir[320];
static int diag_hold_ms = 2000;       /* each colour and level stays this long, so the owner can see it */
static int diag_pending;
static char diag_buf[DIAG_RESULT_MAX]; static int diag_len;
static const char *getprop_bin = "/system/bin/getprop", *hmgapi_dir = "/data/hmgapi";
static const char *report_dir = "/data/hmgapi/public/light-vibes";
static const char *usb_roots[4] = {"/storage/usb0", "/storage/usb1", "/storage/usb2", NULL};
static int usb_any_folder;            /* tests: a folder counts as a stick without being a mount point */

static void diag_set_dir(const char *settings) {
    snprintf(diag_dir, sizeof diag_dir, "%.300s", settings);
    char *sl = strrchr(diag_dir, '/');
    if (sl) snprintf(sl + 1, sizeof diag_dir - (size_t)(sl + 1 - diag_dir), "diag"); else diag_dir[0] = 0;
}
static void dkv(const char *k, const char *fmt, ...) {
    if (diag_len >= DIAG_RESULT_MAX - 2) return;
    int n = snprintf(diag_buf + diag_len, DIAG_RESULT_MAX - diag_len, "%s=", k);
    if (n > 0) diag_len += n;
    if (diag_len >= DIAG_RESULT_MAX - 2) { diag_len = DIAG_RESULT_MAX - 2; }
    va_list ap; va_start(ap, fmt);
    n = vsnprintf(diag_buf + diag_len, DIAG_RESULT_MAX - 1 - diag_len, fmt, ap);
    va_end(ap);
    if (n > 0) diag_len += n;
    if (diag_len > DIAG_RESULT_MAX - 2) diag_len = DIAG_RESULT_MAX - 2;
    diag_buf[diag_len++] = '\n'; diag_buf[diag_len] = 0;
}
/* root writes into the app's folder: never through a link left there */
static void diag_flush(const char *stage) {
    if (!diag_dir[0]) return;
    char tmp[360], path[360], head[64];
    snprintf(tmp, sizeof tmp, "%s/.result.tmp", diag_dir);
    snprintf(path, sizeof path, "%s/result", diag_dir);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (fd < 0) { say("can't write the lamp test's result: %s", strerror(errno)); return; }
    fchmod(fd, 0644);
    int n = snprintf(head, sizeof head, "stage=%s\n", stage);
    int ok = write(fd, head, n) == n && write(fd, diag_buf, diag_len) == diag_len;
    close(fd);
    if (!ok || rename(tmp, path)) { say("can't write the lamp test's result: %s", strerror(errno)); unlink(tmp); }
}
/* first line a program prints, or "" */
static void first_line_of(char *const argv[], char *out, size_t n) {
    out[0] = 0;
    int fd[2]; if (pipe(fd)) return;
    fcntl(fd[0], F_SETFD, FD_CLOEXEC); fcntl(fd[1], F_SETFD, FD_CLOEXEC);   /* the energy sampler forks from another thread */
    pid_t p = spawn(argv, fd[1], 0);
    close(fd[1]);
    FILE *f = fdopen(fd[0], "r");
    char line[256];
    if (f && fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; snprintf(out, n, "%s", line); }
    while (f && fgets(line, sizeof line, f)) { }
    if (f) fclose(f);
    if (p > 0) waitpid(p, NULL, 0);
}
static void diag_prop(const char *key, const char *prop) {
    char v[96]; char *a[] = {(char *)getprop_bin, (char *)prop, NULL};
    first_line_of(a, v, sizeof v);
    dkv(key, "%s", v[0] ? v : "?");
}
/* the raw answer to a harmless status request: tells a missing service or a wrong call number from a working one */
static void diag_service_reply(void) {
    char v[160]; char *a[] = {C.service_bin, "call", C.svc_name, "76", NULL};
    first_line_of(a, v, sizeof v);
    const char *p = strstr(v, "Parcel(");
    if (p) dkv("svc", "%.8s", p + 7);
    else dkv("svc", "%s", strstr(v, "not exist") || !v[0] ? "missing" : "other");
}
static void diag_about_car(void) {
    dkv("v", "%s", VERSION);
    diag_prop("product", "ro.build.product");
    diag_prop("name", "ro.product.name");
    diag_prop("usm", "persist.sys.usmAmbientLight");
    char p[320], line[96] = "?"; snprintf(p, sizeof p, "%s/installed.txt", hmgapi_dir);
    FILE *f = fopen(p, "r");
    if (f) { if (fgets(line, sizeof line, f)) line[strcspn(line, "\r\n")] = 0; fclose(f); }
    dkv("hmg", "%s", !strncmp(line, "version ", 8) ? line + 8 : line);
    char mods[200] = ""; int n = 0; snprintf(p, sizeof p, "%s/mods", hmgapi_dir);
    DIR *d = opendir(p); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "light-vibes")) continue;
        n += snprintf(mods + n, sizeof mods - n, "%s%s", n ? "," : "", e->d_name);
        if (n >= (int)sizeof mods) { n = sizeof mods - 1; break; }
    }
    if (d) closedir(d);
    dkv("mods", "%s", mods);
    dkv("mode", "%s", C.mode);
    dkv("enabled", "%d", C.enabled);
    dkv("range", "%d-%d", C.bright_min, C.bright_max);
    struct mixer *m = mixer_open(C.card);
    struct mixer_ctl *c = m ? mixer_get_ctl_by_name(m, C.loop_ctl) : NULL;
    dkv("loop", "%s", c ? (mixer_ctl_get_value(c, 0) ? "on" : "off") : "none");
    if (m) mixer_close(m);
    dkv("rate", "%d", music_rate());
    dkv("songs", "%d", totals.sessions);
    if (totals.peak_db > -199) dkv("peak", "%.0f", totals.peak_db); else dkv("peak", "none");
    dkv("silence", "%d", C.silence_db);
    dkv("sent", "%d/%d", totals.colours, totals.brights);
    dkv("chk", "%d/%d/%d/%d", chk.ok, chk.no_reply, chk.colour_stuck, chk.bright_stuck);
}
static void diag_hold(void) {
    for (int t = 0; t < diag_hold_ms && !stop_flag; t += 100) usleep((diag_hold_ms < 100 ? diag_hold_ms : 100) * 1000);
}
static void diag_run(void) {
    diag_pending = 0;
    diag_len = 0; diag_buf[0] = 0;
    say("lamp test starting");
    wait_pending();
    check_cancel();
    diag_about_car();
    diag_service_reply();
    diag_flush("start");          /* the app shows each step as it happens: start, red, green, blue, low, high, mid, restoring */

    int l[6]; memset(l, 0, sizeof l);
    lamp_last_status(l);                /* whatever is in the log already doesn't count as an answer */
    int reply = read_lamp(l) && lamp_fresh;
    dkv("reply", "%d", reply);
    base.valid = 0;
    snapshot_base();
    if (base.valid) dkv("user", "%d,%d,%d,%d,%d", base.r, base.g, base.b, base.bright_raw, base.drive_mode);
    else dkv("user", "unknown");
    diag_flush("start");          /* again, now with the owner's colour, which the app shows next to the first and last steps */
    dkv("dim", "%d", reply ? l[5] : -1);
    take_over_lamp();
    check_due = 0;                      /* this test is the check */

    int colours[3] = {
        nearest_vivid_wheel(255, 0, 20),
        nearest_vivid_wheel(0, 255, 64),
        nearest_vivid_wheel(20, 30, 255)
    };
    static const char *colour_steps[] = {"red", "green", "blue"};
    int cn = 3, c_ok = 0, c_answers = 0;
    for (int i = 0; i < cn && !stop_flag; i++) {
        int w = colours[i];
        diag_flush(colour_steps[i]);
        light_rgb(WHEEL_R[w], WHEEL_G[w], WHEEL_B[w], 1);
        diag_hold();
        if (read_lamp(l) && lamp_fresh) {
            c_answers++;
            if (l[0] == WHEEL_R[w] && l[1] == WHEEL_G[w] && l[2] == WHEEL_B[w]) c_ok++;
            else say("lamp test: sent colour %d %d %d, the lamp reports %d %d %d", WHEEL_R[w], WHEEL_G[w], WHEEL_B[w], l[0], l[1], l[2]);
        }
    }
    dkv("c", "%d/%d", c_ok, cn);
    dkv("dm", "%d", c_answers ? l[4] : -1);

    static const int levels[] = {2, 11, 6};     /* raw: dim, bright, in between (levels 1, 10, 5) */
    static const char *level_steps[] = {"low", "high", "mid"};
    int bn = (int)(sizeof levels / sizeof levels[0]), b_ok = 0;
    for (int i = 0; i < bn && !stop_flag; i++) {
        diag_flush(level_steps[i]);
        light_bright_raw(levels[i], 1);
        diag_hold();
        if (read_lamp(l) && lamp_fresh) {
            if (l[3] == levels[i]) b_ok++;
            else say("lamp test: sent brightness raw %d, the lamp reports %d", levels[i], l[3]);
        }
    }
    dkv("b", "%d/%d", b_ok, bn);
    int ceil12 = -1, ceil15 = -1;
    if (reply && !stop_flag) {   /* safe probe above UI 10 (raw 11): checks whether MICOM clamps or accepts raw 12/15 */
        light_bright_raw(12, 1);
        usleep((diag_hold_ms < 250 ? diag_hold_ms : 250) * 1000);
        if (read_lamp(l) && lamp_fresh) ceil12 = l[3];
        light_bright_raw(15, 1);
        usleep((diag_hold_ms < 250 ? diag_hold_ms : 250) * 1000);
        if (read_lamp(l) && lamp_fresh) ceil15 = l[3];
        say("lamp test: ceiling probe raw 12 -> %d, raw 15 -> %d", ceil12, ceil15);
    }
    dkv("b_ceil", "%d,%d", ceil12, ceil15);
    diag_flush("restoring");

    int had_base = base.valid, want[5] = {base.r, base.g, base.b, base.bright_raw, base.drive_mode};
    if (had_base) restore_base();
    usleep(diag_hold_ms < 500 ? diag_hold_ms * 1000 : 500 * 1000);
    int restored = had_base && read_lamp(l) && lamp_fresh && l[3] == want[3]
        && (want[4] == 2 ? l[4] == 2 : l[0] == want[0] && l[1] == want[1] && l[2] == want[2]);
    dkv("restored", "%d", restored);
    base.valid = 0;
    last_rgb[0] = last_rgb[1] = last_rgb[2] = -1; last_raw = -1;
    say("lamp test done: replies %d, colours %d/%d, brightness %d/%d (ceil %d,%d), restored %d",
        reply, c_ok, cn, b_ok, bn, ceil12, ceil15, restored);
    diag_flush("done");
}
static int write_report(const char *path, const char *colour, const char *bright) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return 0;
    fchmod(fd, 0644);
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return 0; }
    fprintf(f, "Light Vibes report\nseen_colour=%s\nseen_brightness=%s\n%s\n--- log ---\n", colour, bright, diag_buf);
    log_tail(f, 200, NULL);
    int ok = !fflush(f) && !fsync(fd);
    fclose(f);
    return ok;
}
static int is_stick(const char *dir) {
    struct stat here, up; char parent[64];
    if (stat(dir, &here) || !S_ISDIR(here.st_mode)) return 0;
    if (usb_any_folder) return 1;
    snprintf(parent, sizeof parent, "%s/..", dir);     /* a stick is a separate file system; an empty mount point isn't */
    return !stat(parent, &up) && up.st_dev != here.st_dev;
}
static const char *find_stick(void) {
    for (int i = 0; usb_roots[i]; i++) if (is_stick(usb_roots[i])) return usb_roots[i];
    return NULL;
}

/* ---------- 20-second zero-jitter RAM-buffered USB tuning recorder (flushed to USB only after capture ends) ---------- */
static struct {
    short *pcm_mem;
    char *csv_mem;
    size_t csv_len, csv_cap;
    char sess_hdr[1792];
    char dir[340], rel[160];
    int req_sec, active, in_pcm, pcm_rate, last_rem;
    long long target_frames, pcm_cap_frames, written_frames, csv_rows;
} tune;

static void tune_free_mem(void) {
    free(tune.pcm_mem); tune.pcm_mem = NULL;
    free(tune.csv_mem); tune.csv_mem = NULL;
    tune.csv_len = tune.csv_cap = 0;
}
static void tune_csv_append(const char *fmt, ...) {
    if (!tune.csv_mem) return;
    if (tune.csv_len + 512 > tune.csv_cap) {
        size_t ncap = tune.csv_cap * 2;
        char *nb = realloc(tune.csv_mem, ncap);
        if (!nb) return;
        tune.csv_mem = nb;
        tune.csv_cap = ncap;
    }
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tune.csv_mem + tune.csv_len, tune.csv_cap - tune.csv_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t avail = tune.csv_cap - 1 - tune.csv_len;
        tune.csv_len += (size_t)n < avail ? (size_t)n : avail;
    }
}
static int tune_rem_sec(void) {
    if (!tune.active || tune.pcm_rate <= 0) return tune.req_sec > 0 ? tune.req_sec : 0;
    long long left = tune.target_frames - tune.written_frames;
    return left > 0 ? (int)((left + tune.pcm_rate - 1) / tune.pcm_rate) : 0;
}
static void tune_publish(const char *state, const char *msg) {
    mkdir(report_dir, 0755); chmod(report_dir, 0755);
    char tmp[360], dst[360];
    snprintf(tmp, sizeof tmp, "%s/.tune_status.tmp", report_dir);
    snprintf(dst, sizeof dst, "%s/tune_status", report_dir);
    FILE *f = fopen(tmp, "w");
    if (f) {
        fprintf(f, "%s %s\n", state, msg);
        fclose(f);
        chmod(tmp, 0644);
        rename(tmp, dst);
    }
    char line[300];
    int n = snprintf(line, sizeof line, "cap %s %s\n", state, msg);
    if (n > 0 && n < (int)sizeof line) meter_broadcast(line, n);
}
static void wav_put16(unsigned char *p, unsigned int v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static void wav_put32(unsigned char *p, unsigned int v) { p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255; }
static void wav_header(unsigned char h[44], int srate, unsigned int data_bytes) {
    memcpy(h, "RIFF", 4); wav_put32(h + 4, 36U + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8); wav_put32(h + 16, 16);
    wav_put16(h + 20, 1); wav_put16(h + 22, CH);
    wav_put32(h + 24, (unsigned int)srate);
    wav_put32(h + 28, (unsigned int)(srate * CH * 2));
    wav_put16(h + 32, CH * 2); wav_put16(h + 34, 16);
    memcpy(h + 36, "data", 4); wav_put32(h + 40, data_bytes);
}
static void tune_finish(const char *reason) {
    if (!tune.active) {
        tune_free_mem();
        if (tune.req_sec > 0) { tune.req_sec = 0; tune_publish("idle", "Recording cancelled"); }
        return;
    }
    tune.active = 0;
    tune.req_sec = 0;
    unsigned int data_bytes = (unsigned int)(tune.written_frames * CH * sizeof(short));
    char wp[400], cp[400], sp[400];
    snprintf(wp, sizeof wp, "%s/capture.wav", tune.dir);
    snprintf(cp, sizeof cp, "%s/frames.csv", tune.dir);
    snprintf(sp, sizeof sp, "%s/session.txt", tune.dir);
    int ok = 1;
    FILE *wf = fopen(wp, "wb");
    if (wf && tune.pcm_mem) {
        unsigned char hdr[44];
        wav_header(hdr, tune.pcm_rate, data_bytes);
        if (fwrite(hdr, 1, sizeof hdr, wf) != sizeof hdr) ok = 0;
        if (data_bytes > 0 && fwrite(tune.pcm_mem, 1, data_bytes, wf) != data_bytes) ok = 0;
        if (fflush(wf) || fsync(fileno(wf))) ok = 0;
        fclose(wf);
    } else {
        if (wf) fclose(wf);
        ok = 0;
    }
    FILE *cf = fopen(cp, "wb");
    if (cf && tune.csv_mem) {
        if (tune.csv_len > 0 && fwrite(tune.csv_mem, 1, tune.csv_len, cf) != tune.csv_len) ok = 0;
        if (fflush(cf) || fsync(fileno(cf))) ok = 0;
        fclose(cf);
    } else {
        if (cf) fclose(cf);
        ok = 0;
    }
    FILE *sf = fopen(sp, "w");
    if (sf) {
        long long dur_ms = tune.pcm_rate > 0 ? tune.written_frames * 1000LL / tune.pcm_rate : 0;
        fputs(tune.sess_hdr, sf);
        fprintf(sf, "captured_ms=%lld\ncaptured_frames=%lld\ncsv_rows=%lld\nfinish_reason=%s\n\n--- recent log ---\n",
                dur_ms, tune.written_frames, tune.csv_rows, reason);
        log_tail(sf, 120, NULL);
        fflush(sf);
        fsync(fileno(sf));
        fclose(sf);
    }
    tune_free_mem();
    sync();
    double secs = tune.pcm_rate > 0 ? (double)tune.written_frames / tune.pcm_rate : 0.0;
    if (!ok) {
        say("tuning capture write failed in %s", tune.dir);
        tune_publish("error", "Failed writing tuning clip to USB drive");
        return;
    }
    char msg[240];
    snprintf(msg, sizeof msg, "Saved %.1fs to USB: %s", secs, tune.rel);
    say("tuning capture finished (%s): %.1fs -> %s", reason, secs, tune.dir);
    tune_publish(!strcmp(reason, "stopped") ? "stopped" : "done", msg);
}
static void tune_start(int srate) {
    if (tune.active || tune.req_sec <= 0) return;
    const char *stick = find_stick();
    if (!stick) {
        tune.req_sec = 0;
        tune_publish("error", "No USB stick found — plug in a USB drive first");
        say("tuning capture failed: no USB stick mounted");
        return;
    }
    char root[280], stamp_str[32];
    snprintf(root, sizeof root, "%s/Light Vibes tuning", stick);
    mkdir(root, 0777);
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    strftime(stamp_str, sizeof stamp_str, "tune-%Y%m%d-%H%M%S", &tm);
    snprintf(tune.rel, sizeof tune.rel, "Light Vibes tuning/%s", stamp_str);
    snprintf(tune.dir, sizeof tune.dir, "%s/%s", root, stamp_str);
    int dir_ok = (mkdir(tune.dir, 0777) == 0);
    for (int suffix = 2; !dir_ok && errno == EEXIST && suffix < 20; suffix++) {
        snprintf(tune.rel, sizeof tune.rel, "Light Vibes tuning/%s-%d", stamp_str, suffix);
        snprintf(tune.dir, sizeof tune.dir, "%s/%s-%d", root, stamp_str, suffix);
        dir_ok = (mkdir(tune.dir, 0777) == 0);
    }
    if (!dir_ok) {
        tune.req_sec = 0;
        tune_publish("error", "Could not create tuning folder on USB drive");
        say("tuning capture mkdir failed in %s: %s", tune.dir, strerror(errno));
        return;
    }
    tune_free_mem();
    tune.target_frames = (long long)srate * tune.req_sec;
    tune.pcm_cap_frames = tune.target_frames + 16LL * PERIOD;
    tune.pcm_mem = malloc((size_t)tune.pcm_cap_frames * CH * sizeof(short));
    tune.csv_cap = (size_t)tune.req_sec * 24000U + 65536U;
    tune.csv_mem = malloc(tune.csv_cap);
    tune.csv_len = 0;
    if (!tune.pcm_mem || !tune.csv_mem) {
        tune_free_mem();
        tune.req_sec = 0;
        tune_publish("error", "Not enough memory for tuning capture");
        say("tuning capture malloc failed for %ds", tune.req_sec);
        return;
    }
    tune_csv_append("t_ms,db,avg_db,bpm,bpm_conf,eff_flash_ms,eff_min_beat_ms,beat_eb,beat_slow,beat_ref_floor,beat_ratio,onset_ratio,beat_armed,beat,sel_band,want_r,want_g,want_b,want_level,sent_level,b0_db,b1_db,b2_db,b3_db,b4_db,b5_db,beat_density\n");

    char prod[96], pname[96];
    char *a1[] = {(char *)getprop_bin, "ro.build.product", NULL};
    char *a2[] = {(char *)getprop_bin, "ro.product.name", NULL};
    first_line_of(a1, prod, sizeof prod);
    first_line_of(a2, pname, sizeof pname);
    int hn = snprintf(tune.sess_hdr, sizeof tune.sess_hdr,
        "Light Vibes tuning capture\nversion=%s\ntimestamp=%s\nsample_rate=%d\n"
        "product=%s\nname=%s\npalette=%s (setting=%s)\n"
        "mode=%s\npreset=%s\nbrightness=%d..%d (fixed=%d, curve=%s, range_db=%d, dim=%d)\n"
        "auto_bpm=%d\nsensitivity=%d\nbeat_hz=%d-%d\nbeat_avg_ms=%d\nmin_beat_ms=%d\n"
        "flash_enabled=%d\nbeat_pulse=%d\nflash_ms=%d\nbeat_step=%d\nskip_white=%d\npulse_decay=%d\npulse_color=%d\n"
        "duet=%d,%d,%d\nsyn_shift=%d\nember_theme=%d\ndrift=%d,%d\nnd_source=%s\n"
        "spectrum_dynamic=%d\nswitch_margin=%d\nband_smoothing_ms=%d\nbands=%s\n"
        "min_interval_ms=%d\nsilence_db=%d\n",
        VERSION, stamp_str, srate,
        prod[0] ? prod : "?", pname[0] ? pname : "?", active_palette, C.palette,
        C.mode, C.preset, C.bright_min, C.bright_max, C.bright_fixed, C.bright_curve, C.loudness_range_db, C.gauge_dim,
        C.auto_bpm, C.sensitivity, C.beat_low_hz, C.beat_high_hz, C.beat_avg_ms, C.min_beat_ms,
        flash_enabled(), beat_pulse_enabled(), C.flash_ms, C.beat_step, C.skip_white, C.pulse_decay, C.pulse_color,
        C.duet_bed, C.duet_accent, C.duet_beats, C.syn_shift, C.ember_theme, C.drift_theme, C.drift_beats, C.nd_source,
        C.spectrum_dynamic, C.switch_margin, C.band_smoothing_ms, C.bands_text,
        C.min_interval_ms, C.silence_db);
    if (base.valid && hn > 0 && hn < (int)sizeof tune.sess_hdr - 80) {
        snprintf(tune.sess_hdr + hn, sizeof tune.sess_hdr - (size_t)hn,
                 "user_light=%d,%d,%d,raw=%d,dm=%d,dim=%d\n",
                 base.r, base.g, base.b, base.bright_raw, base.drive_mode, base.dim);
    }
    tune.active = 1;
    tune.pcm_rate = srate;
    tune.written_frames = 0;
    tune.csv_rows = 0;
    tune.last_rem = tune.req_sec;
    char msg[128];
    snprintf(msg, sizeof msg, "Recording to USB: %ds left...", tune.req_sec);
    say("tuning capture started (RAM-buffered): %ds at %d Hz -> %s", tune.req_sec, srate, tune.dir);
    tune_publish("rec", msg);
}
static void tune_write_pcm(const short *pcm_samples, int frames) {
    if (!tune.active || !tune.pcm_mem || frames <= 0) return;
    long long space = tune.pcm_cap_frames - tune.written_frames;
    int n = (long long)frames < space ? frames : (int)space;
    if (n > 0) {
        memcpy(tune.pcm_mem + tune.written_frames * (long long)CH, pcm_samples, (size_t)n * CH * sizeof(short));
        tune.written_frames += n;
    }
    int rem = tune_rem_sec();
    if (rem > 0 && rem != tune.last_rem) {
        tune.last_rem = rem;
        char msg[128];
        snprintf(msg, sizeof msg, "Recording to USB: %ds left...", rem);
        tune_publish("rec", msg);
    }
}
static void tune_write_frame(long long t, const Ana *a, const Frame *f, const Drv *d) {
    if (!tune.active || !tune.csv_mem) return;
    tune_csv_append("%lld,%.1f,%.1f,%.1f,%.2f,%d,%d,%.3e,%.3e,%.3e,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d",
            t, f->db, a->loud_ref_db, a->bpm, a->bpm_conf, eff_flash_ms(a), eff_min_beat_ms(a),
            a->last_eb, a->beat_slow, a->beat_ref_floor, f->beat_ratio, f->onset_ratio,
            a->beat_armed, f->beat, f->band, d->want_r, d->want_g, d->want_b, d->want_level, d->sent_level);
    for (int i = 0; i < 6; i++) tune_csv_append(",%.1f", i < C.nbands ? f->band_db[i] : -140.0);
    tune_csv_append(",%.3f\n", a->beat_density);
    tune.csv_rows++;
    if (tune.written_frames >= tune.target_frames) tune_finish("complete");
}

/* "save colour=yes bright=no": only the three known answers get through */
static const char *diag_answer(const char *req, const char *key) {
    const char *p = strstr(req, key);
    if (!p) return "unknown";
    p += strlen(key);
    return !strncmp(p, "yes", 3) ? "yes" : !strncmp(p, "no", 2) ? "no" : "unknown";
}
static void diag_save(const char *req) {
    const char *colour = diag_answer(req, "colour="), *bright = diag_answer(req, "bright=");
    if (!diag_len) diag_about_car();     /* the service restarted since the test */
    char path[400], name[64], usb[200] = "none";
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    strftime(name, sizeof name, "light-vibes-report-%Y%m%d-%H%M.txt", &tm);
    mkdir(report_dir, 0755); chmod(report_dir, 0755);    /* adb pull, as the shell user */
    snprintf(path, sizeof path, "%s/report.txt", report_dir);
    int kept = write_report(path, colour, bright);
    for (int i = 0; usb_roots[i]; i++) {
        if (!is_stick(usb_roots[i])) continue;
        char d[300]; snprintf(d, sizeof d, "%s/Light Vibes reports", usb_roots[i]);
        mkdir(d, 0777);
        snprintf(path, sizeof path, "%s/%s", d, name);
        if (write_report(path, colour, bright)) { snprintf(usb, sizeof usb, "%.60s/Light Vibes reports/%s", usb_roots[i], name); sync(); break; }
    }
    say("report saved: owner saw colour %s, brightness %s; USB %s", colour, bright, usb);
    dkv("seen", "%s/%s", colour, bright);
    dkv("kept", "%d", kept);
    dkv("usb", "%s", usb);
    diag_flush("saved");
}
enum { DIAG_NONE = 0, DIAG_TEST = 1, DIAG_SNAP = 2, DIAG_WATCH = 3 };
static int diag_watch_ms = 900;        /* background color watch cadence; tests set this small */
static int diag_watch_max_polls = 700; /* ~10.5 min safety cap so watch never hangs forever */

static void append_captured_color(int r, int g, int b, int br, int dim, int dm, const char *src, char *usb_out, size_t usb_sz) {
    char line[256], stamp[32], path[400];
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
    int ui_br = br > 0 ? br - 1 : 0;
    int n = snprintf(line, sizeof line,
                     "%s  rgb=%d,%d,%d  hex=0x%04X,0x%04X,0x%04X  ui8=%d,%d,%d  bright_raw=%d (ui=%d)  dim=%d  drive_mode=%d  source=%s\n",
                     stamp, r, g, b, r & 0xffff, g & 0xffff, b & 0xffff, r & 0xff, g & 0xff, b & 0xff, br, ui_br, dim, dm, src);
    if (n <= 0) { snprintf(usb_out, usb_sz, "none"); return; }
    if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;

    mkdir(report_dir, 0755); chmod(report_dir, 0755);
    snprintf(path, sizeof path, "%s/captured-colors.txt", report_dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0644);
    if (fd >= 0) { fchmod(fd, 0644); (void)write(fd, line, n); fsync(fd); close(fd); }

    snprintf(usb_out, usb_sz, "none");
    const char *stick = find_stick();
    if (stick) {
        char d[300]; snprintf(d, sizeof d, "%s/Light Vibes reports", stick);
        mkdir(d, 0777);
        snprintf(path, sizeof path, "%s/captured-colors.txt", d);
        fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0644);
        if (fd >= 0) {
            fchmod(fd, 0644);
            if (write(fd, line, n) == n && !fsync(fd)) {
                snprintf(usb_out, usb_sz, "%.60s/Light Vibes reports/captured-colors.txt", stick);
                sync();
            }
            close(fd);
        }
    }
}

static int diag_read_req(char *req, size_t rsz) {
    if (!diag_dir[0]) return 0;
    char path[360]; snprintf(path, sizeof path, "%s/request", diag_dir);
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    int n = read(fd, req, rsz - 1);
    close(fd);
    unlink(path);
    if (n <= 0) return 0;
    req[n] = 0;
    req[strcspn(req, "\r\n")] = 0;
    return 1;
}

static void diag_format_items(int items[][6], int cnt, char *out, size_t osz) {
    out[0] = 0;
    size_t pos = 0;
    int start = cnt > 24 ? cnt - 24 : 0;
    for (int i = cnt - 1; i >= start; i--) {
        int w = snprintf(out + pos, osz - pos, "%s%d,%d,%d,%d,%d,%d",
                         pos ? ";" : "", items[i][0], items[i][1], items[i][2], items[i][3], items[i][4], items[i][5]);
        if (w <= 0 || pos + (size_t)w >= osz - 1) break;
        pos += (size_t)w;
    }
}

static void diag_snap(void) {
    diag_pending = 0;
    diag_len = 0; diag_buf[0] = 0;
    wait_pending();
    check_cancel();
    int dummy[6] = {0}, l[6] = {0};
    lamp_last_status(dummy);
    int ok = read_lamp(l) && lamp_fresh;
    dkv("reply", "%d", ok);
    if (ok) {
        int s7[7] = {l[0], l[1], l[2], l[3], l[4], 0, (l[5] >= 1 && l[5] <= 2) ? l[5] : -1};
        base_from(s7);
        saved_write(0);
        char usb[200];
        append_captured_color(l[0], l[1], l[2], l[3], l[5], l[4], "manual", usb, sizeof usb);
        dkv("rgb", "%d,%d,%d", l[0], l[1], l[2]);
        dkv("hex", "0x%04X,0x%04X,0x%04X", l[0] & 0xffff, l[1] & 0xffff, l[2] & 0xffff);
        dkv("bright", "%d", l[3]);
        dkv("dim", "%d", l[5]);
        dkv("dm", "%d", l[4]);
        dkv("usb", "%s", usb);
        say("color captured: rgb %d %d %d (0x%04X 0x%04X 0x%04X), bright raw %d, dim %d, dm %d -> USB %s",
            l[0], l[1], l[2], l[0] & 0xffff, l[1] & 0xffff, l[2] & 0xffff, l[3], l[5], l[4], usb);
    } else {
        dkv("usb", "none");
        say("color capture: lamp did not reply");
    }
    base.valid = 0;
    diag_flush("snap_done");
}

static void diag_watch(void) {
    diag_pending = 0;
    wait_pending();
    check_cancel();
    int seen[64][6], nseen = 0, dummy[6] = {0};
    char last_usb[200] = "none", items[900];
    lamp_last_status(dummy);
    say("auto-capture watch started");

    for (int polls = 0; polls < diag_watch_max_polls && !stop_flag; polls++) {
        int l[6] = {0};
        int ok = read_lamp(l) && lamp_fresh;
        if (ok) {
            int dup = 0;
            for (int k = 0; k < nseen; k++) {
                if (seen[k][0] == l[0] && seen[k][1] == l[1] && seen[k][2] == l[2] && seen[k][5] == l[4]) { dup = 1; break; }
            }
            if (!dup && nseen < 64) {
                seen[nseen][0] = l[0]; seen[nseen][1] = l[1]; seen[nseen][2] = l[2];
                seen[nseen][3] = l[3]; seen[nseen][4] = l[5]; seen[nseen][5] = l[4];
                nseen++;
                int s7[7] = {l[0], l[1], l[2], l[3], l[4], 0, (l[5] >= 1 && l[5] <= 2) ? l[5] : -1};
                base_from(s7);
                saved_write(0);
                char src[24]; snprintf(src, sizeof src, "auto#%d", nseen);
                append_captured_color(l[0], l[1], l[2], l[3], l[5], l[4], src, last_usb, sizeof last_usb);
                say("auto-captured #%d: rgb %d %d %d (0x%04X 0x%04X 0x%04X) dm=%d -> USB %s",
                    nseen, l[0], l[1], l[2], l[0] & 0xffff, l[1] & 0xffff, l[2] & 0xffff, l[4], last_usb);
            }
        }
        diag_len = 0; diag_buf[0] = 0;
        dkv("reply", "%d", ok || nseen > 0 ? 1 : 0);
        dkv("watch_count", "%d", nseen);
        dkv("usb", "%s", strcmp(last_usb, "none") ? last_usb : (find_stick() ? "ready" : "none"));
        diag_format_items(seen, nseen, items, sizeof items);
        if (items[0]) dkv("items", "%s", items);
        diag_flush("watching");

        int step_ms = diag_watch_ms < 50 ? (diag_watch_ms > 0 ? diag_watch_ms : 1) : 50;
        int stop_watch = 0;
        for (int waited = 0; waited < diag_watch_ms && !stop_flag; waited += step_ms) {
            usleep(step_ms * 1000);
            char req[64];
            if (diag_read_req(req, sizeof req)) {
                if (!strncmp(req, "watch stop", 10)) { stop_watch = 1; break; }
                if (!strncmp(req, "test", 4)) { diag_pending = DIAG_TEST; base.valid = 0; return; }
                if (!strncmp(req, "snap", 4)) { diag_pending = DIAG_SNAP; base.valid = 0; return; }
            }
        }
        if (stop_watch) break;
    }
    base.valid = 0;
    diag_len = 0; diag_buf[0] = 0;
    dkv("reply", "%d", nseen > 0 ? 1 : 0);
    dkv("watch_count", "%d", nseen);
    dkv("usb", "%s", last_usb);
    diag_format_items(seen, nseen, items, sizeof items);
    if (items[0]) dkv("items", "%s", items);
    diag_flush("watch_done");
    say("auto-capture watch ended (%d unique color(s))", nseen);
}

static void diag_dispatch(void) {
    while (diag_pending && !stop_flag) {
        int op = diag_pending;
        if (op == DIAG_TEST) diag_run();
        else if (op == DIAG_SNAP) diag_snap();
        else if (op == DIAG_WATCH) diag_watch();
        else diag_pending = 0;
    }
}

/* 1 when a lamp test/snap/watch is waiting. A save or tuning capture is handled in-place without breaking audio capture. */
static int diag_poll(void) {
    if (diag_pending) return 1;
    char req[64];
    if (!diag_read_req(req, sizeof req)) return 0;
    if (!strncmp(req, "test", 4)) { diag_pending = DIAG_TEST; return 1; }
    if (!strncmp(req, "snap", 4)) { diag_pending = DIAG_SNAP; return 1; }
    if (!strncmp(req, "watch stop", 10)) return 0;
    if (!strncmp(req, "watch", 5)) { diag_pending = DIAG_WATCH; return 1; }
    if (!strncmp(req, "save", 4)) diag_save(req);
    else if (!strncmp(req, "capture", 7)) {
        if (strstr(req + 7, "stop")) {
            tune_finish("stopped");
        } else {
            int sec = atoi(req + 7);
            if (sec <= 0) sec = 20;
            sec = clampi(sec, 1, 120);
            if (!find_stick()) {
                tune_publish("error", "No USB stick found — plug in a USB drive first");
                say("tuning capture requested, but no USB stick found");
            } else {
                if (tune.active) tune_finish("restarted");
                tune.req_sec = sec;
                if (tune.in_pcm) tune_start(rate);
                else {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Waiting for music to start (%ds armed)...", sec);
                    tune_publish("wait", msg);
                    say("tuning capture armed (%ds) — waiting for music", sec);
                }
            }
        }
    }
    return 0;
}

/* ---------- power and efficiency: readings from the car's EV service (com.hkmc.micom.hev, IHEVService) ----------
   Found on the EV6 (CVEV 260507) by reading automotive.odex. getHEVStatus(flag) is call 3; flags are HEVManager.HEVFlag:
     1 AVG_FCU   unit of the average: 2 = mi/kWh on Kyle's US EV6 (1 and 3 are guesses: km/kWh, kWh/100 km)
     2 AVG_FCI   the cluster's average efficiency, in tenths (26 = 2.6 mi/kWh, matched the dash 2026-11-07)
     31 ODOMETER tenths of a km, even on the US car showing miles: 3.9 of them was the cluster's 2.4 mi (2026-11-07)
     66 DATC_CLMTPWRSUM_W  climate power, W (181 parked with the climate on)
     71/72 MCU_MG1/MG2MOTPOWER_WH: each motor's NET energy, Wh: it rises while driving and FALLS during regen (second
       drive, 2026-11-07: front 269 -> 253 while 89 rose 331 -> 347, the same 16). 89/90 MCU_MG1/MG2GENPOWER_WH: regen
       energy, only rises. So the energy used is the change in 71+72 alone; subtracting 89/90 as well counted regen
       twice, and treating a fall in 71/72 as a counter reset turned regen into huge positive power.
       A sampler thread adds up the change in 71+72 every second from car start. Power is that over about 2 s.
       The motor values leave out what else the battery supplies (losses, 12 V, other ECUs), so for the trip average the
       motor energy is multiplied by trip_scale (percent, hidden setting). 185 matched the cluster's Current Drive over
       5.1 km on 2026-11-07 (Current Drive 5.9 mi at 2.9 mi/kWh, less the first 2.4 mi at 3.0: 1.23 kWh, against
       484 Wh motor + 219 Wh climate here). One drive only: check it on more. Power stays unscaled: it's the motors'
       own power (-56 to +91 kW on a town drive, which looked right on the road). Raw samples go to energy.log. */
#define HEV_SERVICE "com.hkmc.micom.hev"
#define GAUGE_POLL_MS 1000
#define GAUGE_LOST_MS 15000
#define TRIP_MIN_TENTHS 5            /* 0.5 km (0.3 mi) before the trip average means anything */
static int hev_status(int flag, long long *out) {
    char f[16], v[200]; snprintf(f, sizeof f, "%d", flag);
    char *a[] = {C.service_bin, "call", HEV_SERVICE, "3", "i32", f, NULL};
    first_line_of(a, v, sizeof v);
    const char *p = strstr(v, "Parcel(00000000 ");
    if (!p) return 0;
    unsigned long x = strtoul(p + 16, NULL, 16);
    if (x == 0xffffffffUL) return 0;
    *out = (long long)(int)(unsigned)x;
    return 1;
}
/* Power is the change over about 1 s, sampled 4 times a second. Over 2 s, sampled once a second, the light trailed the
   real power by 2-3 s (drive, 2026-11-07). The totals move in whole Wh, so 1 s is ±3.6 kW: fine for colours. */
#define PW_WINDOW_MS 1000
#define ENERGY_TICK_MS 250
#define PW_N 32
static struct { long long t, wh; } pw[PW_N];
static int pw_n;
/* kW from a running total sampled over time: from the newest sample at least PW_WINDOW_MS old */
static int power_from_samples(long long t, long long wh, double *kw) {
    if (pw_n && (wh < pw[pw_n - 1].wh - 1000 || t < pw[pw_n - 1].t)) pw_n = 0;   /* the total was reset */
    if (pw_n == PW_N) { memmove(pw, pw + 1, sizeof pw[0] * (PW_N - 1)); pw_n--; }
    pw[pw_n].t = t; pw[pw_n].wh = wh; pw_n++;
    int k = -1;
    for (int i = pw_n - 1; i >= 0; i--) if (t - pw[i].t >= PW_WINDOW_MS) { k = i; break; }
    if (k < 0) return 0;
    double dt = (t - pw[k].t) / 1000.0, v = (wh - pw[k].wh) * 3.6 / dt;
    if (dt <= 0 || v > 700 || v < -400) { pw_n = 0; return 0; }
    *kw = v;
    return 1;
}
/* how much a counter went up since the last sample. A drop means it restarted (or wrapped): what it reads now is what
   it counted since. A jump no motor could make in a second is a glitch, and counts as nothing. For the regen totals. */
#define COUNTER_MAX_STEP 500
static long long counter_step(long long prev, long long now) {
    long long d = now >= prev ? now - prev : now;
    return d > COUNTER_MAX_STEP ? 0 : d;
}
/* the change in a net energy value: down is regen. A step no motor could make in a second is a glitch or a reset. */
static long long net_step(long long prev, long long now) {
    long long d = now - prev;
    return d > COUNTER_MAX_STEP || d < -COUNTER_MAX_STEP ? 0 : d;
}
#define TRIP_SCALE_DEFAULT 185
static int energy_scale = TRIP_SCALE_DEFAULT;     /* percent, from the trip_scale setting: the trip's motor energy only */

/* ---------- the energy sampler: its own thread, from car start to car off, whatever mode is on ----------
   The head unit starts with the car, so "this trip" is this boot. Its state is kept in a file with the boot's id, so
   restarting the service (an update) carries on with the same trip. */
static const char *trip_path = "/data/hmgapi/logs/light-vibes/trip";
static const char *energy_log_path = "/data/hmgapi/logs/light-vibes/energy.log";
static const char *boot_id_path = "/proc/sys/kernel/random/boot_id";
static pthread_mutex_t en_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int have_prev, have_odo;
    long long prev[4];             /* last raw reading of 71, 72, 89, 90 */
    long long mot, gen;            /* this trip, Wh: net energy (71+72, regen already taken off), and regen alone */
    double clim;                   /* Wh of climate power, added up this trip */
    long long odo0, odo;           /* tenths, in the trip unit */
    long long last_t, last_saved, moved_t;
    int ready;                     /* last EV_RDY (flag 44): -1 not known yet */
    double kw; long long kw_t; int kw_ok;
    double kmh; long long kmh_t; int kmh_ok;
    char boot[48];
} en;
static void boot_id(char *out, size_t n) {
    FILE *f = fopen(boot_id_path, "r");
    snprintf(out, n, "?");
    if (f) { char b[64]; if (fgets(b, sizeof b, f)) { b[strcspn(b, "\r\n")] = 0; snprintf(out, n, "%s", b); } fclose(f); }
}
static void trip_save(void) {      /* with en_mu held */
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.tmp", trip_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%s %lld %lld %lld %.0f\n", en.boot, en.odo0, en.mot, en.gen, en.clim);
    fclose(f);
    rename(tmp, trip_path);
}
/* a new trip, unless the file says this boot already has one */
static void trip_begin(void) {
    char b[48], fb[48]; long long odo0, mot, gen; double clim;
    boot_id(b, sizeof b);
    pthread_mutex_lock(&en_mu);
    memset(&en, 0, sizeof en);
    en.ready = -1;
    snprintf(en.boot, sizeof en.boot, "%s", b);
    FILE *f = fopen(trip_path, "r");
    if (f) {
        if (fscanf(f, "%47s %lld %lld %lld %lf", fb, &odo0, &mot, &gen, &clim) == 5 && !strcmp(fb, b) && strcmp(b, "?")) {
            en.odo0 = odo0; en.mot = mot; en.gen = gen; en.clim = clim; en.have_odo = 1; en.odo = odo0;
        }
        fclose(f);
    }
    int resumed = en.have_odo;
    pthread_mutex_unlock(&en_mu);
    if (resumed) say("trip carried on from earlier this drive: %lld Wh motor, %lld Wh regen", mot, gen);
}
static void energy_log(long long t, const long long *raw, long long clim_w, long long odo) {
    struct stat st;
    if (!stat(energy_log_path, &st) && st.st_size > 1024 * 1024) {
        char old[300]; snprintf(old, sizeof old, "%s.1", energy_log_path); rename(energy_log_path, old);
    }
    FILE *f = fopen(energy_log_path, "a");
    if (!f) return;
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm); char ts[24]; strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    fprintf(f, "%s t=%lld odo=%lld m1=%lld m2=%lld g1=%lld g2=%lld clim=%lld | mot=%lld gen=%lld kw=%.1f\n",
            ts, t, odo, raw[0], raw[1], raw[2], raw[3], clim_w, en.mot, en.gen, en.kw_ok ? en.kw : 0.0);
    fclose(f);
}
/* the app shows the car's units: it reads them from here (world-readable, like the reports) */
static const char *units_path = "/data/hmgapi/public/light-vibes/units";
static void units_publish(void) {
    char dir[300], tmp[320]; snprintf(dir, sizeof dir, "%s", units_path);
    char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdir(dir, 0755); chmod(dir, 0755); }
    snprintf(tmp, sizeof tmp, "%s.tmp", units_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%s\n", car_mi ? "mi" : "km");
    fclose(f);
    chmod(tmp, 0644);
    rename(tmp, units_path);
}
/* one sample: the motor values 4 times a second for power; READY, odometer and climate (full) once a second.
   Called by the thread, and straight from the tests. */
static void energy_sample(int full) {
    static const int flags[4] = {71, 72, 89, 90};
    long long raw[4], odo = -1, clim_w = -1, rdy = -1, t = now_ms();
    int ok = 1;
    if (full && (!hev_status(44, &rdy) || (rdy != 0 && rdy != 1))) rdy = -1;
    for (int i = 0; i < 4 && ok; i++) ok = hev_status(flags[i], &raw[i]);
    if (full && !hev_status(31, &odo)) odo = -1;
    if (full && (!hev_status(66, &clim_w) || clim_w < 0 || clim_w > 20000)) clim_w = -1;
    long long tu;
    if (full && hev_status(12, &tu) && (tu == 1 || tu == 2) && car_mi != (tu == 2)) { car_mi = tu == 2; units_publish(); }
    pthread_mutex_lock(&en_mu);
    int changed = 0, new_trip = 0;
    /* the car switched on (READY) again without the head unit restarting: the cluster's Current Drive starts again, so do we */
    if (rdy == 1 && en.ready == 0) {
        en.mot = en.gen = 0; en.clim = 0; en.have_odo = 0; en.last_saved = 0;
        new_trip = 1;
    }
    if (rdy >= 0) en.ready = (int)rdy;
    if (ok) {
        if (en.have_prev) {
            for (int i = 0; i < 4; i++) {
                long long d = i < 2 ? net_step(en.prev[i], raw[i]) : counter_step(en.prev[i], raw[i]);
                if (d) changed = 1;
                if (i < 2) en.mot += d; else en.gen += d;
            }
        }
        memcpy(en.prev, raw, sizeof raw); en.have_prev = 1;
        double kw;
        if (power_from_samples(t, en.mot, &kw)) { en.kw = kw; en.kw_t = t; en.kw_ok = 1; }
    }
    if (odo >= 0) {
        if (!en.have_odo || odo < en.odo0) { en.odo0 = odo; en.have_odo = 1; }
        if (odo != en.odo) changed = 1;
        en.odo = odo;
    }
    if (full) {
        if (clim_w >= 0 && en.last_t) en.clim += clim_w * (t - en.last_t) / 3600000.0;
        en.last_t = t;
    }
    if (changed) en.moved_t = t;
    if (full && en.moved_t && t - en.moved_t < 10000 && ok) energy_log(t, raw, clim_w, odo);
    if (new_trip || t - en.last_saved >= 30000 || (changed && t - en.last_saved >= 5000)) { trip_save(); en.last_saved = t; }
    pthread_mutex_unlock(&en_mu);
    if (new_trip) say("car is READY again: new trip");
}
/* ---------- drive history, for the graphs on the Speed, Power, Efficiency and Night Drive tuning pages ----------
   Every second while the car is on, whatever mode is on: speed, power and both efficiencies, so the app can show what the
   light did, or would have done, with the colours set now. drive_0 is the current or last drive, drive_1 the one before.
   A drive is a trip (one boot, or READY again); one shorter than a minute is replaced rather than kept. Rows are buffered
   and appended every 30 s, so a power cut loses at most that. Each row also goes out on the meter socket as it's taken
   ("hist <row>"), and a client that connects gets "histdrive <start epoch>" and the rows not on disk yet, so the app's
   graph is live without writing to flash every second.
   File: "# drive <boot> <odometer at start> <start epoch>", then "<epoch> <km/h> <kW> <trip mi/kWh> <average mi/kWh>",
   "-" for a reading there wasn't. Public, like the units: the app reads it. */
static int read_power(double *kw);
static int read_trip_eff(double *v, int want_km);
static int read_avg_eff(double *v, int want_km);
#define HISTORY_EVERY_MS 1000
#define HISTORY_FLUSH_ROWS 30
#define HISTORY_MAX_ROWS 28800              /* 8 hours */
#define HISTORY_MIN_ROWS 60                 /* a minute: shorter drives don't push the one before out */
static const char *history_dir = "/data/hmgapi/public/light-vibes";
static pthread_mutex_t hist_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { char key[96]; int rows; long long start; char buf[HISTORY_FLUSH_ROWS * 64 + 64]; int nbuf; long long avg_t; double avg; int avg_ok; } hist;
static void history_path(char *out, size_t n, int which) { snprintf(out, n, "%s/drive_%d.csv", history_dir, which); }
static void history_flush(void) {       /* with hist_mu held */
    if (!hist.nbuf) return;
    char p[320]; history_path(p, sizeof p, 0);
    FILE *f = fopen(p, "a");
    if (f) { fwrite(hist.buf, 1, hist.nbuf, f); fclose(f); chmod(p, 0644); }
    hist.nbuf = 0;
}
/* the file's drive, and how many rows it has: carries on after a service restart mid-drive */
static void history_open(const char *key) {
    char p[320], line[160]; history_path(p, sizeof p, 0);
    FILE *f = fopen(p, "r"); int rows = 0; char fkey[96] = ""; long long fstart = 0;
    if (f) {
        if (fgets(line, sizeof line, f) && !strncmp(line, "# drive ", 8)) {
            char b[64]; long long o; if (sscanf(line + 8, "%63s %lld %lld", b, &o, &fstart) >= 2) snprintf(fkey, sizeof fkey, "%s %lld", b, o);
        }
        while (fgets(line, sizeof line, f)) if (line[0] != '#') rows++;
        fclose(f);
    }
    if (fkey[0] && !strcmp(fkey, key)) { snprintf(hist.key, sizeof hist.key, "%s", key); hist.rows = rows; hist.start = fstart; return; }
    mkdir(history_dir, 0755); chmod(history_dir, 0755);
    if (fkey[0] && rows >= HISTORY_MIN_ROWS) { char p1[320]; history_path(p1, sizeof p1, 1); rename(p, p1); }
    hist.start = time(NULL);
    f = fopen(p, "w");
    if (f) { fprintf(f, "# drive %s %lld\n", key, hist.start); fclose(f); chmod(p, 0644); }
    snprintf(hist.key, sizeof hist.key, "%s", key); hist.rows = 0;
    say("drive history: new drive");
}
static void fmt_val(char *out, size_t n, int ok, double v, const char *f) { if (ok > 0) snprintf(out, n, f, v); else snprintf(out, n, "-"); }
static void history_tick(void) {
    pthread_mutex_lock(&en_mu);
    int ready = en.ready, have = en.have_odo; long long odo0 = en.odo0; char boot[48]; snprintf(boot, sizeof boot, "%s", en.boot);
    pthread_mutex_unlock(&en_mu);
    if (ready == 0 || !have) return;           /* off, or no odometer yet to tell drives apart */
    char key[96]; snprintf(key, sizeof key, "%s %lld", boot, odo0);
    double sp = 0, kw = 0, trip = 0; long long t = now_ms();
    int sp_ok = read_speed(&sp), kw_ok = read_power(&kw), trip_ok = read_trip_eff(&trip, 0);
    if (!hist.avg_t || t - hist.avg_t >= 10000) { hist.avg_ok = read_avg_eff(&hist.avg, 0); hist.avg_t = t; }   /* moves slowly */
    char a[24], b[24], c[24], d[24];
    fmt_val(a, sizeof a, sp_ok, sp, "%.1f"); fmt_val(b, sizeof b, kw_ok, kw, "%.1f");
    fmt_val(c, sizeof c, trip_ok, trip, "%.2f"); fmt_val(d, sizeof d, hist.avg_ok, hist.avg, "%.2f");
    char row[128]; int rn = 0, fresh = 0; long long start;
    pthread_mutex_lock(&hist_mu);
    if (strcmp(hist.key, key)) { history_flush(); history_open(key); fresh = 1; }
    start = hist.start;
    if (hist.rows < HISTORY_MAX_ROWS) {
        rn = snprintf(row, sizeof row, "%lld %s %s %s %s\n", (long long)time(NULL), a, b, c, d);
        if (hist.nbuf > (int)sizeof hist.buf - rn - 1) history_flush();
        memcpy(hist.buf + hist.nbuf, row, rn); hist.nbuf += rn;
        hist.rows++;
        if (hist.nbuf >= HISTORY_FLUSH_ROWS * 40) history_flush();
    }
    pthread_mutex_unlock(&hist_mu);
    char m[160]; int mn;
    if (fresh) { mn = snprintf(m, sizeof m, "histdrive %lld\n", start); meter_broadcast(m, mn); }
    if (rn) { mn = snprintf(m, sizeof m, "hist %s", row); meter_broadcast(m, mn); }
}
/* a new meter client: which drive, and the rows not written to the file yet (the app reads the rest from it) */
static void history_hello(int fd) {
    char out[sizeof hist.buf + 2 * HISTORY_FLUSH_ROWS * 8 + 64]; int n = 0;
    pthread_mutex_lock(&hist_mu);
    if (hist.key[0]) {
        n = snprintf(out, sizeof out, "histdrive %lld\n", hist.start);
        for (int i = 0; i < hist.nbuf && n < (int)sizeof out - 8; ) {
            const char *nl = memchr(hist.buf + i, '\n', hist.nbuf - i);
            int len = nl ? (int)(nl - (hist.buf + i)) + 1 : hist.nbuf - i;
            if (n + 5 + len >= (int)sizeof out) break;
            memcpy(out + n, "hist ", 5); n += 5;
            memcpy(out + n, hist.buf + i, len); n += len;
            i += len;
        }
    }
    pthread_mutex_unlock(&hist_mu);
    if (n) send(fd, out, n, MSG_DONTWAIT | MSG_NOSIGNAL);
}
static void history_stop(void) { pthread_mutex_lock(&hist_mu); history_flush(); pthread_mutex_unlock(&hist_mu); }

static void *energy_thread(void *arg) {
    (void)arg;
    long long last_hist = 0;
    for (int n = 0; ; n++) {
        long long t0 = now_ms();
        energy_sample(n % (1000 / ENERGY_TICK_MS) == 0);
        if (t0 - last_hist >= HISTORY_EVERY_MS) { last_hist = t0; history_tick(); }
        if (n % 2 == 0 && C.enabled && !strcmp(C.mode, "nightdrive") && !strcmp(C.nd_source, "speed")) {
            double sp = 0;
            if (read_speed(&sp)) {
                pthread_mutex_lock(&en_mu);
                en.kmh = sp; en.kmh_t = now_ms(); en.kmh_ok = 1;
                pthread_mutex_unlock(&en_mu);
            }
        }
        long long left = ENERGY_TICK_MS - (now_ms() - t0);
        if (left > 0) usleep(left * 1000);
    }
    return NULL;
}
static void energy_start(void) {
    trip_begin();
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 * 1024);
    if (pthread_create(&th, &at, energy_thread, NULL)) say("energy sampler didn't start: power and trip modes won't work");
}
static int read_power(double *kw) {
    pthread_mutex_lock(&en_mu);
    int ok = en.kw_ok && now_ms() - en.kw_t < 5000;
    if (ok) *kw = en.kw;
    pthread_mutex_unlock(&en_mu);
    return ok;
}
static int read_cached_speed(double *kmh) {
    pthread_mutex_lock(&en_mu);
    int ok = en.kmh_ok && now_ms() - en.kmh_t < 5000;
    if (ok) *kmh = en.kmh;
    pthread_mutex_unlock(&en_mu);
    return ok;
}
/* the car's average, in the unit asked for; tenths in, value out */
static double eff_convert(long long unit, long long tenths, int want_km) {
    double v = tenths / 10.0, mi;
    if (unit == 1) mi = v > 0 ? 100.0 / (v * KMH_PER_MPH) : 0;   /* kWh/100 km: 244 with the EV6 in km, 2026-11-07 */
    else if (unit == 3) mi = v / KMH_PER_MPH;          /* km/kWh: a guess, not seen */
    else mi = v;                                        /* 2: mi/kWh */
    return want_km ? mi * KMH_PER_MPH : mi;
}
/* this trip: distance since the car started over the energy used since (motors less regen, plus climate) */
static int read_trip_eff(double *v, int want_km) {
    pthread_mutex_lock(&en_mu);
    int have = en.have_odo;
    long long tenths = en.odo - en.odo0;
    double kwh = (en.mot * energy_scale / 100.0 + en.clim) / 1000.0;
    pthread_mutex_unlock(&en_mu);
    if (!have) return 0;
    if (tenths < TRIP_MIN_TENTHS || kwh <= 0.05) return -1;   /* not far enough yet */
    double mi = tenths / 10.0 / KMH_PER_MPH;
    *v = (want_km ? mi * KMH_PER_MPH : mi) / kwh;
    return *v > 0 && *v < 30;
}
static int read_avg_eff(double *v, int want_km) {
    long long unit, tenths;
    if (!hev_status(1, &unit) || !hev_status(2, &tenths) || tenths <= 0 || tenths >= 1000) return 0;   /* 1023 / 255: no value */
    *v = eff_convert(unit, tenths, want_km);
    return 1;
}

/* one step of speed, power or efficiency mode: read, pick the colour, send it if it changed */
typedef struct { long long last_ok, last_sent, last_read; int sent; double value; int have; char bright[16]; } GaugeDrv;
/* speed, power and efficiency set the brightness themselves: a level 1-10 (or "own": leave the owner's), and the car's
   "dimmed in dark" on or off. Dim is SML setting 8: 1 off, 2 on; the lamp reports it in its status, and it goes back
   with the colour. */
static void gauge_brightness(void) {
    wait_pending();
    int want_dim = C.gauge_dim ? 2 : 1;
    if (want_dim != base.dim || we_set_dim) { light_config(8, want_dim, 1); we_set_dim = 1; }
    if (!strcmp(C.gauge_bright, "own")) { if (base.valid) light_bright_raw(base.bright_raw, 1); }
    else light_bright_raw(clampi(atoi(C.gauge_bright), 1, 10) + 1, 1);
}
static void gauge_step(GaugeDrv *g, int *active) {
    long long t = now_ms();
    double v = 0; int ok, idx;
    const char *what = C.mode;
    if (!strcmp(C.mode, "speed")) ok = read_speed(&v);
    else if (!strcmp(C.mode, "power")) ok = read_power(&v);
    else {
        int km = eff_in_km();
        if (g->have && t - g->last_read < 5000 && strcmp(C.eff_source, "trip")) ok = 1, v = g->value;   /* the average moves slowly */
        else ok = !strcmp(C.eff_source, "trip") ? read_trip_eff(&v, km) : read_avg_eff(&v, km);
        if (ok > 0) { g->last_read = t; g->value = v; g->have = 1; }
    }
    if (ok < 0) {                     /* the trip has no average yet: show the owner's own colour meanwhile, not a stale one */
        if (*active) { say("trip average not ready yet (under %.1f km) — own colour until then", TRIP_MIN_TENTHS / 10.0); restore_base(); *active = 0; base.valid = 0; }
        meter_broadcast("eff wait -1\n", 12);
        return;
    }
    if (!ok) {
        if (*active && t - g->last_ok > GAUGE_LOST_MS) {
            say("no %s reading for %d s — lights back to normal", what, GAUGE_LOST_MS / 1000);
            restore_base(); *active = 0; base.valid = 0;
        }
        return;
    }
    g->last_ok = t;
    if (!*active) {
        snapshot_base();
        take_over_lamp();
        *active = 1; g->sent = -1; g->bright[0] = 0;
        say("%s mode — lights following it (%.1f now)", what, v);
    }
    char want_bright[16]; snprintf(want_bright, sizeof want_bright, "%s/%d", C.gauge_bright, C.gauge_dim);
    if (strcmp(g->bright, want_bright)) {
        gauge_brightness();
        snprintf(g->bright, sizeof g->bright, "%s", want_bright);
        say("%s mode brightness %s, dimmed in dark %s", what, C.gauge_bright, C.gauge_dim ? "on" : "off");
    }
    if (!strcmp(C.mode, "speed")) idx = speed_wheel_index(v);
    else if (!strcmp(C.mode, "power")) idx = power_wheel_index(v);
    else idx = wheel_walk(v, C.eff_low / 10.0, C.eff_high / 10.0, C.eff_color_low, C.eff_color_high);
    check_tick();
    if (idx != g->sent && !pending_pid && t - g->last_sent >= C.min_interval_ms) {
        light_rgb(WHEEL_R[idx], WHEEL_G[idx], WHEEL_B[idx], 0);
        g->sent = idx; g->last_sent = t;
        if (C.debug) say("%s %.2f: colour #%d", what, v, idx);
    }
    char b[64];
    int n = snprintf(b, sizeof b, "%s %.2f %d\n", !strcmp(C.mode, "speed") ? "speed" : !strcmp(C.mode, "power") ? "power" : "eff",
                     !strcmp(C.mode, "speed") && !speed_in_kmh() ? v / KMH_PER_MPH : v, idx);   /* speed in the unit shown */
    meter_broadcast(b, n);
}
static void nightdrive_idle_step(GaugeDrv *g, int *active) {
    long long t = now_ms();
    double v = 0;
    int by_speed = !strcmp(C.nd_source, "speed");
    int ok = by_speed ? read_cached_speed(&v) : read_power(&v);
    if (!ok) {
        if (*active && g->last_ok > 0 && t - g->last_ok > GAUGE_LOST_MS) {
            say("no %s reading for %d s — lights back to normal", C.nd_source, GAUGE_LOST_MS / 1000);
            restore_base(); *active = 0; base.valid = 0;
        }
        return;
    }
    g->last_ok = t;
    if (!*active) {
        if (!base.valid) snapshot_base();
        take_over_lamp();
        music_dim();
        *active = 1; g->sent = -1; g->bright[0] = 0;
        say("nightdrive cruise — lights following %s (%.1f now)", C.nd_source, v);
    }
    int cruise = clampi(C.bright_min + (C.bright_max - C.bright_min) / 4, C.bright_min > 0 ? C.bright_min : 1, C.bright_max);
    char want_bright[16]; snprintf(want_bright, sizeof want_bright, "nd:%d/%d", cruise, C.gauge_dim);
    if (strcmp(g->bright, want_bright)) {
        music_dim();
        wait_pending();
        light_bright_raw(cruise + 1, 1);
        snprintf(g->bright, sizeof g->bright, "%s", want_bright);
    }
    int idx = by_speed ? speed_wheel_index(v) : power_wheel_index(v);
    check_tick();
    if (idx != g->sent && !pending_pid && t - g->last_sent >= C.min_interval_ms) {
        light_rgb(WHEEL_R[idx], WHEEL_G[idx], WHEEL_B[idx], 0);
        g->sent = idx; g->last_sent = t;
    }
    char b[64];
    int n = snprintf(b, sizeof b, "%s %.2f %d\n", by_speed ? "speed" : "power",
                     by_speed && !speed_in_kmh() ? v / KMH_PER_MPH : v, idx);
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
    int s[7], lamp[7];
    if (!saved_load(s) || !s[5]) {     /* clean: note the user's colour now */
        snapshot_base(); base.valid = 0;
        return;
    }
    base_from(s);
    we_set_dim = base.dim >= 1;      /* a gauge mode may have changed it */
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
static void pcm_drain_backlog(struct pcm *pcm, short *buf) {
    if (tune.active) return;   /* keep sample-exact continuity during a tuning WAV recording */
    for (int k = 0; k < 16 && pcm_wait(pcm, 0) > 0; k++) {
        if (pcm_readi(pcm, buf, PERIOD) < 0) break;
    }
}
static int analyze_file(const char *path) {
    C.dry = 1;
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 1; }
    int prev_rate = rate;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) == 12 && !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4)) {
        unsigned char chdr[8];
        while (fread(chdr, 1, 8, f) == 8) {
            unsigned int csz = (unsigned int)chdr[4] | ((unsigned int)chdr[5] << 8) | ((unsigned int)chdr[6] << 16) | ((unsigned int)chdr[7] << 24);
            if (!memcmp(chdr, "fmt ", 4) && csz >= 16) {
                unsigned char fmt[16];
                if (fread(fmt, 1, 16, f) != 16) break;
                int srate = (int)((unsigned int)fmt[4] | ((unsigned int)fmt[5] << 8) | ((unsigned int)fmt[6] << 16) | ((unsigned int)fmt[7] << 24));
                if (srate == 44100 || srate == 48000) rate = srate;
                if (csz > 16) fseek(f, (long)(csz - 16 + (csz & 1)), SEEK_CUR);
            } else if (!memcmp(chdr, "data", 4)) {
                break;
            } else {
                fseek(f, (long)(csz + (csz & 1)), SEEK_CUR);
            }
        }
    } else {
        fseek(f, 0, SEEK_SET);
    }
    Ana a; ana_init(&a); Drv d; memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
    Meter m; memset(&m, 0, sizeof m);
    short buf[PERIOD * CH]; long long nblk = 0, active_blks = 0, peak_blks = 0, t = 0; int beats = 0;
    show_config();
    while (fread(buf, sizeof(short) * CH, PERIOD, f) == PERIOD) {
        t = nblk * 1000LL * PERIOD / rate;
        Frame fr = ana_block(&a, buf, PERIOD, t);
        if (fr.beat) { beats++; if (C.debug >= 1) say("t=%6lld ms BEAT ratio=%.2f", t, fr.beat_ratio); }
        meter(&m, &a, fr, t);
        drive(&d, &a, fr, t);
        if (fr.db > C.silence_db) {
            active_blks++;
            if (d.want_level >= C.bright_max) peak_blks++;
        }
        nblk++;
    }
    t = nblk * 1000LL * PERIOD / rate;
    fclose(f);
    double duty = active_blks > 0 ? 100.0 * (double)peak_blks / (double)active_blks : 0.0;
    if (a.bpm_conf >= 0.28 && a.bpm > 50.0)
        say("analyzed %lld ms (%d Hz), %d beats, %.1f BPM (conf %.0f%%), peak_duty=%.1f%%", t, rate, beats, a.bpm, a.bpm_conf * 100, duty);
    else
        say("analyzed %lld ms (%d Hz), %d beats, peak_duty=%.1f%%", t, rate, beats, duty);
    rate = prev_rate;
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
        return log_tail(stdout, argc >= 3 ? atoi(argv[2]) : 40, argc >= 4 ? argv[3] : NULL);
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
    if (argc < 2 || argv[1][0] == '-') { fprintf(stderr, "usage: lightvibesd <state-dir> [fallback-dir] | --status | --log [lines] [filter] | --stop-all | --off | --testcall R G B [level] | --analyze file.wav|file.raw [dir]\n"); return 2; }

    settings_dir = argv[1];
    if (argc >= 3) fallback_dir = argv[2];
    diag_set_dir(settings_dir);
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
    if (C.enabled) original_record(); else original_restore();
    int was_enabled = C.enabled;

    short *buf = malloc(sizeof(short) * PERIOD * CH);
    int lights_active = 0;
    long long stamp = config_stamp();
    long long last_cfg = 0;
    int tapped = 0;
    GaugeDrv gd; memset(&gd, 0, sizeof gd);
    char gd_mode[32] = "";
    energy_start();

    while (!stop_flag) {
        meter_accept();
        reap();
        if (diag_poll()) {
            if (lights_active) { restore_base(); lights_active = 0; base.valid = 0; }
            diag_dispatch();
            continue;
        }
        long long s = config_stamp();
        if (s != stamp) { stamp = s; load_config(active_dir()); say("settings changed"); show_config(); pulse_car_colour(lights_active); }
        if (lights_active && (!C.enabled || (!gauge_mode() && strcmp(C.mode, "nightdrive")))) {   /* left speed, power, efficiency or nightdrive mode */
            restore_base(); lights_active = 0; base.valid = 0;
        }
        if (C.enabled != was_enabled) {
            was_enabled = C.enabled;
            if (C.enabled) original_record(); else original_restore();
        }
        if (!C.enabled) {
            if (tapped) { set_loopback(0); tapped = 0; say("switched off in settings"); }
            usleep(500 * 1000); continue;
        }
        if (gauge_mode()) {
            if (tapped) { set_loopback(0); tapped = 0; }
            char want[32]; snprintf(want, sizeof want, "%s/%s", C.mode, C.eff_source);
            if (strcmp(gd_mode, want)) { snprintf(gd_mode, sizeof gd_mode, "%s", want); gd.have = 0; gd.sent = -1; gd.last_sent = 0; pw_n = 0; }
            energy_scale = clampi(cfg_int("trip_scale", TRIP_SCALE_DEFAULT), 10, 1000);
            gauge_step(&gd, &lights_active);
            usleep((!strcmp(C.mode, "speed") ? SPEED_POLL_MS : !strcmp(C.mode, "power") ? ENERGY_TICK_MS : GAUGE_POLL_MS) * 1000); continue;
        }
        if (!tapped) {
            if (set_loopback(1)) { say("cannot enable loopback; exiting"); return 1; }
            tapped = 1;
        }
        int playing = music_rate();
        if (!playing) {
            if (!strcmp(C.mode, "nightdrive")) {
                char want[32]; snprintf(want, sizeof want, "nightdrive/%s", C.nd_source);
                if (strcmp(gd_mode, want)) { snprintf(gd_mode, sizeof gd_mode, "%s", want); gd.sent = -1; gd.last_sent = 0; gd.bright[0] = 0; }
                nightdrive_idle_step(&gd, &lights_active);
            }
            usleep(150 * 1000); continue;     /* also how soon a setting change is seen */
        }
        rate = playing;

        struct pcm_config cfg; memset(&cfg, 0, sizeof cfg);
        cfg.channels = CH; cfg.rate = rate; cfg.period_size = PERIOD; cfg.period_count = 8;
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

        totals.sessions++;
        int sess_colours = totals.colours, sess_brights = totals.brights;
        double sess_peak = -200;
        Ana a; ana_init(&a); Drv d; memset(&d, 0, sizeof d); d.sent_idx = d.sent_level = -1; d.idx = 22;
        Meter mtr; memset(&mtr, 0, sizeof mtr);
        MeterStream ms; memset(&ms, 0, sizeof ms);
        int zero_blocks = 0, stalls = 0, xruns = 0;
        long long t0 = now_ms(), quiet_since = -1, sound_since = -1, last_check = 0, last_fix = -100000, last_nd_idle = -1000;
        tune.in_pcm = 1;
        if (tune.req_sec > 0 && !tune.active) tune_start(rate);
        while (!stop_flag) {
            long long t = now_ms() - t0;
            if (t - last_check >= 80) {           /* Siri / call / source change: let go fast */
                last_check = t;
                if (music_rate() != rate) { say("playback stopped or changed rate — releasing capture"); break; }
            }
            if (t - last_cfg >= 150 || t < last_cfg) {   /* live settings: a colour picked mid-song shows at once */
                last_cfg = t;
                if (diag_poll()) break;
                long long s2 = config_stamp();
                if (s2 != stamp) {
                    stamp = s2; load_config(active_dir()); say("settings changed"); show_config();
                    if (!C.enabled || gauge_mode()) break;
                    ana_init(&a);                 /* bands and beat settings may have changed */
                    d.sent_idx = d.sent_level = -1;
                    gd.sent = -1; gd.bright[0] = 0;
                    if (lights_active) music_dim();
                    if (lights_active && !strcmp(C.mode, "pulse")) {   /* pulse's colour may have changed */
                        pulse_car_colour(1);
                        wait_pending();
                        drive_link_sync();      /* to or from the drive-mode colour */
                        if (C.pulse_color >= 0) light_rgb(WHEEL_R[C.pulse_color], WHEEL_G[C.pulse_color], WHEEL_B[C.pulse_color], 1);
                        else if (C.pulse_color == -1 && base.valid) light_rgb(base.r, base.g, base.b, 1);
                    }   /* pulse keeps the user's colour */
                    pcm_drain_backlog(pcm, buf);  /* discard audio queued while synchronous IPC/disk writes ran */
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
            if (tune.active) tune_write_pcm(buf, PERIOD);
            int nz = 0; for (int i = 0; i < PERIOD * CH; i++) if (buf[i]) { nz = 1; break; }
            if (!nz) {
                if (++zero_blocks * BLOCK_MS >= 1000 && t - last_fix >= 5000) {   /* ~1 s of digital zeros */
                    say("capture is all zeros while music plays — re-enabling loopback");
                    set_loopback(1); last_fix = t; zero_blocks = 0;
                }
            } else zero_blocks = 0;

            Frame f = ana_block(&a, buf, PERIOD, t);
            /* catch up immediately if scheduler jitter queued extra periods, keeping phase continuous and preserving beats */
            for (int k = 0; k < 8 && pcm_wait(pcm, 0) > 0; k++) {
                if (pcm_readi(pcm, buf, PERIOD) < 0) break;
                if (tune.active) tune_write_pcm(buf, PERIOD);
                t = now_ms() - t0;
                Frame fn = ana_block(&a, buf, PERIOD, t);
                if (fn.beat) f.beat = 1;
                if (fn.beat_ratio > f.beat_ratio) f.beat_ratio = fn.beat_ratio;
                if (fn.onset_ratio > f.onset_ratio) f.onset_ratio = fn.onset_ratio;
                if (fn.db > f.db) f.db = fn.db;
                f.band = fn.band;
                f.mid_norm = fn.mid_norm;
                f.pitch_norm = fn.pitch_norm;
                if (fn.spark_norm > f.spark_norm) f.spark_norm = fn.spark_norm;
                memcpy(f.band_db, fn.band_db, sizeof f.band_db);
            }
            meter(&mtr, &a, f, t);
            meter_stream(&ms, &a, f, t);
            if (f.db > sess_peak) sess_peak = f.db;
            if (f.db > totals.peak_db) totals.peak_db = f.db;
            if (f.db > C.silence_db) {
                quiet_since = -1;
                if (sound_since < 0) sound_since = t;
                if (!lights_active && rate != MAX_RATE && t - sound_since < SOURCE_START_MS) {
                    if (tune.active) { tune_write_frame(t, &a, &f, &d); if (!tune.active) pcm_drain_backlog(pcm, buf); }
                    continue;
                }
                if (!lights_active) {
                    if (!base.valid) snapshot_base();
                    take_over_lamp();
                    music_dim();
                    pcm_drain_backlog(pcm, buf);  /* start locked to live speaker output, not buffered takeover audio */
                    t = now_ms() - t0;
                    lights_active = 1; say("music started — lights following (%s)", C.mode);
                }
                gd.sent = -1; gd.bright[0] = 0;
                drive(&d, &a, f, t);
                check_tick();
            } else if (!lights_active) {
                sound_since = -1;
                if (!strcmp(C.mode, "nightdrive") && t - last_nd_idle >= 250) {
                    last_nd_idle = t;
                    nightdrive_idle_step(&gd, &lights_active);
                }
            } else {
                sound_since = -1;
                if (quiet_since < 0) quiet_since = t;
                if (t - quiet_since >= C.idle_seconds * 1000LL) {
                    if (!strcmp(C.mode, "nightdrive")) {
                        d.sent_idx = d.sent_level = -1;
                        if (t - last_nd_idle >= 250) {
                            last_nd_idle = t;
                            nightdrive_idle_step(&gd, &lights_active);
                        }
                    } else {
                        restore_base(); lights_active = 0; base.valid = 0; say("music quiet — lights back to normal");
                    }
                }
            }
            if (tune.active) { tune_write_frame(t, &a, &f, &d); if (!tune.active) pcm_drain_backlog(pcm, buf); }
        }
        tune.in_pcm = 0;
        if (tune.active) tune_finish("playback_stopped");
        pcm_close(pcm);
        /* outside debug too: "music played but nothing was sent" is the first thing a report needs to show */
        if (sess_peak > -199) say("session: loudest %.0f dB (silence below %d), %d colour and %d brightness commands", sess_peak, C.silence_db, totals.colours - sess_colours, totals.brights - sess_brights);
        if (lights_active && (strcmp(C.mode, "nightdrive") || !C.enabled || stop_flag || diag_pending)) {
            restore_base(); lights_active = 0; base.valid = 0;
        } else if (lights_active) {
            gd.sent = -1; gd.bright[0] = 0;
        }
        if (!stop_flag) usleep(300 * 1000);
    }

    say("stopping");
    if (tune.active || tune.req_sec > 0) tune_finish("daemon_stopped");
    /* HMG-API's stop reaches the helper too (same process group): the putting back goes through service call */
    helper_stop(); helper_failed = 1;
    history_stop();
    if (lights_active) restore_base();
    original_restore();
    set_loopback(0);
    return 0;
}
