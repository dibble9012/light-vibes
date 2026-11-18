package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Settings for lightvibesd: pick a preset, change its colours, save your own.
 *
 * lightvibesd (root) watches files/settings/, one value per file, and applies changes within
 * half a second. Those files are the single source of truth for the current settings; "active"
 * only remembers which preset they came from. Saving over a built-in preset keeps a copy in
 * files/overrides/, which Reset removes; the shipped one in assets is never changed. Deleting a
 * built-in preset leaves a marker in files/hidden/, and restoring them removes the markers.
 */
public class MainActivity extends Activity {
    /* factory 64-colour custom wheels + Hyundai Ioniq 5 0x01xx stock mood colours (64..73), matching lightvibesd.c */
    private static final int[] KIA_R = {16,16,16,33,47,64,82,100,120,136,155,169,183,193,204,213,221,228,235,239,246,250,254,255,255,255,255,255,255,255,255,255,255,255,254,255,246,233,220,202,166,168,149,132,117,104,93,81,71,62,53,44,37,30,25,20,16,16,16,16,16,16,16,16};
    private static final int[] KIA_G = {67,46,32,28,28,28,28,28,28,29,33,35,35,33,31,27,23,19,14,10,6,4,1,50,68,80,105,120,143,166,195,205,218,232,243,255,255,255,255,252,226,243,235,229,223,221,222,225,231,238,245,250,250,250,250,250,244,230,210,187,163,138,111,89};
    private static final int[] KIA_B = {244,242,239,239,239,239,239,239,239,239,239,239,234,224,210,192,174,153,153,116,96,81,69,29,35,39,48,53,62,70,88,118,158,200,230,255,246,221,188,147,91,98,98,98,98,98,105,118,133,152,169,189,207,225,237,249,252,252,252,252,252,252,252,248};

    private static final int[] HYUNDAI_R = {
        176,195,205,232,255,255,255,255,255,255,255,255,254,255,254,254,255,255,255,255,255,255,255,254,255,255,255,255,255,255,255,255,255,254,255,254,247,230,210,175,119,58,10,0,0,0,0,0,0,0,0,0,25,55,81,100,109,122,125,128,130,143,156,167,
        229,244,183,24,3,126,254,233,187,138
    };
    private static final int[] HYUNDAI_G = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,107,73,88,113,125,3,34,48,60,65,137,148,160,172,177,191,207,216,237,255,254,255,254,254,255,255,217,180,161,140,125,115,99,85,62,0,0,0,0,42,52,25,0,0,0,0,0,
        242,233,228,142,199,215,168,62,15,37
    };
    private static final int[] HYUNDAI_B = {
        255,254,254,254,241,195,158,137,117,111,106,101,95,72,58,30,106,116,73,88,113,115,10,0,23,42,49,107,119,111,113,108,123,138,147,165,182,186,188,191,196,198,235,245,255,255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        250,223,252,253,192,21,17,62,236,252
    };

    private static int[] WHEEL_R = KIA_R, WHEEL_G = KIA_G, WHEEL_B = KIA_B;

    private static final String DEFAULT_BANDS = "40-130:#FF0020:1.6 130-450:#FF3C0F:2.0 450-2000:#00FF40:3.2 2000-6000:#00AAFF:9 6000-16000:#9100FF:32";
    private static final String[] BUILT_IN = {
        "synesthesia", "phasepulse", "drift", "duet", "ember", "nightdrive",
        "lively", "pulse3", "epic", "glow",
        "speed", "power", "efficiency"
    };
    private static final String FIRST_RUN_PRESET = "b:synesthesia";
    private static final double[] FULL_WHEEL_HUES = {358.0, 38.0, 88.0, 178.0, 209.0, 246.0, 268.0, 287.0, 314.0, 336.0};
    private static final double[][] DRIFT_THEMES = {
        {92.0, 178.0, 209.0, 268.0, 145.0},
        {186.0, 236.0, 290.0, 330.0, 356.0},
        {40.0, 16.0, 356.0, 316.0, 270.0},
        {176.0, 192.0, 209.0, 232.0, 256.0},
        {358.0, 38.0, 88.0, 209.0, 287.0}
    };
    private static final int MAX_BANDS = 8;      // what fits on screen; lightvibesd takes up to 12
    private static final int MIN_SPLIT_HZ = 60;  // each half keeps at least one FFT bin (23.4 Hz each)

    private static boolean isBuiltIn(String name) {
        for (String b : BUILT_IN) if (b.equals(name)) return true;
        return false;
    }


    private File settingsDir, presetsDir, overridesDir, hiddenDir, activeFile;
    private Map<String, String> cfg = new LinkedHashMap<String, String>();
    private String active = FIRST_RUN_PRESET;   // "b:<asset name>" or "c:<custom name>"
    private boolean modified;
    private int selectedBand;
    private static final int TAB_COLOR = 0, TAB_BRIGHTNESS = 1, TAB_TUNING = 2;
    private static int tab = TAB_COLOR;   // static: kept when Android restarts the activity for day/night
    private int speedEnd;          // the speed, power or efficiency page's selected end: 0 low, 1 high
    private int duetEnd;           // duet's selected card: 0 melody bed, 1 beat accent
    private TextView speedNow;
    private TextView tuneButton, tuneStatus;
    private boolean tuneActive;
    private long tuneRequestedAt;
    private ReportPage reportPage;  // on screen instead of the editor while set
    private ScrollView rightScroll;

    private LinearLayout presetList, presetActions, editorTop, editor;
    private MeterView meter;
    private volatile Thread meterThread;
    private boolean resumed;
    private final Handler handler = new Handler();

    private static final class Band {
        String lo, hi, colour, boost;

        static Band parse(String token) {
            String[] p = token.split(":");
            String[] r = p[0].split("-");
            if (p.length < 2 || r.length != 2) return null;
            Band b = new Band();
            b.lo = r[0];
            b.hi = r[1];
            b.colour = p[1];
            b.boost = p.length > 2 ? p[2] : null;
            return b;
        }

        @Override public String toString() {
            return lo + "-" + hi + ":" + colour + (boost != null ? ":" + boost : "");
        }

        int wheelIndex() {
            return colorIndexOf(colour, 0, 0);
        }

        int rgb() {
            return wheelColour(wheelIndex());
        }

        String title() {
            double mid = Math.sqrt(intOf(lo, 1) * (double) intOf(hi, 1));
            if (mid < 150) return "Bass";
            if (mid < 600) return "Low mids";
            if (mid < 2000) return "Mids";
            if (mid < 6000) return "Highs";
            return "Air";
        }

        String range() {
            return hz(lo) + "–" + hz(hi);
        }

        private static String hz(String v) {
            int f = intOf(v, 0);
            return f >= 1000 ? (f % 1000 == 0 ? (f / 1000) + " kHz" : (f / 100) / 10.0 + " kHz") : f + " Hz";
        }
    }

    /** Black or white, whichever reads better on a colour (by its perceived brightness). */
    private static int readableOn(int c) {
        double y = 0.299 * Color.red(c) + 0.587 * Color.green(c) + 0.114 * Color.blue(c);
        return y > 150 ? Color.BLACK : Color.WHITE;
    }

    private static double rgbHueDeg(int r, int g, int b) {
        r &= 0xff; g &= 0xff; b &= 0xff;
        int mx = Math.max(r, Math.max(g, b));
        int mn = Math.min(r, Math.min(g, b));
        int d = mx - mn;
        if (d <= 0) return 0.0;
        double h;
        if (mx == r) h = 60.0 * ((g - b) / (double) d);
        else if (mx == g) h = 60.0 * (2.0 + (b - r) / (double) d);
        else h = 60.0 * (4.0 + (r - g) / (double) d);
        if (h < 0.0) h += 360.0;
        return h;
    }

    private static double wheelHue(int idx) {
        if (idx < 0 || idx >= WHEEL_R.length) idx = 0;
        return rgbHueDeg(WHEEL_R[idx], WHEEL_G[idx], WHEEL_B[idx]);
    }

    private static boolean vividWheel(int i) {
        if (i < 0 || i >= WHEEL_R.length) return false;
        int r = WHEEL_R[i], g = WHEEL_G[i], b = WHEEL_B[i];
        int mx = Math.max(r, Math.max(g, b));
        int mn = Math.min(r, Math.min(g, b));
        int minSat = (g >= r && g >= b && g - r >= 55 && g - b >= 75) ? 52 : 78;
        return mx > 0 && (mx - mn) * 100 >= minSat * mx;
    }

    private static int nearestVividWheel(int r, int g, int b) {
        int best = 0, bestD = Integer.MAX_VALUE;
        for (int i = 0; i < WHEEL_R.length; i++) {
            if (!vividWheel(i)) continue;
            int dr = WHEEL_R[i] - r, dg = WHEEL_G[i] - g, db = WHEEL_B[i] - b;
            int d = dr * dr + dg * dg + db * db;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    private static int vividAtHue(double deg) {
        deg = ((deg % 360.0) + 360.0) % 360.0;
        int best = 0;
        double bestD = 999.0;
        for (int i = 0; i < WHEEL_R.length; i++) {
            if (!vividWheel(i)) continue;
            double h = wheelHue(i);
            double d = Math.abs(h - deg);
            if (d > 180.0) d = 360.0 - d;
            // match lightvibesd.c's hue-sorted vivid_ring tie-breaking
            if (d < bestD - 1e-6 || (Math.abs(d - bestD) <= 1e-6 && h < wheelHue(best))) {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    private static double hueLerp(double a, double b, double t) {
        double d = b - a;
        if (d > 180.0) d -= 360.0;
        if (d < -180.0) d += 360.0;
        return ((a + d * t) % 360.0 + 360.0) % 360.0;
    }

    private static int colorIndexOf(String s, int def, int minSpecial) {
        if (s == null) return def;
        s = s.trim();
        if (s.isEmpty()) return def;
        if (s.startsWith("#") && s.length() == 7) {
            try {
                int hex = Integer.parseInt(s.substring(1), 16);
                return nearestVividWheel((hex >> 16) & 255, (hex >> 8) & 255, hex & 255);
            } catch (NumberFormatException e) {
                return def;
            }
        }
        int v = intOf(s, def);
        if (v < minSpecial) return minSpecial;
        if (v >= WHEEL_R.length) {
            if (v >= 0 && v < HYUNDAI_R.length) return nearestVividWheel(HYUNDAI_R[v], HYUNDAI_G[v], HYUNDAI_B[v]);
            return WHEEL_R.length - 1;
        }
        return v;
    }

    private static int wheelColour(int i) {
        if (i < 0 || i >= WHEEL_R.length) i = 0;
        return Color.rgb(WHEEL_R[i], WHEEL_G[i], WHEEL_B[i]);
    }

    private static int intOf(String s, int def) {
        if (s == null) return def;
        try { return Integer.parseInt(s.trim()); } catch (RuntimeException e) { return def; }
    }

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    // ---------- lifecycle ----------

    @Override protected void onCreate(Bundle saved) {
        Ui.apply(this);     // the head unit's day or night theme
        setTheme(Ui.night ? android.R.style.Theme_Holo_NoActionBar : android.R.style.Theme_Holo_Light_NoActionBar);
        super.onCreate(saved);
        settingsDir = new File(getFilesDir(), "settings");
        presetsDir = new File(getFilesDir(), "presets");
        activeFile = new File(getFilesDir(), "active");
        overridesDir = new File(getFilesDir(), "overrides");
        hiddenDir = new File(getFilesDir(), "hidden");
        presetsDir.mkdirs();
        overridesDir.mkdirs();
        hiddenDir.mkdirs();
        loadCurrent();
        buildScreen();
    }

    @Override protected void onResume() {
        super.onResume();
        // the settings files are the truth: they may have changed while this screen was away (another copy of it,
        // loadpreset.sh, a restart), so show what's there now rather than what was loaded earlier
        if (reportPage == null) {
            loadCurrent();
            refreshPresets();
            refreshEditor();
        }
        resumed = true;
        syncMeter();
        if (reportPage != null) reportPage.resume();
    }

    @Override protected void onPause() {
        resumed = false;
        syncMeter();
        if (reportPage != null) reportPage.stop();
        super.onPause();
    }

    // ---------- settings files ----------

    private void loadCurrent() {
        settingsDir.mkdirs();
        updatePalette();
        String[] hNames = hiddenDir.list();
        if (hNames != null) for (String n : hNames) if (!isBuiltIn(n)) new File(hiddenDir, n).delete();
        String[] oNames = overridesDir.list();
        if (oNames != null) {
            for (String n : oNames) {
                if (n.endsWith(".conf") && !isBuiltIn(n.substring(0, n.length() - 5))) new File(overridesDir, n).delete();
            }
        }
        cfg.clear();
        String[] names = settingsDir.list();
        if (names != null) {
            Arrays.sort(names);
            for (String n : names) {
                if (n.startsWith(".") || isSwitchFile(n)) continue;
                String v = readFirstLine(new File(settingsDir, n));
                if (v != null) cfg.put(n, v);
            }
        }
        String a = readFirstLine(activeFile);
        if (a != null && a.length() > 2) {
            active = a.endsWith("*") ? a.substring(0, a.length() - 1) : a;
            modified = a.endsWith("*");
        }
        boolean staleBuiltIn = active.startsWith("b:") && !isBuiltIn(active.substring(2));
        if (cfg.isEmpty() || staleBuiltIn || "drop".equals(cfg.get("mode"))) applyPreset(FIRST_RUN_PRESET);
        else if (!modified && active.startsWith("b:")) {
            // an untouched built-in preset follows the shipped one when an update changes it
            Map<String, String> shipped = readPreset(active);
            if (!shipped.isEmpty() && !shipped.equals(cfg)) applyPreset(active);
        }
    }

    /** enabled, debug and palette belong to the car, not to a preset. */
    private static boolean isSwitchFile(String n) {
        return n.equals("enabled") || n.equals("debug") || n.equals("palette");
    }

    private static String matchMake(String s) {
        if (s == null) return null;
        String l = s.toLowerCase();
        if (l.contains("neev") || l.contains("ceev") || l.contains("ioniq") || l.contains("ecohmc") || l.contains("hyundai")) return "hyundai";
        if (l.contains("cvev") || l.contains("sg2ev") || l.contains("dl3") || l.contains("ev6") || l.contains("ecokia") || l.contains("kia")) return "kia";
        return null;
    }

    private void updatePalette() {
        String p = readFirstLine(new File(settingsDir, "palette"));
        String make = ("hyundai".equals(p) || "kia".equals(p)) ? p : null;
        if (make == null) {
            String[] buildFields = {Build.PRODUCT, Build.DEVICE, Build.MODEL, Build.FINGERPRINT};
            for (String f : buildFields) {
                make = matchMake(f);
                if (make != null) break;
            }
        }
        if (make == null) {
            BufferedReader r = null;
            try {
                r = new BufferedReader(new FileReader("/system/build.prop"));
                String line;
                String[] keys = {"ro.build.product=", "ro.product.device=", "ro.product.name=", "ro.product.model=", "ro.build.fingerprint=", "ro.mobis.onebindir="};
                while ((line = r.readLine()) != null && make == null) {
                    String l = line.toLowerCase();
                    for (String k : keys) {
                        if (l.startsWith(k)) {
                            make = matchMake(l.substring(k.length()));
                            if (make != null) break;
                        }
                    }
                }
            } catch (IOException ignored) {
            } finally {
                if (r != null) try { r.close(); } catch (IOException ignored) { }
            }
        }
        if (make == null) {
            BufferedReader r = null;
            try {
                r = new BufferedReader(new FileReader("/data/hmgapi/settings/hmgapi.json"));
                StringBuilder sb = new StringBuilder();
                String line;
                while ((line = r.readLine()) != null) sb.append(line).append('\n');
                make = matchMake(sb.toString());
            } catch (IOException ignored) {
            } finally {
                if (r != null) try { r.close(); } catch (IOException ignored) { }
            }
        }
        if ("hyundai".equals(make)) {
            WHEEL_R = HYUNDAI_R; WHEEL_G = HYUNDAI_G; WHEEL_B = HYUNDAI_B;
        } else {
            WHEEL_R = KIA_R; WHEEL_G = KIA_G; WHEEL_B = KIA_B;
        }
    }


    private static String readFirstLine(File f) {
        BufferedReader r = null;
        try {
            r = new BufferedReader(new FileReader(f));
            String v = r.readLine();
            return v == null ? null : v.trim();
        } catch (IOException e) {
            return null;
        } finally {
            if (r != null) try { r.close(); } catch (IOException ignored) { }
        }
    }

    private static void writeFile(File f, String v) {
        File tmp = new File(f.getParentFile(), "." + f.getName() + ".tmp");
        try {
            FileOutputStream o = new FileOutputStream(tmp);
            try { o.write((v + "\n").getBytes("UTF-8")); } finally { o.close(); }
            if (!tmp.renameTo(f)) {     // a file left by root (loadpreset.sh) can't always be replaced in place
                f.delete();
                tmp.renameTo(f);
            }
        } catch (IOException e) {
            tmp.delete();
        }
    }

    private void writeAllSettings() {
        settingsDir.mkdirs();
        String[] names = settingsDir.list();
        if (names != null) {
            for (String n : names) {
                if (!isSwitchFile(n) && !cfg.containsKey(n)) new File(settingsDir, n).delete();
            }
        }
        for (Map.Entry<String, String> e : cfg.entrySet()) writeFile(new File(settingsDir, e.getKey()), e.getValue());
    }

    private void saveActive() {
        writeFile(activeFile, active + (modified ? "*" : ""));
    }

    /** One setting changed by hand: applied at once, and the preset now counts as edited. */
    private void set(String key, String value) {
        cfg.put(key, value);
        writeFile(new File(settingsDir, key), value);
        modified = true;
        saveActive();
        refreshPresets();
        configureMeter();
    }

    // ---------- presets ----------

    private static String titleOf(String id) {
        String n = id.substring(2);
        if (id.startsWith("c:")) return n;
        if (n.equals("pulse3")) return "Bold Flash";
        if (n.equals("nightdrive")) return "Night Drive";
        if (n.equals("drift")) return "Phase Drift";
        if (n.equals("phasepulse")) return "Phase Pulse";
        return n.substring(0, 1).toUpperCase() + n.substring(1);
    }

    private List<String> presetIds() {
        List<String> ids = new ArrayList<String>();
        for (String n : BUILT_IN) if (!new File(hiddenDir, n).exists()) ids.add("b:" + n);
        String[] custom = presetsDir.list();
        if (custom != null) {
            Arrays.sort(custom, String.CASE_INSENSITIVE_ORDER);
            for (String n : custom) if (n.endsWith(".conf")) ids.add("c:" + n.substring(0, n.length() - 5));
        }
        return ids;
    }

    /** A built-in preset as it shipped, ignoring any saved changes. */
    private Map<String, String> readShipped(String name) {
        return readPreset("s:" + name);
    }

    private Map<String, String> readPreset(String id) {
        Map<String, String> m = new LinkedHashMap<String, String>();
        Reader in = null;
        try {
            if (id.startsWith("b:") && overrideOf(id).exists()) {
                in = new FileReader(overrideOf(id));
            } else if (!id.startsWith("c:")) {
                InputStream a = getAssets().open("presets/" + id.substring(2) + ".conf");
                in = new InputStreamReader(a, "UTF-8");
            } else {
                in = new FileReader(new File(presetsDir, id.substring(2) + ".conf"));
            }
            BufferedReader r = new BufferedReader(in);
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                int eq = line.indexOf('=');
                if (line.startsWith("#") || eq <= 0) continue;
                m.put(line.substring(0, eq), line.substring(eq + 1));
            }
        } catch (IOException e) {
            m.clear();
        } finally {
            if (in != null) try { in.close(); } catch (IOException ignored) { }
        }
        return m;
    }

    private void applyPreset(String id) {
        Map<String, String> m = readPreset(id);
        if (m.isEmpty()) return;
        cfg = m;
        active = id;
        modified = false;
        selectedBand = 0;
        writeAllSettings();
        saveActive();
    }

    private File overrideOf(String id) {
        return new File(overridesDir, id.substring(2) + ".conf");
    }

    private String confText() {
        StringBuilder b = new StringBuilder();
        for (Map.Entry<String, String> e : cfg.entrySet()) b.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        return b.toString().trim();
    }

    private void savePreset(String name) {
        writeFile(new File(presetsDir, name + ".conf"), confText());
        active = "c:" + name;
        modified = false;
        saveActive();
    }

    /** Save: the edits become part of the preset they were made to. */
    private void saveOverActive() {
        writeFile(active.startsWith("c:") ? new File(presetsDir, active.substring(2) + ".conf") : overrideOf(active), confText());
        modified = false;
        saveActive();
    }

    private String subtitleOf(Map<String, String> m) {
        String mode = m.containsKey("mode") ? m.get("mode") : "beat";
        Gauge g = Gauge.of(mode);
        if (g != null) {
            return g.title + (g == Gauge.EFFICIENCY ? ("trip".equals(m.get("eff_source")) ? " (trip)" : " (average)") : "")
                    + " · " + g.show(speedValue(m, g.lowKey, g.lowDef)) + "–" + g.show(speedValue(m, g.highKey, g.highDef)) + " " + g.unit(carUnitsKm());
        }
        if (mode.equals("spectrum")) {
            String bands = m.containsKey("bands") ? m.get("bands") : DEFAULT_BANDS;
            return "Spectrum · " + bands.trim().split("[ ,\\t]+").length + " colours";
        }
        if (mode.equals("pulse")) {
            int pc = colorIndexOf(m.get("pulse_color"), -1, -2);
            return pc >= 0 ? "Pulse · picked color" : pc == -2 ? "Pulse · drive mode color" : "Pulse · car's own color";
        }
        if (mode.equals("duet")) {
            int bed = colorIndexOf(m.get("duet_bed"), -1, -1), acc = colorIndexOf(m.get("duet_accent"), -1, -1);
            int nb = Math.max(0, Math.min(16, intOf(m.get("duet_beats"), bed >= 0 ? 0 : 4)));
            if (nb > 0) return "Duet · pair walk (" + (nb == 1 ? "every beat" : nb + " beats") + ")";
            return "Duet · " + (bed >= 0 ? "picked bed" : "auto bed") + " + " + (acc >= 0 ? "picked flash" : "opposite flash");
        }
        if (mode.equals("synesthesia")) {
            int sh = intOf(m.get("syn_shift"), 0);
            return "Synesthesia · pitch-to-color" + (sh > 0 ? " (+" + sh + "°)" : "");
        }
        if (mode.equals("ember")) {
            int th = Math.max(0, Math.min(2, intOf(m.get("ember_theme"), 0)));
            String[] names = {"Fire", "Plasma", "Aurora"};
            return "Ember · " + names[th] + " beat & spark glow";
        }
        if (mode.equals("drift")) {
            int th = Math.max(0, Math.min(4, intOf(m.get("drift_theme"), 0)));
            int nb = intOf(m.get("drift_beats"), 4);
            String[] names = {"Forest & Aurora", "Cyberpunk", "Sunset", "Ocean Deep", "Full Wheel"};
            boolean fixed = "1".equals(m.get("brightness_fixed"));
            boolean bpulse = !fixed && "2".equals(m.get("flash_on_beat"));
            return "Phase Drift · " + names[th] + " · " + nb + " beats" + (fixed ? " · steady" : bpulse ? " · pulse" : "");
        }
        if (mode.equals("nightdrive")) {
            return "Night Drive · " + ("speed".equals(m.get("nd_source")) ? "speed" : "power & regen") + " + beat pulse";
        }
        return "Beat · colour changes";
    }

    // ---------- screen (Material Design 3 split-pane) ----------

    private Widgets.M3Switch masterSwitch;

    private void buildScreen() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(14), dp(6), dp(38), dp(6));   // right: clear of the Gen5W split-screen handle

        // Left M3 Navigation Rail / Preset Pane (surfaceContainerLow)
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setBackgroundDrawable(Widgets.shape(SURFACE_LOW, RULE, dp(1), dp(20)));
        left.setPadding(dp(10), dp(10), dp(10), dp(10));
        root.addView(left, new LinearLayout.LayoutParams(dp(282), ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(dp(4), 0, dp(2), dp(4));
        TextView title = text("Light Vibes", 18.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setSingleLine(true);
        head.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        masterSwitch = new Widgets.M3Switch(this, !"0".equals(readFirstLine(new File(settingsDir, "enabled"))),
                new Widgets.Toggle() {
                    @Override public void onToggle(boolean checked) {
                        writeFile(new File(settingsDir, "enabled"), checked ? "1" : "0");
                    }
                });
        LinearLayout.LayoutParams swLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        swLp.rightMargin = dp(6);
        head.addView(masterSwitch, swLp);

        View newBtn = iconCircleButton(Widgets.Icon.PLUS, PRIMARY_CONT, ON_PRIMARY_CONT, 0, new View.OnClickListener() {
            @Override public void onClick(View v) { askNew(); }
        });
        LinearLayout.LayoutParams newLp = new LinearLayout.LayoutParams(dp(34), dp(34));
        newLp.rightMargin = dp(4);
        head.addView(newBtn, newLp);

        View moreBtn = iconCircleButton(Widgets.Icon.MORE, SURFACE_HIGH, TEXT, RULE, new View.OnClickListener() {
            @Override public void onClick(View v) { showOverflowMenu(); }
        });
        head.addView(moreBtn, new LinearLayout.LayoutParams(dp(34), dp(34)));
        left.addView(head);

        ScrollView scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        presetList = new LinearLayout(this);
        presetList.setOrientation(LinearLayout.VERTICAL);
        presetList.setPadding(0, dp(4), 0, dp(2));
        scroll.addView(presetList);
        left.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        presetActions = new LinearLayout(this);
        presetActions.setGravity(Gravity.CENTER_VERTICAL);
        presetActions.setPadding(0, dp(6), 0, 0);
        left.addView(presetActions);

        // Right M3 Editor Pane
        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        editorTop = new LinearLayout(this);     // stays pinned while settings below scroll
        editorTop.setOrientation(LinearLayout.VERTICAL);
        right.addView(editorTop);
        rightScroll = new ScrollView(this);
        rightScroll.setVerticalScrollBarEnabled(false);
        editor = new LinearLayout(this);
        editor.setOrientation(LinearLayout.VERTICAL);
        rightScroll.addView(editor);
        right.addView(rightScroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
        rp.leftMargin = dp(14);
        root.addView(right, rp);
        meter = new MeterView(this);

        setContentView(root);
        refreshPresets();
        refreshEditor();
    }

    private View iconCircleButton(int iconKind, int fill, int iconCol, int strokeCol, View.OnClickListener click) {
        FrameLayout box = new FrameLayout(this);
        box.setBackgroundDrawable(Widgets.shape(fill, strokeCol, strokeCol != 0 ? dp(1) : 0, dp(17)));
        View ic = new View(this);
        ic.setBackgroundDrawable(new Widgets.Icon(getResources().getDisplayMetrics().density, iconKind, iconCol));
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp(18), dp(18), Gravity.CENTER);
        box.addView(ic, lp);
        box.setOnClickListener(click);
        return box;
    }

    private LinearLayout.LayoutParams weighted(int w, int h, float weight, int leftMargin) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(w == 0 ? 0 : dp(w), h == 0 ? ViewGroup.LayoutParams.WRAP_CONTENT : dp(h), weight);
        p.leftMargin = dp(leftMargin);
        return p;
    }

    private TextView text(String s, float sp, int colour) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(colour);
        return t;
    }

    private GradientDrawable rounded(int fill, int stroke, int strokeDp, int radiusDp) {
        return Widgets.shape(fill, stroke, strokeDp > 0 ? dp(strokeDp) : 0, dp(radiusDp));
    }

    /** Material 3 stadium pill button: kind 0 = tonal, 1 = filled primary, 2 = outlined, 3 = destructive tonal. */
    private TextView pillButton(String label, int kind, float textSp, View.OnClickListener click) {
        int fill, fg, stroke;
        if (kind == 1) { fill = ACCENT; fg = ON_PRIMARY; stroke = 0; }
        else if (kind == 2) { fill = 0x00000000; fg = TEXT; stroke = OUTLINE; }
        else if (kind == 3) { fill = ERR_CONT; fg = ON_ERR_CONT; stroke = 0; }
        else { fill = SURFACE_HIGH; fg = TEXT; stroke = RULE; }
        TextView t = text(label, textSp, fg);
        t.setSingleLine(true);
        t.setGravity(Gravity.CENTER);
        if (kind == 1 || kind == 3) t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(dp(12), 0, dp(12), 0);
        t.setBackgroundDrawable(Widgets.shape(fill, stroke, stroke != 0 ? dp(1) : 0, dp(22)));
        t.setOnClickListener(click);
        return t;
    }

    private LinearLayout m3Card(int padH, int padV, int topMarginDp) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundDrawable(Widgets.shape(CARD, RULE, dp(1), dp(16)));
        card.setPadding(dp(padH), dp(padV), dp(padH), dp(padV));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(topMarginDp);
        card.setLayoutParams(lp);
        return card;
    }

    private int[] presetColorsOf(Map<String, String> m) {
        String md = m.containsKey("mode") ? m.get("mode") : "beat";
        Gauge g = "nightdrive".equals(md) ? ("speed".equals(m.get("nd_source")) ? Gauge.SPEED : Gauge.POWER) : Gauge.of(md);
        if (g != null) {
            int lo = colorIndexOf(m.get(g.colLowKey), g.colLowDef, 0);
            int hi = colorIndexOf(m.get(g.colHighKey), g.colHighDef, 0);
            if (g == Gauge.POWER) {
                int z = colorIndexOf(m.get(Gauge.ZERO_KEY), Gauge.ZERO_DEF, 0);
                return new int[] {wheelColour(lo), wheelColour(z), wheelColour(hi)};
            }
            return new int[] {wheelColour(lo), wheelColour(wheelBetween(lo, hi, 0.5)), wheelColour(hi)};
        }
        if ("synesthesia".equals(md)) {
            int sh = intOf(m.get("syn_shift"), 0);
            return new int[] {wheelColour(vividAtHue(sh)), wheelColour(vividAtHue(sh + 95)),
                    wheelColour(vividAtHue(sh + 190)), wheelColour(vividAtHue(sh + 285))};
        }
        if ("drift".equals(md)) {
            int th = Math.max(0, Math.min(4, intOf(m.get("drift_theme"), 0)));
            return new int[] {wheelColour(vividAtHue(DRIFT_THEMES[th][0])), wheelColour(vividAtHue(DRIFT_THEMES[th][1])),
                    wheelColour(vividAtHue(DRIFT_THEMES[th][2])), wheelColour(vividAtHue(DRIFT_THEMES[th][3]))};
        }
        if ("duet".equals(md)) {
            int bed = colorIndexOf(m.get("duet_bed"), -1, -1);
            int acc = colorIndexOf(m.get("duet_accent"), -1, -1);
            int b0 = bed >= 0 ? bed : vividAtHue(178.0);
            int a0 = acc >= 0 ? acc : vividAtHue(wheelHue(b0) + 180.0);
            return new int[] {wheelColour(b0), wheelColour(a0)};
        }
        if ("ember".equals(md)) {
            int th = Math.max(0, Math.min(2, intOf(m.get("ember_theme"), 0)));
            return new int[] {wheelColour(emberWheelAt(th, 0.1)), wheelColour(emberWheelAt(th, 0.55)), wheelColour(emberWheelAt(th, 1.0))};
        }
        if ("spectrum".equals(md)) {
            String bandsTxt = m.containsKey("bands") ? m.get("bands") : DEFAULT_BANDS;
            String[] toks = bandsTxt.trim().split("[ ,\\t]+");
            List<Integer> cols = new ArrayList<Integer>();
            for (int i = 0; i < toks.length && cols.size() < 4; i++) {
                Band b = Band.parse(toks[i]);
                if (b != null) cols.add(b.rgb());
            }
            if (!cols.isEmpty()) {
                int[] arr = new int[cols.size()];
                for (int i = 0; i < arr.length; i++) arr[i] = cols.get(i);
                return arr;
            }
        }
        if ("pulse".equals(md)) {
            int pc = colorIndexOf(m.get("pulse_color"), -1, -2);
            if (pc == -2) return DRIVE_MODE_STRIP;
            if (pc >= 0) return new int[] {wheelColour(pc)};
            int cc = carColour();
            return new int[] {cc != 0 ? cc : ACCENT};
        }
        int st = Math.max(1, intOf(m.get("beat_step"), 19));
        return new int[] {wheelColour(vividAtHue(0)), wheelColour(vividAtHue(st * 6.0)), wheelColour(vividAtHue(st * 12.0))};
    }

    private View presetBadge(Map<String, String> m, int sizeDp, int radiusDp) {
        View dot = new View(this);
        int[] cols = presetColorsOf(m);
        if (cols.length == 1) {
            dot.setBackgroundDrawable(Widgets.shape(cols[0], RULE, dp(0.8f), dp(radiusDp)));
        } else {
            GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, cols);
            g.setGradientType(GradientDrawable.LINEAR_GRADIENT);
            g.setOrientation(GradientDrawable.Orientation.TL_BR);
            g.setCornerRadius(dp(radiusDp));
            g.setStroke(Math.max(1, dp(0.8f)), RULE);
            dot.setBackgroundDrawable(g);
        }
        return dot;
    }

    private void refreshPresets() {
        if (masterSwitch != null) {
            masterSwitch.setChecked(!"0".equals(readFirstLine(new File(settingsDir, "enabled"))));
        }
        presetList.removeAllViews();
        List<String> ids = presetIds();
        for (final String id : ids) {
            boolean sel = id.equals(active);
            Map<String, String> m = sel ? cfg : readPreset(id);

            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(10), dp(7), dp(10), dp(7));
            row.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : 0x00000000,
                    sel && modified ? ACCENT : 0, sel && modified ? dp(1) : 0, dp(16)));

            LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(dp(18), dp(18));
            bp.rightMargin = dp(10);
            row.addView(presetBadge(m, 18, 6), bp);

            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);

            LinearLayout topLine = new LinearLayout(this);
            topLine.setGravity(Gravity.CENTER_VERTICAL);
            TextView name = text(titleOf(id), 15.5f, sel ? ON_TEXT : TEXT);
            name.setSingleLine(true);
            name.setEllipsize(TextUtils.TruncateAt.END);
            if (sel) name.setTypeface(Typeface.DEFAULT_BOLD);
            topLine.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

            if (sel && modified) {
                TextView badge = text("Edited", 10.5f, ON_PRIMARY_CONT);
                badge.setTypeface(Typeface.DEFAULT_BOLD);
                badge.setPadding(dp(6), dp(1), dp(6), dp(1));
                badge.setBackgroundDrawable(Widgets.shape(PRIMARY_CONT, 0, 0, dp(8)));
                LinearLayout.LayoutParams edp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                edp.leftMargin = dp(6);
                topLine.addView(badge, edp);
            }
            col.addView(topLine);

            TextView sub = text(subtitleOf(m), 11.5f, sel ? ON_TEXT : MUTED);
            if (sel) sub.setAlpha(0.80f);
            sub.setSingleLine(true);
            sub.setEllipsize(TextUtils.TruncateAt.END);
            col.addView(sub);

            row.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            row.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (modified) askUnsaved(id);
                    else switchTo(id);
                }
            });
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            p.bottomMargin = dp(3);
            presetList.addView(row, p);
        }

        presetActions.removeAllViews();
        if (modified) {
            TextView save = pillButton("Save", 1, 14f, new View.OnClickListener() {
                @Override public void onClick(View v) {
                    saveOverActive();
                    refreshPresets();
                }
            });
            TextView saveAs = pillButton("Save as…", 0, 14f, new View.OnClickListener() {
                @Override public void onClick(View v) { askSave(); }
            });
            TextView undo = pillButton("Undo", 2, 13.5f, new View.OnClickListener() {
                @Override public void onClick(View v) { switchTo(active); }
            });
            presetActions.addView(save, weighted(0, 40, 1.05f, 0));
            presetActions.addView(saveAs, weighted(0, 40, 1.25f, 5));
            presetActions.addView(undo, weighted(0, 40, 0.9f, 5));
        } else {
            TextView saveAs = pillButton("Save as…", 0, 14f, new View.OnClickListener() {
                @Override public void onClick(View v) { askSave(); }
            });
            boolean removable = ids.size() > 1;
            TextView del = pillButton("Delete", 2, 14f, new View.OnClickListener() {
                @Override public void onClick(View v) { askDelete(); }
            });
            del.setEnabled(removable);
            del.setAlpha(removable ? 1f : 0.38f);
            presetActions.addView(saveAs, weighted(0, 40, 1.2f, 0));
            presetActions.addView(del, weighted(0, 40, 1f, 6));
        }
    }

    private void showOverflowMenu() {
        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundDrawable(Widgets.shape(SURFACE_HIGH, RULE, dp(1), dp(28)));
        card.setPadding(dp(22), dp(18), dp(22), dp(18));

        TextView head = text("Options & diagnostics", 19, TEXT);
        head.setTypeface(Typeface.DEFAULT_BOLD);
        head.setPadding(0, 0, 0, dp(10));
        card.addView(head);

        card.addView(menuItemRow("LED testing & color capture",
                "Test RGB & brightness, capture stock/drive-mode colors, or save USB logs", new View.OnClickListener() {
                    @Override public void onClick(View v) {
                        dlg.dismiss();
                        openReport();
                    }
                }));

        String[] over = overridesDir.list();
        boolean hasOverrides = false;
        if (over != null) for (String n : over) if (n.endsWith(".conf")) hasOverrides = true;
        if (hasOverrides) {
            card.addView(menuItemRow("Reset modified built-in presets",
                    "Restore shipped settings for all edited built-in presets", new View.OnClickListener() {
                        @Override public void onClick(View v) {
                            dlg.dismiss();
                            String[] names = overridesDir.list();
                            if (names != null) for (String n : names) new File(overridesDir, n).delete();
                            if (active.startsWith("b:")) applyPreset(active);
                            refreshPresets();
                            refreshEditor();
                        }
                    }));
        }

        String[] hidden = hiddenDir.list();
        if (hidden != null && hidden.length > 0) {
            card.addView(menuItemRow("Restore deleted built-in presets",
                    "Bring back hidden built-in presets in the preset list", new View.OnClickListener() {
                        @Override public void onClick(View v) {
                            dlg.dismiss();
                            String[] names = hiddenDir.list();
                            if (names != null) for (String n : names) new File(hiddenDir, n).delete();
                            refreshPresets();
                        }
                    }));
        }

        LinearLayout foot = new LinearLayout(this);
        foot.setGravity(Gravity.CENTER_VERTICAL);
        foot.setPadding(0, dp(10), 0, 0);
        TextView credit = text("Light Vibes v0.7.0 · Kyle (dibble9012) & lemoyneiv", 11.5f, MUTED);
        credit.setSingleLine(true);
        credit.setEllipsize(TextUtils.TruncateAt.END);
        foot.addView(credit, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        TextView close = pillButton("Close", 2, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) { dlg.dismiss(); }
        });
        foot.addView(close, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));
        card.addView(foot);

        dlg.setContentView(card, new ViewGroup.LayoutParams(dp(480), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.show();
    }

    private View menuItemRow(String title, String sub, View.OnClickListener click) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setBackgroundDrawable(Widgets.shape(SURFACE_LOW, RULE, dp(1), dp(14)));
        row.setPadding(dp(14), dp(10), dp(14), dp(10));
        TextView t = text(title, 15.5f, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(t);
        TextView s = text(sub, 12.5f, MUTED);
        s.setPadding(0, dp(2), 0, 0);
        row.addView(s);
        row.setOnClickListener(click);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(6);
        row.setLayoutParams(lp);
        return row;
    }

    private String mode() {
        return cfg.containsKey("mode") ? cfg.get("mode") : "beat";
    }

    private static String modeTitle(String m) {
        if ("synesthesia".equals(m)) return "Synesthesia";
        if ("spectrum".equals(m)) return "Spectrum";
        if ("beat".equals(m)) return "Beat";
        if ("pulse".equals(m)) return "Pulse";
        if ("duet".equals(m)) return "Duet";
        if ("ember".equals(m)) return "Ember";
        if ("drift".equals(m)) return "Phase Drift";
        if ("nightdrive".equals(m)) return "Night Drive";
        if ("speed".equals(m)) return "Speed";
        if ("power".equals(m)) return "Power";
        if ("efficiency".equals(m)) return "Efficiency";
        return m;
    }

    private void openReport() {
        if (reportPage != null) return;
        reportPage = new ReportPage(this, getFilesDir(), new Runnable() {
            @Override public void run() { closeReport(); }
        });
        refreshEditor();
    }

    /** Back to the settings from LED testing & color capture. */
    private void closeReport() {
        if (reportPage == null) return;
        reportPage.cancelAndStop();
        reportPage = null;
        refreshEditor();
    }

    private void refreshEditor() {
        editor.removeAllViews();
        editorTop.removeAllViews();
        tuneButton = null;
        tuneStatus = null;
        speedNow = null;
        history = null;
        if (reportPage != null) {
            rightScroll.setVisibility(View.GONE);
            editorTop.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
            editorTop.addView(reportPage, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            syncMeter();
            return;
        }
        rightScroll.setVisibility(View.VISIBLE);
        editorTop.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        editorTop.addView(topBar());
        String mode = mode();
        syncMeter();
        Gauge g = Gauge.of(mode);
        if (g != null) {
            // the car's units, not a choice: older settings said mph/kmh or mi/km
            if (g.unitKey != null && !"auto".equals(cfg.get(g.unitKey))) {
                cfg.put(g.unitKey, "auto");
                writeFile(new File(settingsDir, g.unitKey), "auto");
            }
            if (tab == TAB_COLOR) addGaugeColour(g);
            else if (tab == TAB_BRIGHTNESS) addGaugeBrightness();
            else addGaugeTuning(g);
            return;
        }
        if (tab == TAB_TUNING) {
            LinearLayout.LayoutParams mp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(68));
            mp.bottomMargin = dp(6);
            editorTop.addView(meter, mp);
            configureMeter();
            addResponse();
            return;
        }
        if (tab == TAB_BRIGHTNESS) {
            addBrightnessPage(mode);
            return;
        }
        if (mode.equals("spectrum")) {
            addSpectrum();
        } else if (mode.equals("beat")) {
            addBeatColour();
        } else if (mode.equals("duet")) {
            addDuetColour();
        } else if (mode.equals("synesthesia")) {
            addSynesthesiaColour();
        } else if (mode.equals("ember")) {
            addEmberColour();
        } else if (mode.equals("drift")) {
            addDriftColour();
        } else if (mode.equals("nightdrive")) {
            addNightDriveColour();
        } else {
            addPulseColour();
        }
    }

    /** Unified 40dp Material 3 header bar: Mode dropdown chip on the left + Segmented Button tabs on the right. */
    private View topBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBaselineAligned(false);
        bar.setPadding(0, dp(2), 0, dp(8));

        LinearLayout chip = new LinearLayout(this);
        chip.setGravity(Gravity.CENTER_VERTICAL);
        chip.setPadding(dp(12), 0, dp(12), 0);
        chip.setBackgroundDrawable(Widgets.shape(SURFACE_HIGH, OUTLINE, dp(1), dp(20)));

        LinearLayout.LayoutParams dotLp = new LinearLayout.LayoutParams(dp(12), dp(12));
        dotLp.rightMargin = dp(8);
        chip.addView(presetBadge(cfg, 12, 6), dotLp);

        TextView lbl = text("Mode", 13f, MUTED);
        chip.addView(lbl);

        TextView cur = text(modeTitle(mode()), 15.5f, TEXT);
        cur.setTypeface(Typeface.DEFAULT_BOLD);
        cur.setPadding(dp(7), 0, dp(6), 0);
        chip.addView(cur);

        View chev = new View(this);
        chev.setBackgroundDrawable(new Widgets.Icon(getResources().getDisplayMetrics().density, Widgets.Icon.CHEVRON_DOWN, TEXT));
        chip.addView(chev, new LinearLayout.LayoutParams(dp(16), dp(16)));

        chip.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showModeSheet(); }
        });
        bar.addView(chip, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));

        bar.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1));

        LinearLayout seg = Widgets.segmented(this,
                new String[][] {{"0", "Color"}, {"1", "Brightness"}, {"2", "Tuning"}},
                String.valueOf(tab), 40f, new Widgets.Pick() {
                    @Override public void onPick(String value) {
                        int nextTab = intOf(value, TAB_COLOR);
                        if (tab == nextTab) return;
                        tab = nextTab;
                        refreshEditor();
                        syncMeter();
                    }
                });
        bar.addView(seg);
        return bar;
    }

    private void selectMode(String m) {
        if (m.equals(mode())) return;
        speedEnd = 0;
        duetEnd = 0;
        if (m.equals("spectrum") && !cfg.containsKey("bands")) {
            cfg.put("bands", DEFAULT_BANDS);
            writeFile(new File(settingsDir, "bands"), DEFAULT_BANDS);
        }
        for (Map.Entry<String, String> e : readShipped(shippedForMode(m)).entrySet()) {
            if (e.getKey().equals("mode") || cfg.containsKey(e.getKey())) continue;
            cfg.put(e.getKey(), e.getValue());
            writeFile(new File(settingsDir, e.getKey()), e.getValue());
        }
        set("mode", m);
        refreshEditor();
    }

    /** Categorized Material 3 Mode Picker Sheet: 2x4 Music Modes on the left, 1x4 Hybrid & Driving on the right. */
    private void showModeSheet() {
        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout sheet = new LinearLayout(this);
        sheet.setOrientation(LinearLayout.VERTICAL);
        sheet.setBackgroundDrawable(Widgets.shape(SURFACE_HIGH, RULE, dp(1), dp(28)));
        sheet.setPadding(dp(20), dp(16), dp(20), dp(18));

        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(0, 0, 0, dp(10));
        TextView title = text("Select lighting mode", 19, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        head.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        TextView done = pillButton("Done", 0, 14f, new View.OnClickListener() {
            @Override public void onClick(View v) { dlg.dismiss(); }
        });
        head.addView(done, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34)));
        sheet.addView(head);

        LinearLayout cols = new LinearLayout(this);
        cols.setBaselineAligned(false);

        // Left section: Music Modes (2 columns x 4 rows)
        LinearLayout musicCol = new LinearLayout(this);
        musicCol.setOrientation(LinearLayout.VERTICAL);
        TextView mHead = text("MUSIC MODES", 12f, ACCENT);
        mHead.setTypeface(Typeface.DEFAULT_BOLD);
        mHead.setPadding(dp(2), 0, 0, dp(6));
        musicCol.addView(mHead);

        String[][] musicModes = {
            {"synesthesia", "Synesthesia", "Pitch-to-color spectrum glide"},
            {"spectrum", "Spectrum", "Custom multi-band frequency colors"},
            {"drift", "Phase Drift", "Mood or rainbow shifts on N beats"},
            {"duet", "Duet", "Vocal melody bed + 180° beat flash"},
            {"ember", "Ember", "Beat flares & snare/clap sparks"},
            {"beat", "Beat", "Steps around the wheel on beat"},
            {"pulse", "Pulse", "Single or drive-mode beat glow"}
        };
        for (int i = 0; i < musicModes.length; i += 2) {
            LinearLayout r = new LinearLayout(this);
            r.setBaselineAligned(false);
            r.addView(modeSheetCard(dlg, musicModes[i][0], musicModes[i][1], musicModes[i][2]), weighted(0, 54, 1f, 0));
            if (i + 1 < musicModes.length) {
                r.addView(modeSheetCard(dlg, musicModes[i + 1][0], musicModes[i + 1][1], musicModes[i + 1][2]), weighted(0, 54, 1f, 8));
            } else {
                r.addView(new View(this), weighted(0, 54, 1f, 8));
            }
            LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            if (i > 0) rp.topMargin = dp(6);
            musicCol.addView(r, rp);
        }
        cols.addView(musicCol, weighted(0, 0, 2.0f, 0));

        // Right section: Hybrid & Driving (1 column x 4 rows — exact same 4-row height!)
        LinearLayout driveCol = new LinearLayout(this);
        driveCol.setOrientation(LinearLayout.VERTICAL);
        TextView dHead = text("HYBRID & DRIVING", 12f, ACCENT);
        dHead.setTypeface(Typeface.DEFAULT_BOLD);
        dHead.setPadding(dp(2), 0, 0, dp(6));
        driveCol.addView(dHead);

        String[][] driveModes = {
            {"nightdrive", "Night Drive", "Power/speed color + beat pulse"},
            {"speed", "Speed", "Smooth color fade by vehicle speed"},
            {"power", "Power", "Regen, cruise & throttle kW"},
            {"efficiency", "Efficiency", "Trip or average mi/kWh fade"}
        };
        for (int i = 0; i < driveModes.length; i++) {
            View card = modeSheetCard(dlg, driveModes[i][0], driveModes[i][1], driveModes[i][2]);
            LinearLayout.LayoutParams dpLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(54));
            if (i > 0) dpLp.topMargin = dp(6);
            driveCol.addView(card, dpLp);
        }
        cols.addView(driveCol, weighted(0, 0, 1.05f, 14));

        sheet.addView(cols);
        dlg.setContentView(sheet, new ViewGroup.LayoutParams(dp(840), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.show();
    }

    private View modeSheetCard(final Dialog dlg, final String id, String label, String desc) {
        boolean sel = id.equals(mode());
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setPadding(dp(12), dp(6), dp(12), dp(6));
        card.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : SURFACE_LOW,
                sel ? ACCENT : RULE, dp(sel ? 1.5f : 1f), dp(14)));

        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        TextView t = text(label, 15f, sel ? ON_TEXT : TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setSingleLine(true);
        top.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (sel) {
            View chk = new View(this);
            chk.setBackgroundDrawable(new Widgets.Icon(getResources().getDisplayMetrics().density, Widgets.Icon.CHECK, ACCENT));
            top.addView(chk, new LinearLayout.LayoutParams(dp(15), dp(15)));
        }
        card.addView(top);

        TextView sub = text(desc, 11.5f, sel ? ON_TEXT : MUTED);
        if (sel) sub.setAlpha(0.82f);
        sub.setSingleLine(true);
        sub.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(sub);

        card.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                selectMode(id);
            }
        });
        return card;
    }

    /** Material 3 Brightness Tab: segmented mode/envelope selectors + Expressive range/level slider + Dimmed in Dark M3Switch. */
    private void addBrightnessPage(String mode) {
        boolean hasChoice = !(mode.equals("pulse") || mode.equals("duet") || mode.equals("ember") || mode.equals("nightdrive"));
        if (hasChoice) {
            LinearLayout modeCard = m3Card(16, 12, 2);
            boolean fixed = "1".equals(cfg.get("brightness_fixed"));

            LinearLayout r1 = new LinearLayout(this);
            r1.setGravity(Gravity.CENTER_VERTICAL);
            LinearLayout r1Text = new LinearLayout(this);
            r1Text.setOrientation(LinearLayout.VERTICAL);
            TextView r1Title = text("Brightness response", 15.5f, TEXT);
            r1Title.setTypeface(Typeface.DEFAULT_BOLD);
            r1Text.addView(r1Title);
            r1Text.addView(text(fixed ? "Holds one steady brightness while colors move"
                    : "Moves brightness dynamically between min and max", 12.5f, MUTED));
            r1.addView(r1Text, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            r1.addView(Widgets.segmented(this, new String[][] {{"0", "Follow music"}, {"1", "Fixed level"}},
                    fixed ? "1" : "0", 38f, new Widgets.Pick() {
                        @Override public void onPick(String v) {
                            set("brightness_fixed", v);
                            refreshEditor();
                        }
                    }));
            modeCard.addView(r1);

            if (!fixed) {
                View div = new View(this);
                div.setBackgroundColor(RULE);
                LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
                dlp.topMargin = dlp.bottomMargin = dp(10);
                modeCard.addView(div, dlp);

                String cur = "2".equals(cfg.get("flash_on_beat")) ? "2" : flashOn() ? "1" : "0";
                LinearLayout r2 = new LinearLayout(this);
                r2.setGravity(Gravity.CENTER_VERTICAL);
                LinearLayout r2Text = new LinearLayout(this);
                r2Text.setOrientation(LinearLayout.VERTICAL);
                TextView r2Title = text("Music envelope", 15.5f, TEXT);
                r2Title.setTypeface(Typeface.DEFAULT_BOLD);
                r2Text.addView(r2Title);
                r2Text.addView(text("2".equals(cur) ? "Smooth beat-synced swell and decay"
                        : "1".equals(cur) ? "Crisp peak flash on every detected beat"
                        : "Follows overall song loudness continuously", 12.5f, MUTED));
                r2.addView(r2Text, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
                r2.addView(Widgets.segmented(this,
                        new String[][] {{"0", "Loudness"}, {"2", "Beat pulse"}, {"1", "Flash on beat"}},
                        cur, 38f, new Widgets.Pick() {
                            @Override public void onPick(String v) {
                                set("flash_on_beat", v);
                                refreshEditor();
                            }
                        }));
                modeCard.addView(r2);
            }
            editor.addView(modeCard);

            if (fixed) {
                addGaugeBrightness();
                return;
            }
        }
        addBrightness();
    }

    /** Pulse's Color tab: compact M3 top card (active Pulse swatch + Drive Mode pill + Set as ambient M3Switch) + palette card. */
    private void addPulseColour() {
        final int picked = colorIndexOf(cfg.get("pulse_color"), -1, -2);
        int shown = picked >= 0 ? wheelColour(picked) : picked == -1 ? carColour() : 0;

        LinearLayout topCard = m3Card(12, 8, 0);
        topCard.setOrientation(LinearLayout.HORIZONTAL);
        topCard.setGravity(Gravity.CENTER_VERTICAL);

        // Active Pulse color chip
        LinearLayout activeChip = new LinearLayout(this);
        activeChip.setGravity(Gravity.CENTER_VERTICAL);
        activeChip.setPadding(dp(10), dp(6), dp(12), dp(6));
        activeChip.setBackgroundDrawable(Widgets.shape(picked != -2 ? CARD_ON : SURFACE_LOW,
                picked != -2 ? ACCENT : RULE, dp(picked != -2 ? 1.5f : 1f), dp(12)));
        View swView = new View(this);
        swView.setBackgroundDrawable(shown != 0 ? Widgets.shape(shown, RULE, dp(1), dp(7))
                : Widgets.shape(SURFACE_HIGH, OUTLINE, dp(1), dp(7)));
        LinearLayout.LayoutParams swLp = new LinearLayout.LayoutParams(dp(26), dp(26));
        swLp.rightMargin = dp(9);
        activeChip.addView(swView, swLp);
        TextView pLbl = text(picked >= 0 ? "Picked color" : picked == -1 ? "Car color" : "Pick below", 14.5f, picked != -2 ? ON_TEXT : MUTED);
        if (picked != -2) pLbl.setTypeface(Typeface.DEFAULT_BOLD);
        activeChip.addView(pLbl);
        topCard.addView(activeChip, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));

        // Drive Mode gradient chip right beside it
        TextView drive = text("Drive Mode", 14.5f, 0xFFFFFFFF);
        drive.setTypeface(Typeface.DEFAULT_BOLD);
        drive.setGravity(Gravity.CENTER);
        drive.setPadding(dp(16), 0, dp(16), 0);
        drive.setShadowLayer(dp(2), 0, 0, 0xAA000000);
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, DRIVE_MODE_STRIP);
        g.setCornerRadius(dp(12));
        g.setStroke(dp(picked == -2 ? 2.5f : 1f), picked == -2 ? TEXT : RULE);
        drive.setBackgroundDrawable(g);
        drive.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                set("pulse_color", "-2");
                refreshEditor();
            }
        });
        LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40));
        dlp.leftMargin = dp(8);
        topCard.addView(drive, dlp);

        topCard.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1));

        TextView ambLbl = text("Set as ambient color", 14.5f, picked < 0 ? MUTED : TEXT);
        ambLbl.setPadding(0, 0, dp(10), 0);
        topCard.addView(ambLbl);

        Widgets.M3Switch setCarSw = new Widgets.M3Switch(this, "1".equals(cfg.get("pulse_set_car")), new Widgets.Toggle() {
            @Override public void onToggle(boolean checked) { set("pulse_set_car", checked ? "1" : "0"); }
        });
        setCarSw.setEnabled(picked >= 0);
        topCard.addView(setCarSw);
        editor.addView(topCard);

        addPalette(picked, new ColourPick() {
            @Override public void pick(int idx) {
                set("pulse_color", String.valueOf(idx));
                refreshEditor();
            }
        }, 23, 8);
    }

    /** Drive Mode's look: three equal parts, cyan, blue and red, blended. */
    private static final int[] DRIVE_MODE_STRIP = {kiaColour(54), kiaColour(0), kiaColour(22)};
    private static int kiaColour(int i) { return 0xFF000000 | (KIA_R[i] << 16) | (KIA_G[i] << 8) | KIA_B[i]; }

    /** The car's own ambient colour, as lightvibesd last read it ("r g b drive_mode"); 0x01xx stock colours mask & 0xFF. */
    private static int carColour() {
        String v = readFirstLine(new File("/data/hmgapi/public/light-vibes/user_color"));
        if (v == null) return 0;
        String[] p = v.split(" ");
        if (p.length < 3) return 0;
        return 0xFF000000 | ((intOf(p[0], 0) & 0xFF) << 16) | ((intOf(p[1], 0) & 0xFF) << 8) | (intOf(p[2], 0) & 0xFF);
    }

    private static String shippedForMode(String m) {
        if ("spectrum".equals(m)) return "lively";
        if ("beat".equals(m)) return "epic";
        if ("pulse".equals(m)) return "glow";
        if ("drift".equals(m)) return "phasepulse";
        return m;
    }

    private List<Band> bands() {
        List<Band> out = new ArrayList<Band>();
        String txt = cfg.containsKey("bands") ? cfg.get("bands") : DEFAULT_BANDS;
        for (String tok : txt.trim().split("[ ,\\t]+")) {
            Band b = Band.parse(tok);
            if (b != null) out.add(b);
        }
        return out;
    }

    private LinearLayout makeStrip(int count, int heightDp) {
        LinearLayout strip = new LinearLayout(this);
        for (int i = 0; i < count; i++) {
            strip.addView(new View(this), new LinearLayout.LayoutParams(0, dp(heightDp), 1));
        }
        return strip;
    }

    private void paintStrip(LinearLayout strip, int[] rgbCols, float radiusDp) {
        int n = Math.min(strip.getChildCount(), rgbCols.length);
        float rPx = dp(radiusDp);
        for (int i = 0; i < n; i++) {
            View seg = strip.getChildAt(i);
            if (n == 1) seg.setBackgroundDrawable(Widgets.shape(rgbCols[i], 0, 0, rPx));
            else if (i == 0) seg.setBackgroundDrawable(Widgets.segmentShape(rgbCols[i], 0, 0, rPx, 0));
            else if (i == n - 1) seg.setBackgroundDrawable(Widgets.segmentShape(rgbCols[i], 0, 0, 0, rPx));
            else seg.setBackgroundColor(rgbCols[i]);
        }
    }

    private View swatchStripView(int[] wheelIndices, int heightDp) {
        LinearLayout strip = makeStrip(wheelIndices.length, heightDp);
        int[] rgb = new int[wheelIndices.length];
        for (int i = 0; i < wheelIndices.length; i++) rgb[i] = wheelColour(wheelIndices[i]);
        paintStrip(strip, rgb, 7f);
        return strip;
    }

    private void addSpectrum() {
        final List<Band> bands = bands();
        if (selectedBand >= bands.size()) selectedBand = 0;
        addBandChips(bands, 22, true);
        addPalette(bands.isEmpty() ? -1 : bands.get(selectedBand).wheelIndex(), new ColourPick() {
            @Override public void pick(int idx) {
                if (bands.isEmpty()) return;
                bands.get(selectedBand).colour = String.valueOf(idx);
                saveBands(bands);
                refreshEditor();
            }
        }, 23, 8);
    }

    private void addBeatColour() {
        int step0 = Math.max(1, Math.min(32, intOf(cfg.get("beat_step"), 19)));
        LinearLayout card = m3Card(16, 14, 2);

        TextView h = text("Beat color step", 16f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(h);
        TextView sub = text("Jumps around the vivid color wheel on every detected beat. Small steps glide through neighbors; larger steps leap across contrasting hues.", 13f, MUTED);
        sub.setPadding(0, dp(2), 0, dp(10));
        card.addView(sub);

        final LinearLayout preview = makeStrip(16, 26);
        paintBeatPreview(preview, step0);
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        pp.bottomMargin = dp(12);
        card.addView(preview, pp);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final TextView label = text("Color step: " + step0, 15.5f, TEXT);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(label, new LinearLayout.LayoutParams(dp(145), ViewGroup.LayoutParams.WRAP_CONTENT));

        RangeSlider bar = level(1, 32, 1, step0);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) {
                label.setText("Color step: " + hi);
                paintBeatPreview(preview, hi);
            }
            @Override public void done(int lo, int hi) {
                set("beat_step", String.valueOf(hi));
            }
        });
        row.addView(bar, new LinearLayout.LayoutParams(0, dp(40), 1));
        card.addView(row);
        editor.addView(card);
    }

    private void paintBeatPreview(LinearLayout strip, int step) {
        int n = strip.getChildCount();
        int[] cols = new int[n];
        for (int i = 0; i < n; i++) {
            double h = (i * step * (360.0 / 64.0)) % 360.0;
            cols[i] = wheelColour(vividAtHue(h));
        }
        paintStrip(strip, cols, 8f);
    }

    private static String duetWalkLabel(int nb) {
        return nb <= 0 ? "Pair walk: Off" : nb == 1 ? "Pair walk: every beat" : "Pair walk: " + nb + " beats";
    }

    private void addDuetColour() {
        if (duetEnd < 0 || duetEnd > 1) duetEnd = 0;
        final int bed = colorIndexOf(cfg.get("duet_bed"), -1, -1);
        final int acc = colorIndexOf(cfg.get("duet_accent"), -1, -1);
        final int nb0 = Math.max(0, Math.min(16, intOf(cfg.get("duet_beats"), bed >= 0 ? 0 : 4)));
        int baseBed = bed >= 0 ? bed : vividAtHue(178.0);
        int shownAcc = acc >= 0 ? acc : vividAtHue(wheelHue(baseBed) + 180.0);

        final String targetKey = duetEnd == 0 ? "duet_bed" : "duet_accent";
        final int curIdx = duetEnd == 0 ? bed : acc;

        LinearLayout ends = new LinearLayout(this);
        ends.setBaselineAligned(false);
        ends.setGravity(Gravity.CENTER_VERTICAL);
        String[] titles = {"Melody Bed" + (bed < 0 ? " · Auto" : ""), "Beat Accent" + (acc < 0 ? " · 180°" : "")};
        for (int i = 0; i < 2; i++) {
            final int which = i;
            boolean sel = i == duetEnd;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : CARD, sel ? ACCENT : RULE, dp(sel ? 1.8f : 1f), dp(14)));
            cell.setPadding(dp(8), dp(6), dp(8), dp(6));
            View swatch = new View(this);
            if (i == 0 && bed < 0) {
                int[] autoCols = {
                    wheelColour(vividAtHue(178.0)), wheelColour(vividAtHue(209.0)),
                    wheelColour(vividAtHue(287.0)), wheelColour(vividAtHue(38.0)), wheelColour(vividAtHue(88.0))
                };
                GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, autoCols);
                g.setCornerRadius(dp(6));
                swatch.setBackgroundDrawable(g);
            } else if (i == 1 && bed < 0 && acc < 0) {
                int[] oppCols = {
                    wheelColour(vividAtHue(358.0)), wheelColour(vividAtHue(29.0)),
                    wheelColour(vividAtHue(107.0)), wheelColour(vividAtHue(218.0)), wheelColour(vividAtHue(268.0))
                };
                GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, oppCols);
                g.setCornerRadius(dp(6));
                swatch.setBackgroundDrawable(g);
            } else {
                swatch.setBackgroundDrawable(Widgets.shape(wheelColour(i == 0 ? bed : shownAcc), RULE, dp(0.8f), dp(6)));
            }
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(20));
            sp.bottomMargin = dp(3);
            cell.addView(swatch, sp);
            TextView name = text(titles[i], 14.5f, sel ? ON_TEXT : TEXT);
            if (sel) name.setTypeface(Typeface.DEFAULT_BOLD);
            name.setGravity(Gravity.CENTER);
            name.setSingleLine(true);
            cell.addView(name);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    duetEnd = which;
                    refreshEditor();
                }
            });
            ends.addView(cell, weighted(0, 52, 1f, i == 0 ? 0 : 8));
        }

        // Inline Auto button on the same top row to save vertical space
        TextView autoBtn = text(duetEnd == 0 ? "Use Auto Wheel" : "Use Auto 180°", 14f, curIdx < 0 ? ON_PRIMARY_CONT : TEXT);
        autoBtn.setTypeface(curIdx < 0 ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        autoBtn.setGravity(Gravity.CENTER);
        autoBtn.setBackgroundDrawable(Widgets.shape(curIdx < 0 ? PRIMARY_CONT : SURFACE_HIGH,
                curIdx < 0 ? ACCENT : OUTLINE, dp(1), dp(14)));
        autoBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                set(targetKey, "-1");
                refreshEditor();
            }
        });
        ends.addView(autoBtn, weighted(0, 52, 0.72f, 8));
        editor.addView(ends);

        addPalette(curIdx, new ColourPick() {
            @Override public void pick(int idx) {
                if (!cfg.containsKey("duet_beats")) {
                    cfg.put("duet_beats", String.valueOf(nb0));
                    writeFile(new File(settingsDir, "duet_beats"), String.valueOf(nb0));
                }
                set(targetKey, String.valueOf(idx));
                refreshEditor();
            }
        }, 21, 6);

        LinearLayout walkCard = m3Card(14, 6, 6);
        walkCard.setOrientation(LinearLayout.HORIZONTAL);
        walkCard.setGravity(Gravity.CENTER_VERTICAL);
        final TextView walkLbl = text(duetWalkLabel(nb0), 15f, TEXT);
        walkLbl.setTypeface(Typeface.DEFAULT_BOLD);
        walkCard.addView(walkLbl, new LinearLayout.LayoutParams(dp(180), ViewGroup.LayoutParams.WRAP_CONTENT));
        RangeSlider walkBar = level(0, 16, 1, nb0);
        walkBar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) { walkLbl.setText(duetWalkLabel(hi)); }
            @Override public void done(int lo, int hi) {
                set("duet_beats", String.valueOf(hi));
                refreshPresets();
            }
        });
        walkCard.addView(walkBar, new LinearLayout.LayoutParams(0, dp(38), 1));
        editor.addView(walkCard);
    }

    private void addSynesthesiaColour() {
        int shift0 = Math.max(0, Math.min(355, intOf(cfg.get("syn_shift"), 0)));
        LinearLayout card = m3Card(16, 14, 2);

        TextView h = text("Continuous pitch-to-color", 16f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(h);
        TextView sub = text("Maps musical pitch smoothly to hue: deep bass on the left, vocals and guitars in the middle, bright highs on the right.", 13f, MUTED);
        sub.setPadding(0, dp(2), 0, dp(10));
        card.addView(sub);

        LinearLayout labels = new LinearLayout(this);
        TextView lBass = text("Bass (55 Hz)", 12.5f, MUTED);
        TextView lMid = text("Mids & Vocals", 12.5f, MUTED);
        TextView lHigh = text("Highs (8 kHz)", 12.5f, MUTED);
        lMid.setGravity(Gravity.CENTER);
        lHigh.setGravity(Gravity.RIGHT);
        labels.addView(lBass, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        labels.addView(lMid, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        labels.addView(lHigh, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        labels.setPadding(dp(2), 0, dp(2), dp(4));
        card.addView(labels);

        final LinearLayout strip = makeStrip(32, 26);
        drawSynStrip(strip, shift0);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        sp.bottomMargin = dp(12);
        card.addView(strip, sp);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final TextView label = text("Color rotation: " + shift0 + "°", 15.5f, TEXT);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(label, new LinearLayout.LayoutParams(dp(175), ViewGroup.LayoutParams.WRAP_CONTENT));
        RangeSlider bar = level(0, 355, 5, shift0);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) {
                label.setText("Color rotation: " + hi + "°");
                drawSynStrip(strip, hi);
            }
            @Override public void done(int lo, int hi) {
                set("syn_shift", String.valueOf(hi));
            }
        });
        row.addView(bar, new LinearLayout.LayoutParams(0, dp(40), 1));
        card.addView(row);
        editor.addView(card);
    }

    private void drawSynStrip(LinearLayout strip, int shift) {
        int n = strip.getChildCount();
        int[] cols = new int[n];
        for (int i = 0; i < n; i++) {
            double h = (shift + (i / (double) (n - 1)) * 285.0) % 360.0;
            cols[i] = wheelColour(vividAtHue(h));
        }
        paintStrip(strip, cols, 8f);
    }

    private static int emberWheelAt(int theme, double heat) {
        double[][] stops = {
            {268.0, 322.0,   6.0,  38.0},
            {246.0, 287.0, 209.0, 175.0},
            {246.0, 178.0,  88.0,  38.0}
        };
        double[] s = stops[Math.max(0, Math.min(2, theme))];
        double h;
        if (heat < 0.28) h = hueLerp(s[0], s[1], heat / 0.28);
        else if (heat < 0.56) h = hueLerp(s[1], s[2], (heat - 0.28) / 0.28);
        else h = hueLerp(s[2], s[3], (heat - 0.56) / 0.44);
        return vividAtHue(h);
    }

    private void addEmberColour() {
        LinearLayout info = m3Card(16, 12, 2);
        TextView h = text("Thermal beat & spark gradients", 16f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        info.addView(h);
        TextView sub = text("Beats ignite a hot flare on the right while snares and claps re-ignite cooling embers from middle to left.", 13f, MUTED);
        sub.setPadding(0, dp(2), 0, 0);
        info.addView(sub);
        editor.addView(info);

        final int cur = Math.max(0, Math.min(2, intOf(cfg.get("ember_theme"), 0)));
        String[] names = {"Fire", "Plasma", "Aurora"};
        String[] descs = {"Violet ember → crimson → gold flare", "Indigo → magenta → electric cyan", "Deep ocean → forest green → gold"};
        LinearLayout row = new LinearLayout(this);
        row.setBaselineAligned(false);
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rlp.topMargin = dp(8);
        for (int i = 0; i < 3; i++) {
            final int th = i;
            boolean sel = i == cur;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : CARD, sel ? ACCENT : RULE, dp(sel ? 2f : 1f), dp(16)));
            cell.setPadding(dp(12), dp(12), dp(12), dp(12));
            int[] cols = new int[16];
            for (int k = 0; k < 16; k++) cols[k] = emberWheelAt(i, k / 15.0);
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(26));
            sp.bottomMargin = dp(8);
            cell.addView(swatchStripView(cols, 26), sp);
            TextView title = text(names[i], 16f, sel ? ON_TEXT : TEXT);
            title.setTypeface(Typeface.DEFAULT_BOLD);
            title.setGravity(Gravity.CENTER);
            cell.addView(title);
            TextView desc = text(descs[i], 11.5f, sel ? ON_TEXT : MUTED);
            if (sel) desc.setAlpha(0.82f);
            desc.setGravity(Gravity.CENTER);
            desc.setSingleLine(true);
            desc.setEllipsize(TextUtils.TruncateAt.END);
            cell.addView(desc);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    set("ember_theme", String.valueOf(th));
                    refreshEditor();
                }
            });
            row.addView(cell, weighted(0, 0, 1, i == 0 ? 0 : 8));
        }
        editor.addView(row, rlp);
    }

    private void addDriftColour() {
        final int cur = Math.max(0, Math.min(4, intOf(cfg.get("drift_theme"), 0)));
        String[] names = {"Forest & Aurora", "Cyberpunk", "Sunset", "Ocean Deep", "Full Wheel"};
        LinearLayout row = new LinearLayout(this);
        row.setBaselineAligned(false);
        for (int i = 0; i < 5; i++) {
            final int th = i;
            boolean sel = i == cur;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : CARD, sel ? ACCENT : RULE, dp(sel ? 2f : 1f), dp(16)));
            cell.setPadding(dp(8), dp(10), dp(8), dp(10));
            int[] cols = new int[5];
            for (int k = 0; k < 5; k++) cols[k] = vividAtHue(DRIFT_THEMES[i][k]);
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(24));
            sp.bottomMargin = dp(6);
            cell.addView(swatchStripView(cols, 24), sp);
            TextView title = text(names[i], 13.5f, sel ? ON_TEXT : TEXT);
            if (sel) title.setTypeface(Typeface.DEFAULT_BOLD);
            title.setGravity(Gravity.CENTER);
            title.setSingleLine(true);
            cell.addView(title);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    set("drift_theme", String.valueOf(th));
                    refreshEditor();
                }
            });
            row.addView(cell, weighted(0, 0, 1, i == 0 ? 0 : 6));
        }
        editor.addView(row);

        int nb = Math.max(2, Math.min(16, intOf(cfg.get("drift_beats"), 4)));
        LinearLayout beatCard = m3Card(16, 12, 8);
        TextView hint = text("Tempo-synced palette step — use 2 beats for energetic pulse changes, or 8–16 beats for calm highway drift.", 12.5f, MUTED);
        hint.setPadding(0, 0, 0, dp(6));
        beatCard.addView(hint);

        LinearLayout sRow = new LinearLayout(this);
        sRow.setGravity(Gravity.CENTER_VERTICAL);
        final TextView label = text("Beats per color: " + nb, 15.5f, TEXT);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        sRow.addView(label, new LinearLayout.LayoutParams(dp(175), ViewGroup.LayoutParams.WRAP_CONTENT));
        RangeSlider bar = level(2, 16, 1, nb);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) { label.setText("Beats per color: " + hi); }
            @Override public void done(int lo, int hi) {
                set("drift_beats", String.valueOf(hi));
                refreshPresets();
            }
        });
        sRow.addView(bar, new LinearLayout.LayoutParams(0, dp(40), 1));
        beatCard.addView(sRow);
        editor.addView(beatCard);
    }

    private void addNightDriveColour() {
        boolean bySpeed = "speed".equals(cfg.get("nd_source"));
        LinearLayout srcBar = new LinearLayout(this);
        srcBar.setGravity(Gravity.CENTER_VERTICAL);
        srcBar.setPadding(dp(2), 0, 0, dp(6));
        TextView title = text("Telemetry color source", 14.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        srcBar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        srcBar.addView(Widgets.segmented(this,
                new String[][] {{"power", "Motor Power & Regen"}, {"speed", "Vehicle Speed"}},
                bySpeed ? "speed" : "power", 36f, new Widgets.Pick() {
                    @Override public void onPick(String v) {
                        set("nd_source", v);
                        refreshEditor();
                    }
                }));
        editor.addView(srcBar);
        addGaugeColour(bySpeed ? Gauge.SPEED : Gauge.POWER, 20);
    }

    private interface ColourPick {
        void pick(int wheelIndex);
    }

    private void addPalette(final int current, final ColourPick listener, int swatchDp, int topDp) {
        boolean dimPale = Gauge.of(mode()) == null && !"nightdrive".equals(mode());
        LinearLayout card = m3Card(10, 7, topDp);

        if (WHEEL_R.length > 64) {
            TextView stLbl = text("Stock ambient colors", 11.5f, MUTED);
            stLbl.setTypeface(Typeface.DEFAULT_BOLD);
            stLbl.setPadding(dp(2), 0, 0, dp(2));
            card.addView(stLbl);
            LinearLayout stRow = new LinearLayout(this);
            for (int idx = 64; idx < WHEEL_R.length; idx++) {
                boolean dim = dimPale && idx < 67 && idx != current;
                stRow.addView(makeSwatch(idx, idx == current, dim, listener), makeSwatchParams(swatchDp));
            }
            card.addView(stRow);

            TextView whLbl = text("Custom color wheel", 11.5f, MUTED);
            whLbl.setTypeface(Typeface.DEFAULT_BOLD);
            whLbl.setPadding(dp(2), dp(4), 0, dp(2));
            card.addView(whLbl);
        }
        for (int r = 0; r < 4; r++) {
            LinearLayout line = new LinearLayout(this);
            for (int c = 0; c < 16; c++) {
                int idx = r * 16 + c;
                boolean dim = dimPale && !vividWheel(idx) && idx != current;
                line.addView(makeSwatch(idx, idx == current, dim, listener), makeSwatchParams(swatchDp));
            }
            card.addView(line);
        }
        editor.addView(card);
    }

    private LinearLayout.LayoutParams makeSwatchParams(int swatchDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, dp(swatchDp), 1);
        p.leftMargin = p.rightMargin = dp(1.8f);
        p.topMargin = p.bottomMargin = dp(1.5f);
        return p;
    }

    private View makeSwatch(final int idx, boolean sel, boolean dim, final ColourPick listener) {
        int col = wheelColour(idx);
        FrameLayout box = new FrameLayout(this);
        box.setBackgroundDrawable(Widgets.shape(col, sel ? TEXT : RULE,
                sel ? dp(2.2f) : (Ui.night ? 0 : dp(0.6f)), dp(6)));
        if (dim) box.setAlpha(0.50f);
        if (sel) {
            View chk = new View(this);
            chk.setBackgroundDrawable(new Widgets.Icon(getResources().getDisplayMetrics().density, Widgets.Icon.CHECK, readableOn(col)));
            box.addView(chk, new FrameLayout.LayoutParams(dp(14), dp(14), Gravity.CENTER));
        }
        box.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { listener.pick(idx); }
        });
        return box;
    }

    private void saveBands(List<Band> bands) {
        StringBuilder sb = new StringBuilder();
        for (Band b : bands) sb.append(sb.length() > 0 ? " " : "").append(b);
        set("bands", sb.toString());
    }

    /** Two bands with the same name get numbered, so they can be told apart. */
    private static String[] titlesOf(List<Band> bands) {
        String[] out = new String[bands.size()];
        for (int i = 0; i < out.length; i++) {
            String t = bands.get(i).title();
            int same = 0, position = 0;
            for (int j = 0; j < out.length; j++) {
                if (bands.get(j).title().equals(t)) {
                    same++;
                    if (j <= i) position++;
                }
            }
            out[i] = same > 1 ? t + " " + position : t;
        }
        return out;
    }

    private static boolean canSplit(Band b) {
        return intOf(b.hi, 0) - intOf(b.lo, 0) >= MIN_SPLIT_HZ;
    }

    /** A new band is the top half of the selected one, so all the sound stays covered. */
    private void addBand(List<Band> bands) {
        Band b = bands.get(selectedBand);
        int lo = Math.max(intOf(b.lo, 0), 20), hi = intOf(b.hi, 0);
        int mid = Math.round((float) Math.sqrt(lo * (double) hi) / 10) * 10;
        mid = Math.max(lo + MIN_SPLIT_HZ / 2, Math.min(hi - MIN_SPLIT_HZ / 2, mid));
        Band n = new Band();
        n.lo = String.valueOf(mid);
        n.hi = b.hi;
        n.boost = b.boost;
        n.colour = String.valueOf(unusedColour(bands, b.wheelIndex()));
        b.hi = n.lo;
        bands.add(selectedBand + 1, n);
        selectedBand++;
        saveBands(bands);
        refreshEditor();
    }

    /** Steps round the wheel from the starting colour, choosing only vivid unused swatches. */
    private static int unusedColour(List<Band> bands, int from) {
        for (int k = 1; k < 64; k++) {
            int c = (from + k * 9) % 64;
            if (!vividWheel(c)) continue;
            boolean used = false;
            for (Band b : bands) if (b.wheelIndex() == c) used = true;
            if (!used) return c;
        }
        return from;
    }

    /** Its neighbour takes over the removed band's range. */
    private void removeBand(List<Band> bands) {
        Band b = bands.get(selectedBand);
        if (selectedBand > 0) bands.get(selectedBand - 1).hi = b.hi;
        else bands.get(1).lo = b.lo;
        bands.remove(selectedBand);
        selectedBand = Math.max(0, selectedBand - 1);
        saveBands(bands);
        refreshEditor();
    }

    private View addRemoveButtons(final List<Band> bands) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        TextView add = pillButton("+", 0, 20f, new View.OnClickListener() {
            @Override public void onClick(View v) { addBand(bands); }
        });
        TextView remove = pillButton("−", 0, 20f, new View.OnClickListener() {
            @Override public void onClick(View v) { removeBand(bands); }
        });
        add.setPadding(0, 0, 0, 0);
        remove.setPadding(0, 0, 0, 0);
        add.setBackgroundDrawable(Widgets.shape(PRIMARY_CONT, 0, 0, dp(12)));
        add.setTextColor(ON_PRIMARY_CONT);
        add.setTypeface(Typeface.DEFAULT_BOLD);
        remove.setBackgroundDrawable(Widgets.shape(SURFACE_HIGH, RULE, dp(1), dp(12)));
        boolean canAdd = bands.size() < MAX_BANDS && canSplit(bands.get(selectedBand));
        boolean canRemove = bands.size() > 1;
        add.setEnabled(canAdd);
        add.setAlpha(canAdd ? 1f : 0.35f);
        remove.setEnabled(canRemove);
        remove.setAlpha(canRemove ? 1f : 0.35f);
        col.addView(add, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1);
        rp.topMargin = dp(4);
        col.addView(remove, rp);
        return col;
    }

    private void addBandChips(final List<Band> bands, int swatchDp, boolean editable) {
        String[] titles = titlesOf(bands);
        LinearLayout row = new LinearLayout(this);
        for (int i = 0; i < bands.size(); i++) {
            final int index = i;
            Band b = bands.get(i);
            boolean sel = i == selectedBand;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : CARD, sel ? ACCENT : RULE, dp(sel ? 1.8f : 1f), dp(14)));
            cell.setPadding(dp(4), dp(6), dp(4), dp(6));
            View swatch = new View(this);
            swatch.setBackgroundDrawable(Widgets.shape(b.rgb(), RULE, dp(0.8f), dp(6)));
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(swatchDp));
            sp.leftMargin = sp.rightMargin = dp(6);
            sp.bottomMargin = dp(3);
            cell.addView(swatch, sp);
            TextView name = text(titles[i], bands.size() > 6 ? 13.5f : 15f, sel ? ON_TEXT : TEXT);
            if (sel) name.setTypeface(Typeface.DEFAULT_BOLD);
            name.setGravity(Gravity.CENTER);
            name.setSingleLine(true);
            cell.addView(name);
            TextView range = text(b.range(), bands.size() > 6 ? 10f : 11.5f, sel ? ON_TEXT : MUTED);
            if (sel) range.setAlpha(0.8f);
            range.setGravity(Gravity.CENTER);
            range.setSingleLine(true);
            cell.addView(range);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    selectedBand = index;
                    refreshEditor();
                }
            });
            row.addView(cell, weighted(0, 0, 1, i == 0 ? 0 : 6));
        }
        if (editable && !bands.isEmpty()) {
            LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(dp(46), ViewGroup.LayoutParams.MATCH_PARENT);
            bp.leftMargin = dp(8);
            row.addView(addRemoveButtons(bands), bp);
        }
        editor.addView(row);
    }

    private static String brightValueText(int lo, int hi) {
        return (lo == 0 ? "0 (off)" : String.valueOf(lo)) + " to " + hi;
    }

    /** Material 3 Brightness Range & Dimmed in Dark card (levels 0-10, where 0 turns the lamp completely off between beats). */
    private void addBrightness() {
        final int low = Math.max(0, Math.min(10, intOf(cfg.get("brightness_min"), 1)));
        final int high = Math.max(Math.max(1, low), Math.min(10, intOf(cfg.get("brightness_max"), 10)));

        LinearLayout card = m3Card(16, 12, 8);
        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("Brightness range", 15.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        top.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        final TextView val = text(brightValueText(low, high), 16.5f, ACCENT);
        val.setTypeface(Typeface.DEFAULT_BOLD);
        top.addView(val);
        card.addView(top);

        TextView hint = text("Level 0 turns the ambient lamp completely off between hits; 1–10 match the car's brightness menu.", 12.5f, MUTED);
        hint.setPadding(0, dp(2), 0, dp(4));
        card.addView(hint);

        RangeSlider range = new RangeSlider(this, 0, 10, 1, low, high, TRACK, ACCENT, TEXT).gap(0);
        range.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) {
                int h = Math.max(1, hi);
                val.setText(brightValueText(Math.min(lo, h), h));
            }
            @Override public void done(int lo, int hi) {
                int h = Math.max(1, hi), l = Math.min(lo, h);
                cfg.put("brightness_max", String.valueOf(h));
                writeFile(new File(settingsDir, "brightness_max"), cfg.get("brightness_max"));
                set("brightness_min", String.valueOf(l));
                if (hi < 1) refreshEditor();
            }
        });
        card.addView(range, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));

        addDimmedInDarkRow(card);
        editor.addView(card);
    }

    private void addDimmedInDarkRow(LinearLayout parentCard) {
        View div = new View(this);
        div.setBackgroundColor(RULE);
        LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
        dlp.topMargin = dlp.bottomMargin = dp(10);
        parentCard.addView(div, dlp);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        TextView t = text("Dimmed in Dark", 15f, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(t);
        col.addView(text("Applies the car's automatic nighttime cabin dimming", 12.5f, MUTED));
        row.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        Widgets.M3Switch sw = new Widgets.M3Switch(this, "1".equals(cfg.get("gauge_dim")), new Widgets.Toggle() {
            @Override public void onToggle(boolean checked) { set("gauge_dim", checked ? "1" : "0"); }
        });
        row.addView(sw);
        parentCard.addView(row);
    }

    // ---------- response page: the same settings the presets were tuned with ----------

    private boolean flashOn() {
        String m = mode();
        if (m.equals("duet")) return true;
        String v = cfg.get("flash_on_beat");
        return v != null && !v.equals("-1") ? "1".equals(v) : m.equals("beat");
    }

    /** Spectrum, Beat, Synesthesia or Phase Drift at one brightness level: loudness and flashes then don't touch the brightness. */
    private boolean fixedLevel() {
        String m = mode();
        return "1".equals(cfg.get("brightness_fixed")) && (m.equals("spectrum") || m.equals("beat") || m.equals("synesthesia") || m.equals("drift"));
    }

    /** Smooth beat-pulse brightness envelope on Spectrum, Beat, Synesthesia or Phase Drift (e.g. Phase Pulse). */
    private boolean beatPulseOn() {
        String m = mode();
        return !fixedLevel() && "2".equals(cfg.get("flash_on_beat"))
                && (m.equals("spectrum") || m.equals("beat") || m.equals("synesthesia") || m.equals("drift"));
    }

    private boolean pulseLike() {
        String m = mode();
        return m.equals("pulse") || m.equals("ember") || m.equals("nightdrive") || beatPulseOn();
    }

    /** Beats flash the brightness: Flash on Beat is on and the brightness follows the music. */
    private boolean flashing() {
        return flashOn() && !fixedLevel() && !pulseLike();
    }

    private boolean usesLoudness() {
        String m = mode();
        return (m.equals("spectrum") || m.equals("beat") || m.equals("synesthesia") || m.equals("drift")) && !fixedLevel() && !flashing() && !beatPulseOn();
    }

    /** Beats matter: they move Beat/Duet/Phase Drift's colour, Pulse/Ember/Night Drive/Beat-pulse's brightness, or a flash. */
    private boolean beatsMatter() {
        String m = mode();
        return (!m.equals("spectrum") && !m.equals("synesthesia")) || flashing() || beatPulseOn();
    }

    // ---------- speed mode ----------

    /**
     * Speed, Power and Efficiency: the colour follows one reading from the car, between a colour for a low value and one
     * for a high value (Power has a third, for zero). Values are stored as whole numbers: mph or km/h, kW, and tenths of
     * mi/kWh or km/kWh. Speed and Efficiency are in the car's own units: lightvibesd reads them from the car.
     */
    private static final class Gauge {
        static final Gauge SPEED = new Gauge("speed", "Speed", "speed", 0, 80, 44, 22, 0, 160, 5, 1, "speed_unit",
                new String[] {"Low Speed", "High Speed"}, "Speed Range");
        static final Gauge POWER = new Gauge("power", "Power", "power", -50, 150, 0, 22, -150, 350, 10, 1, null,
                new String[] {"Regen", "Zero", "Full Power"}, "Power Range");
        static final Gauge EFFICIENCY = new Gauge("efficiency", "Efficiency", "eff", 20, 45, 22, 44, 0, 100, 1, 10, "eff_unit",
                new String[] {"Low Efficiency", "High Efficiency"}, "Efficiency Range");
        static final String ZERO_KEY = "power_color_zero";
        static final int ZERO_DEF = 44;

        final String mode, title, lowKey, highKey, colLowKey, colHighKey, unitKey, rangeTitle;
        final String[] colourNames;
        final int lowDef, highDef, colLowDef, colHighDef, min, max, step, divisor;

        Gauge(String mode, String title, String prefix, int lowDef, int highDef, int colLowDef, int colHighDef, int min, int max, int step,
              int divisor, String unitKey, String[] colourNames, String rangeTitle) {
            this.mode = mode; this.title = title;
            lowKey = prefix + "_low"; highKey = prefix + "_high";
            colLowKey = prefix + "_color_low"; colHighKey = prefix + "_color_high";
            this.lowDef = lowDef; this.highDef = highDef; this.colLowDef = colLowDef; this.colHighDef = colHighDef;
            this.min = min; this.max = max; this.step = step; this.divisor = divisor;
            this.unitKey = unitKey; this.colourNames = colourNames; this.rangeTitle = rangeTitle;
        }

        static Gauge of(String mode) {
            return "speed".equals(mode) ? SPEED : "power".equals(mode) ? POWER : "efficiency".equals(mode) ? EFFICIENCY : null;
        }

        String unit(boolean km) {
            return this == POWER ? "kW" : this == SPEED ? (km ? "km/h" : "mph") : (km ? "km/kWh" : "mi/kWh");
        }

        String show(int stored) {
            return divisor == 1 ? String.valueOf(stored) : String.valueOf(stored / (double) divisor);
        }

        /** The colour keys and defaults in order along the gauge. */
        String[] colourKeys() {
            return this == POWER ? new String[] {colLowKey, ZERO_KEY, colHighKey} : new String[] {colLowKey, colHighKey};
        }

        int[] colourDefaults() {
            return this == POWER ? new int[] {colLowDef, ZERO_DEF, colHighDef} : new int[] {colLowDef, colHighDef};
        }

        /** The wheel colour for x (0 at the low value, 1 at the high), as lightvibesd picks it. */
        int colourAt(int[] colours, int low, int high, double x) {
            if (this != POWER) return wheelBetween(colours[0], colours[1], x);
            double v = low + (high - low) * x, zero = Math.max(low, Math.min(high, 0));
            if (v <= zero) return wheelBetween(colours[0], colours[1], zero > low ? (v - low) / (zero - low) : 1);
            return wheelBetween(colours[1], colours[2], high > zero ? (v - zero) / (high - zero) : 1);
        }
    }

    /** The car's units, as lightvibesd last read them: "km" or "mi" (miles until it has). */
    private static boolean carUnitsKm() {
        return "km".equals(readFirstLine(new File("/data/hmgapi/public/light-vibes/units")));
    }

    private static int speedValue(Map<String, String> m, String key, int def) {
        return intOf(m.get(key), def);
    }

    private static boolean pale(int i) {
        return Math.min(WHEEL_R[i], Math.min(WHEEL_G[i], WHEEL_B[i])) > 150;
    }

    /** The same walk round the wheel as wheel_walk() in lightvibesd.c; x is 0 at the low value, 1 at the high. */
    private static int wheelBetween(int a, int b, double x) {
        a = Math.max(0, Math.min(WHEEL_R.length - 1, a));
        b = Math.max(0, Math.min(WHEEL_R.length - 1, b));
        if (a == b) return a;
        if (a >= 64 || b >= 64) {
            double ha = wheelHue(a), hb = wheelHue(b);
            double dh = hb - ha;
            if (dh > 180.0) dh -= 360.0;
            if (dh < -180.0) dh += 360.0;
            double span = Math.abs(dh);
            int[] midIdx = new int[WHEEL_R.length];
            double[] midOff = new double[WHEEL_R.length];
            int mn = 0;
            for (int idx = 0; idx < WHEEL_R.length; idx++) {
                if (idx == a || idx == b || !vividWheel(idx)) continue;
                double h = wheelHue(idx);
                double off = dh >= 0.0 ? ((h - ha) % 360.0 + 360.0) % 360.0 : ((ha - h) % 360.0 + 360.0) % 360.0;
                if (off <= 4.0 || off >= span - 4.0) continue;
                int pos = mn++;
                while (pos > 0 && midOff[pos - 1] > off) {
                    midOff[pos] = midOff[pos - 1];
                    midIdx[pos] = midIdx[pos - 1];
                    pos--;
                }
                midOff[pos] = off;
                midIdx[pos] = idx;
            }
            int[] path = new int[mn + 2];
            int n = 0;
            path[n++] = a;
            double prevOff = -99.0;
            for (int i = 0; i < mn; i++) {
                if (midOff[i] - prevOff < 2.5) continue;
                path[n++] = midIdx[i];
                prevOff = midOff[i];
            }
            path[n++] = b;
            return path[(int) Math.round(x * (n - 1))];
        }
        int d = (b - a + 64) % 64;
        if (d > 32) d -= 64;
        int steps = Math.abs(d), dir = d < 0 ? -1 : 1, n = 0;
        int[] path = new int[steps + 1];
        path[n++] = a;
        for (int i = 1; i <= steps; i++) {
            int j = (a + dir * i + 64) % 64;
            if (j == b || !pale(j)) path[n++] = j;
        }
        return path[(int) Math.round(x * (n - 1))];
    }

    private int[] gaugeColours(Gauge g) {
        String[] keys = g.colourKeys();
        int[] defs = g.colourDefaults(), colours = new int[keys.length];
        for (int i = 0; i < keys.length; i++) colours[i] = colorIndexOf(cfg.get(keys[i]), defs[i], 0);
        return colours;
    }

    private void addGaugeColour(final Gauge g) {
        addGaugeColour(g, 22);
    }

    /** Color tab: endpoint cards + live rounded fade bar directly beneath them + palette card. */
    private void addGaugeColour(final Gauge g, int swatchDp) {
        final String[] keys = g.colourKeys();
        final int[] colours = gaugeColours(g);
        if (speedEnd >= keys.length) speedEnd = 0;

        LinearLayout ends = new LinearLayout(this);
        ends.setBaselineAligned(false);
        for (int i = 0; i < keys.length; i++) {
            final int end = i;
            boolean sel = i == speedEnd;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(Widgets.shape(sel ? CARD_ON : CARD, sel ? ACCENT : RULE, dp(sel ? 1.8f : 1f), dp(14)));
            cell.setPadding(dp(8), dp(6), dp(8), dp(6));
            View swatch = new View(this);
            swatch.setBackgroundDrawable(Widgets.shape(wheelColour(colours[i]), RULE, dp(0.8f), dp(6)));
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(20));
            sp.bottomMargin = dp(3);
            cell.addView(swatch, sp);
            TextView name = text(g.colourNames[i], 14.5f, sel ? ON_TEXT : TEXT);
            if (sel) name.setTypeface(Typeface.DEFAULT_BOLD);
            name.setGravity(Gravity.CENTER);
            name.setSingleLine(true);
            cell.addView(name);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    speedEnd = end;
                    refreshEditor();
                }
            });
            ends.addView(cell, weighted(0, 0, 1, i == 0 ? 0 : 8));
        }
        editor.addView(ends);

        LinearLayout fade = fadeStrip(g, colours);
        LinearLayout.LayoutParams fp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        fp.topMargin = dp(6);
        editor.addView(fade, fp);

        addPalette(colours[speedEnd], new ColourPick() {
            @Override public void pick(int idx) {
                set(keys[speedEnd], String.valueOf(idx));
                refreshEditor();
            }
        }, swatchDp, 6);
    }

    /** Rounded 32-stop preview of how the factory wheel walks across the gauge range. */
    private LinearLayout fadeStrip(Gauge g, int[] colours) {
        LinearLayout fade = makeStrip(32, 12);
        drawFade(fade, g, colours, speedValue(cfg, g.lowKey, g.lowDef), speedValue(cfg, g.highKey, g.highDef));
        return fade;
    }

    /** Tuning tab: M3 card containing optional Efficiency source segmented control + range slider + live Now pill + Current Drive graph. */
    private void addGaugeTuning(final Gauge g) {
        final boolean km = carUnitsKm();
        speedNow = text(g == Gauge.EFFICIENCY && "trip".equals(cfg.get("eff_source"))
                ? "Now: after first " + (km ? "0.5 km" : "0.3 mi") : "Now: waiting", 13.5f, ON_PRIMARY_CONT);
        speedNow.setTypeface(Typeface.DEFAULT_BOLD);
        speedNow.setPadding(dp(10), dp(3), dp(10), dp(3));
        speedNow.setBackgroundDrawable(Widgets.shape(PRIMARY_CONT, 0, 0, dp(12)));
        int[] values = {speedValue(cfg, g.lowKey, g.lowDef), speedValue(cfg, g.highKey, g.highDef)};
        addRange(g, values, km, null, gaugeColours(g));
        addHistory(g, values, km);
    }

    /** the drive graph on the page (null when there isn't one), kept live from the meter socket */
    private HistoryView history;
    private TextView historyCaption;
    private long historyReloadedFor;

    /**
     * The current drive, a line in the colour the light had, or would have had, with the colours and range set now.
     * lightvibesd records it whatever mode is on; new rows arrive each second while the page is open.
     */
    private void addHistory(final Gauge g, int[] values, final boolean km) {
        LinearLayout card = m3Card(16, 12, 8);
        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(0, 0, 0, dp(6));
        TextView title = text("Current Drive", 15.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        head.addView(title);

        HistoryView.Drive d = HistoryView.load();
        historyCaption = text(d.n >= 2 ? HistoryView.describe(d)
                : "Records speed, power & efficiency every second while the car is on", 12.5f, MUTED);
        historyCaption.setGravity(Gravity.RIGHT);
        historyCaption.setPadding(dp(12), 0, 0, 0);
        historyCaption.setSingleLine(true);
        historyCaption.setEllipsize(TextUtils.TruncateAt.END);
        head.addView(historyCaption, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        card.addView(head);

        final int[] colours = gaugeColours(g);
        final boolean trip = "trip".equals(cfg.get("eff_source"));
        final HistoryView hv = new HistoryView(this);
        final double div = g.divisor;
        hv.set(d, new HistoryView.Reading() {
            @Override public double of(HistoryView.Drive dr, int i) {   // recorded in km/h and mi/kWh
                if (g == Gauge.SPEED) return km ? dr.speed[i] : dr.speed[i] / 1.609344;
                if (g == Gauge.POWER) return dr.power[i];
                double v = trip ? dr.trip[i] : dr.avg[i];
                return km ? v * 1.609344 : v;
            }
        }, values[0] / div, values[1] / div, g == Gauge.POWER, new HistoryView.Colours() {
            @Override public int at(double value) {
                double lo = hv.low(), hi = hv.high();
                double x = hi > lo ? (value - lo) / (hi - lo) : 0;
                x = Math.max(0, Math.min(1, x));
                return wheelColour(g.colourAt(colours, (int) Math.round(lo * div), (int) Math.round(hi * div), x));
            }
        });
        int graphDp = "nightdrive".equals(mode()) ? 150 : (g == Gauge.EFFICIENCY ? 165 : 195);
        card.addView(hv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(graphDp)));
        editor.addView(card);
        history = hv;
        // a fresh connection: lightvibesd sends the rows it hasn't written to the file yet, then each new one
        meterThread = null;
        syncMeter();
    }

    /** a line from the meter socket for the drive graph: "histdrive <start>" (a new drive) or "hist <row>" */
    private void historyLine(String l) {
        if (history == null || history.getParent() == null) return;
        HistoryView.Drive d = history.drive();
        if (l.startsWith("histdrive ")) {
            long start = 0;
            try { start = Long.parseLong(l.substring(10).trim()); } catch (NumberFormatException ignored) { }
            if (start != 0 && start != d.start && start != historyReloadedFor) {   // a new drive since the page was opened
                historyReloadedFor = start;
                refreshEditor();
            }
            return;
        }
        if (d.add(l.substring(5))) {
            history.invalidate();
            if (d.n >= 2) historyCaption.setText(HistoryView.describe(d));
        }
    }

    private void drawFade(LinearLayout fade, Gauge g, int[] colours, int low, int high) {
        int n = fade.getChildCount();
        int[] cols = new int[n];
        for (int i = 0; i < n; i++) {
            cols[i] = wheelColour(g.colourAt(colours, low, high, i / (double) (n - 1)));
        }
        paintStrip(fade, cols, 6f);
    }

    /** Two-handle range card for Speed, Power, Efficiency, and Night Drive telemetry. */
    private void addRange(final Gauge g, int[] values, boolean km, final LinearLayout fade, final int[] colours) {
        final String unit = g.unit(km);
        LinearLayout card = m3Card(16, 12, 2);

        if (g == Gauge.EFFICIENCY) {
            boolean trip = "trip".equals(cfg.get("eff_source"));
            LinearLayout effRow = new LinearLayout(this);
            effRow.setGravity(Gravity.CENTER_VERTICAL);
            LinearLayout effCol = new LinearLayout(this);
            effCol.setOrientation(LinearLayout.VERTICAL);
            TextView effTitle = text("Efficiency based on", 15f, TEXT);
            effTitle.setTypeface(Typeface.DEFAULT_BOLD);
            effCol.addView(effTitle);
            effCol.addView(text(trip ? "Calculated over your current drive since start"
                    : "Matches the car cluster's accumulated average", 12.5f, MUTED));
            effRow.addView(effCol, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            effRow.addView(Widgets.segmented(this,
                    new String[][] {{"trip", "Current Trip"}, {"average", "Car Average"}},
                    trip ? "trip" : "average", 36f, new Widgets.Pick() {
                        @Override public void onPick(String v) {
                            set("eff_source", v);
                            refreshEditor();
                        }
                    }));
            card.addView(effRow);

            View div = new View(this);
            div.setBackgroundColor(RULE);
            LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
            dlp.topMargin = dlp.bottomMargin = dp(10);
            card.addView(div, dlp);
        }

        final TextView label = text("", 16.5f, ACCENT);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        final RangeSlider range = new RangeSlider(this, g.min, g.max, g.step, values[0], values[1], TRACK, ACCENT, TEXT);
        label.setText(g.show(range.low()) + " to " + g.show(range.high()) + " " + unit);
        range.setListener(new RangeSlider.Listener() {
            @Override public void moved(int low, int high) {
                label.setText(g.show(low) + " to " + g.show(high) + " " + unit);
                if (fade != null) drawFade(fade, g, colours, low, high);
                if (history != null) history.setRange(low / (double) g.divisor, high / (double) g.divisor);
            }
            @Override public void done(int low, int high) {
                cfg.put(g.highKey, String.valueOf(high));
                writeFile(new File(settingsDir, g.highKey), cfg.get(g.highKey));
                set(g.lowKey, String.valueOf(low));
            }
        });

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text(g.rangeTitle, 15.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title);
        label.setPadding(dp(12), 0, 0, 0);
        header.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (speedNow != null) header.addView(speedNow);
        card.addView(header);

        card.addView(range, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));
        editor.addView(card);
    }

    /** Steady brightness level + Dimmed in Dark M3 card (for Driving modes and Fixed-level Music modes). */
    private void addGaugeBrightness() {
        if ("auto".equals(cfg.get("gauge_brightness"))) {
            cfg.put("gauge_brightness", "10");
            writeFile(new File(settingsDir, "gauge_brightness"), "10");
            set("gauge_dim", "1");
        }
        int level = Math.max(1, Math.min(10, intOf(cfg.get("gauge_brightness"), 10)));
        LinearLayout card = m3Card(16, 12, 8);

        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("Steady brightness", 15.5f, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        top.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        final TextView val = text("Level " + level, 16.5f, ACCENT);
        val.setTypeface(Typeface.DEFAULT_BOLD);
        top.addView(val);
        card.addView(top);

        RangeSlider bar = level(1, 10, 1, level);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int lo, int hi) { val.setText("Level " + hi); }
            @Override public void done(int lo, int hi) { set("gauge_brightness", String.valueOf(hi)); }
        });
        card.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));

        addDimmedInDarkRow(card);
        editor.addView(card);
    }

    /** "speed <in the shown unit> <i>", "power <kW> <i>" or "eff <in the shown unit> <i>" from lightvibesd. */
    private void showGauge(String line) {
        if (speedNow == null) return;
        String[] p = line.split(" ");
        String m = mode();
        Gauge g = "nightdrive".equals(m) ? ("speed".equals(cfg.get("nd_source")) ? Gauge.SPEED : Gauge.POWER) : Gauge.of(m);
        if (p.length < 3 || g == null) return;
        boolean km = carUnitsKm();
        try {
            if (p[1].equals("wait")) {
                speedNow.setText("Now: after first " + (km ? "0.5 km" : "0.3 mi"));
                speedNow.setTextColor(ON_PRIMARY_CONT);
                return;
            }
            double v = Double.parseDouble(p[1]);
            String shown;
            if (p[0].equals("speed") && g == Gauge.SPEED) shown = Math.round(v) + " " + g.unit(km);
            else if (p[0].equals("power") && g == Gauge.POWER) shown = Math.round(v) + " kW" + (v < -0.5 ? " (regen)" : "");
            else if (p[0].equals("eff") && g == Gauge.EFFICIENCY) shown = (Math.round(v * 10) / 10.0) + " " + g.unit(km);
            else return;
            speedNow.setText("Now: " + shown);
            speedNow.setTextColor(ON_PRIMARY_CONT);
        } catch (NumberFormatException ignored) {
        }
    }

    private void configureMeter() {
        if (meter == null) return;
        List<Band> bands = bands();
        int[] colours = new int[bands.size()];
        String[] names = titlesOf(bands);
        for (int i = 0; i < colours.length; i++) colours[i] = bands.get(i).rgb();
        String mode = mode();
        meter.configure(mode.equals("spectrum"), beatsMatter(), usesLoudness(), colours, names,
                intOf(cfg.get("switch_margin"), 15), intOf(cfg.get("sensitivity"), 150), intOf(cfg.get("loudness_range_db"), 6));
    }

    /** The meter only reads from lightvibesd while the Tuning page (or a Driving gauge) is on screen. */
    private void syncMeter() {
        handler.removeCallbacks(tunePoll);
        if (resumed && (tab == TAB_TUNING || Gauge.of(mode()) != null)) {
            if (meterThread == null) {
                meterThread = new Thread(meterReader, "meter");
                meterThread.start();
            }
            if (tab == TAB_TUNING && Gauge.of(mode()) == null) handler.postDelayed(tunePoll, 800);
        } else {
            meterThread = null;    // meterReader sees this at its next line or timeout, and stops
        }
    }

    private final Runnable tunePoll = new Runnable() {
        @Override public void run() {
            if (!resumed || tab != TAB_TUNING || tuneButton == null) return;
            refreshTuneStatusFromDisk();
            handler.postDelayed(this, 800);
        }
    };

    private final Runnable meterReader = new Runnable() {
        @Override public void run() {
            Thread me = Thread.currentThread();
            while (meterThread == me) {
                LocalSocket s = new LocalSocket();
                try {
                    s.connect(new LocalSocketAddress("light-vibes.meter"));
                    s.setSoTimeout(1500);
                    BufferedReader r = new BufferedReader(new InputStreamReader(s.getInputStream(), "UTF-8"));
                    String line;
                    while (meterThread == me && (line = r.readLine()) != null) {
                        final String l = line;
                        handler.post(new Runnable() {     // not meter.post: it never runs while the meter isn't on screen
                            @Override public void run() {
                                if (l.startsWith("hist")) historyLine(l);
                                else if (l.startsWith("speed ") || l.startsWith("power ") || l.startsWith("eff ")) showGauge(l);
                                else if (l.startsWith("cap ")) applyTuneStatus(l.substring(4));
                                else meter.update(l);
                            }
                        });
                    }
                } catch (IOException e) {
                    // no music, or the service is restarting: try again
                } finally {
                    try { s.close(); } catch (IOException ignored) { }
                }
                try { Thread.sleep(500); } catch (InterruptedException e) { return; }
            }
        }
    };

    private void addSectionHeader(String label, View trailing, int topMarginDp) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(2), dp(topMarginDp), 0, dp(6));
        TextView t = text(label, 14f, ACCENT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (trailing != null) row.addView(trailing);
        editor.addView(row);
    }

    private void addResponse() {
        String mode = mode();
        boolean spectrum = mode.equals("spectrum");
        if (mode.equals("nightdrive")) {
            addGaugeTuning("speed".equals(cfg.get("nd_source")) ? Gauge.SPEED : Gauge.POWER);
        }
        List<View> tiles = new ArrayList<View>();
        if (spectrum) {
            tiles.add(slider("Color smoothing", "Lower reacts faster, higher is calmer", "band_smoothing_ms", 20, 0, 600, 10, " ms"));
            tiles.add(slider("Color stickiness", "Margin a new band must beat (marker line)", "switch_margin", 15, 0, 200, 5, "%"));
        } else if (mode.equals("synesthesia")) {
            tiles.add(slider("Pitch smoothing", "Lower tracks fast notes, higher glides smoothly", "band_smoothing_ms", 115, 20, 600, 10, " ms"));
        }
        tiles.add(slider("Fastest light change", "Shortest dwell between lamp updates", "min_interval_ms", 50, 20, 1000, 10, " ms"));
        if (usesLoudness()) {
            tiles.add(slider("Brightness swing", "Lower makes loudness changes more dramatic", "loudness_range_db", 6, 2, 24, 1, " dB"));
        }
        addSectionHeader("LIGHT RESPONSE", null, mode.equals("nightdrive") ? 10 : 2);
        addGrid(tiles);

        if (spectrum) {
            List<Band> bands = bands();
            if (selectedBand >= bands.size()) selectedBand = 0;
            boolean autoAdapt = !"0".equals(cfg.get("spectrum_dynamic"));
            View dynSeg = Widgets.segmented(this,
                    new String[][] {{"1", "Auto-adapt"}, {"0", "Manual only"}},
                    autoAdapt ? "1" : "0", 34f, new Widgets.Pick() {
                        @Override public void onPick(String v) {
                            set("spectrum_dynamic", v);
                            refreshEditor();
                        }
                    });
            addSectionHeader("BAND SENSITIVITY", dynSeg, 10);
            addBandChips(bands, 12, false);
            if (!bands.isEmpty()) {
                View bs = boostSlider(bands);
                LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                blp.topMargin = dp(6);
                editor.addView(bs, blp);
            }
        }

        if (beatsMatter()) {
            tiles = new ArrayList<View>();
            tiles.add(slider("Beat sensitivity", "Lower catches more beats (marker line)", "sensitivity", 150, 105, 500, 5, "%"));
            tiles.add(slider("Shortest beat gap", "Ignores beats closer together", "min_beat_ms", 200, 60, 1000, 10, " ms"));
            tiles.add(slider("Beat average", "Shorter adapts to build-ups faster", "beat_avg_ms", 1500, 200, 5000, 100, " ms"));
            int glowDef = mode.equals("ember") ? 38 : mode.equals("nightdrive") ? 45 : beatPulseOn() ? 40 : 35;
            if (pulseLike()) tiles.add(slider("Glow decay", "Higher fades more slowly after beats", "pulse_decay", glowDef, 0, 99, 1, "%"));
            else if (flashing()) tiles.add(slider("Flash length", "How long each beat flash holds", "flash_ms", 120, 30, 1000, 10, " ms"));
            tiles.add(beatRange());
            boolean autoBpm = !"0".equals(cfg.get("auto_bpm"));
            View bpmSeg = Widgets.segmented(this,
                    new String[][] {{"1", "Auto BPM"}, {"0", "Manual"}},
                    autoBpm ? "1" : "0", 34f, new Widgets.Pick() {
                        @Override public void onPick(String v) {
                            set("auto_bpm", v);
                            refreshEditor();
                        }
                    });
            addSectionHeader("BEAT DETECTION", bpmSeg, 10);
            addGrid(tiles);
        }

        addTuneCapture();
    }

    private void addTuneCapture() {
        addSectionHeader("USB TUNING CAPTURE", null, 10);
        LinearLayout card = m3Card(14, 10, 0);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);

        tuneButton = pillButton("Record 20s clip to USB", 0, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                File diag = new File(getFilesDir(), "diag");
                diag.mkdirs();
                tuneRequestedAt = System.currentTimeMillis();
                if (tuneActive) {
                    writeFile(new File(diag, "request"), "capture stop");
                    applyTuneStatus("stopped Stopping recording...");
                } else {
                    writeFile(new File(diag, "request"), "capture 20");
                    applyTuneStatus("wait Starting 20s USB recording...");
                }
            }
        });
        tuneButton.setBackgroundDrawable(Widgets.shape(PRIMARY_CONT, 0, 0, dp(20)));
        tuneButton.setTextColor(ON_PRIMARY_CONT);
        tuneButton.setTypeface(Typeface.DEFAULT_BOLD);
        tuneButton.setPadding(dp(18), 0, dp(18), 0);
        card.addView(tuneButton, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));

        tuneStatus = text("Saves 20s loopback WAV + 10 ms frame CSV + settings to USB", 13f, MUTED);
        tuneStatus.setPadding(dp(14), 0, 0, 0);
        card.addView(tuneStatus, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        clp.bottomMargin = dp(8);
        editor.addView(card, clp);

        refreshTuneStatusFromDisk();
        handler.removeCallbacks(tunePoll);
        handler.postDelayed(tunePoll, 800);
    }

    private void refreshTuneStatusFromDisk() {
        File f = new File("/data/hmgapi/public/light-vibes/tune_status");
        if (!f.exists()) return;
        String line = readFirstLine(f);
        if (line == null || line.isEmpty()) return;
        boolean fresh = (System.currentTimeMillis() - f.lastModified() < 120000L)
                || (tuneRequestedAt > 0 && f.lastModified() >= tuneRequestedAt - 2000L)
                || line.startsWith("rec ") || line.startsWith("wait ");
        if (fresh) applyTuneStatus(line);
    }

    private void applyTuneStatus(String s) {
        if (tuneButton == null || tuneStatus == null || s == null) return;
        int sp = s.indexOf(' ');
        String state = sp > 0 ? s.substring(0, sp) : s;
        String msg = sp > 0 ? s.substring(sp + 1) : s;
        boolean activeNow = "rec".equals(state) || "wait".equals(state);
        tuneActive = activeNow;
        tuneButton.setText(activeNow ? ("rec".equals(state) ? "Stop recording" : "Cancel") : "Record 20s clip to USB");
        tuneButton.setTextColor(activeNow ? ON_ERR_CONT : ON_PRIMARY_CONT);
        tuneButton.setBackgroundDrawable(Widgets.shape(activeNow ? ERR_CONT : PRIMARY_CONT, 0, 0, dp(20)));
        tuneButton.setPadding(dp(18), 0, dp(18), 0);
        tuneStatus.setText(msg);
        tuneStatus.setTextColor("error".equals(state) ? ON_ERR_CONT : (activeNow || "done".equals(state)) ? ACCENT : MUTED);
    }

    private void addGrid(List<View> tiles) {
        for (int i = 0; i < tiles.size(); i += 2) {
            LinearLayout row = new LinearLayout(this);
            row.setBaselineAligned(false);
            row.addView(tiles.get(i), weighted(0, 0, 1, 0));
            if (i + 1 < tiles.size()) row.addView(tiles.get(i + 1), weighted(0, 0, 1, 8));
            else row.addView(new View(this), weighted(0, 0, 1, 8));
            LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            if (i > 0) rlp.topMargin = dp(8);
            editor.addView(row, rlp);
        }
    }

    /** A one-handle slider in the app's M3 Expressive style. */
    private RangeSlider level(int min, int max, int step, int value) {
        return RangeSlider.level(this, min, max, step, value, TRACK, ACCENT, TEXT);
    }

    /** Compact M3 parameter card: title + accent value on the top line, 1-line hint beneath, Expressive slider at bottom. */
    private View tile(String title, TextView value, String hint, View bar) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setBackgroundDrawable(Widgets.shape(CARD, RULE, dp(1), dp(14)));
        col.setPadding(dp(14), dp(9), dp(14), dp(6));

        LinearLayout line = new LinearLayout(this);
        line.setBaselineAligned(false);
        line.setGravity(Gravity.CENTER_VERTICAL);
        TextView t = text(title, 15f, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setSingleLine(true);
        line.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        value.setTextSize(15.5f);
        value.setTextColor(ACCENT);
        value.setTypeface(Typeface.DEFAULT_BOLD);
        line.addView(value);
        col.addView(line);

        if (hint != null) {
            TextView h = text(hint, 12f, MUTED);
            h.setSingleLine(true);
            h.setEllipsize(TextUtils.TruncateAt.END);
            h.setPadding(0, dp(1), 0, 0);
            col.addView(h);
        }
        col.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(38)));
        return col;
    }

    private View slider(final String title, String hint, final String key, int def, final int lo, int hi, final int step, final String unit) {
        int v0 = Math.max(lo, Math.min(hi, intOf(cfg.get(key), def)));
        final TextView value = text(v0 + unit, 15.5f, ACCENT);
        RangeSlider bar = level(lo, hi, step, v0);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int a, int v) { value.setText(v + unit); }
            @Override public void done(int a, int v) { set(key, String.valueOf(v)); }
        });
        return tile(title, value, hint, bar);
    }

    /** The beat range as one two-handle slider. */
    private View beatRange() {
        int lo = Math.max(20, Math.min(480, intOf(cfg.get("beat_low_hz"), 40)));
        int hi = Math.max(lo + 20, Math.min(500, intOf(cfg.get("beat_high_hz"), 180)));
        final TextView value = text(lo + "–" + hi + " Hz", 15.5f, ACCENT);
        RangeSlider bar = new RangeSlider(this, 20, 500, 5, lo, hi, TRACK, ACCENT, TEXT).gap(4);
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int a, int b) { value.setText(a + "–" + b + " Hz"); }
            @Override public void done(int a, int b) {
                cfg.put("beat_high_hz", String.valueOf(b));
                writeFile(new File(settingsDir, "beat_high_hz"), cfg.get("beat_high_hz"));
                set("beat_low_hz", String.valueOf(a));
            }
        });
        return tile("Beat frequency range", value, "Kick & bass to 180 Hz, snare up to 400 Hz", bar);
    }

    /** A band's boost, ×0.1 to ×100 on a log scale: it multiplies that band's energy before the bands compete. */
    private View boostSlider(final List<Band> bands) {
        final Band band = bands.get(selectedBand);
        final String title = titlesOf(bands)[selectedBand];
        double boost = 1;
        try { if (band.boost != null) boost = Double.parseDouble(band.boost); } catch (NumberFormatException ignored) { }
        final TextView value = text("×" + boostText(boost), 15.5f, ACCENT);
        RangeSlider bar = level(0, 60, 1, (int) Math.round(20 * Math.log10(Math.max(0.1, Math.min(100, boost)) / 0.1)));
        bar.setListener(new RangeSlider.Listener() {
            @Override public void moved(int a, int p) { value.setText("×" + boostText(boostAt(p))); }
            @Override public void done(int a, int p) {
                band.boost = boostText(boostAt(p));
                saveBands(bands);
            }
        });
        return tile(title + " sensitivity", value, "Raise a band that never lights up, lower one that wins too often", bar);
    }

    private static double boostAt(int p) {
        return 0.1 * Math.pow(10, p / 20.0);
    }

    private static String boostText(double v) {
        if (v >= 10) return String.valueOf(Math.round(v));
        return String.valueOf(Math.round(v * 10) / 10.0);
    }

    // ---------- Material Design 3 frameless dialogs ----------

    /** Load a preset (tapping Undo on the active one reverts its unsaved edits). */
    private void switchTo(String id) {
        applyPreset(id);
        closeReport();
        refreshPresets();
        refreshEditor();
    }

    private EditText m3InputField(String initial) {
        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setTextSize(16.5f);
        input.setTextColor(TEXT);
        input.setHintTextColor(MUTED);
        input.setPadding(dp(14), dp(10), dp(14), dp(10));
        input.setBackgroundDrawable(Widgets.shape(SURFACE_LOW, ACCENT, dp(1.5f), dp(12)));
        input.setText(initial);
        input.selectAll();
        return input;
    }

    private LinearLayout m3DialogCard(String titleText, String bodyText) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundDrawable(Widgets.shape(SURFACE_HIGH, RULE, dp(1), dp(28)));
        card.setPadding(dp(24), dp(20), dp(24), dp(20));

        TextView h = text(titleText, 20f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(h);

        if (bodyText != null) {
            TextView body = text(bodyText, 15f, MUTED);
            body.setPadding(0, dp(8), 0, 0);
            card.addView(body);
        }
        return card;
    }

    /** The current preset has unsaved edits and another one was tapped: keep them (Save, Save as new) or drop them. */
    private void askUnsaved(final String next) {
        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout card = m3DialogCard("Save changes to " + titleOf(active) + "?",
                "Your edits will be lost if you switch presets without saving.");

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        actions.setPadding(0, dp(18), 0, 0);

        TextView discard = pillButton("Discard", 2, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                switchTo(next);
            }
        });
        actions.addView(discard, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42)));

        TextView saveNew = pillButton("Save as new", 0, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                askSave(new Runnable() { @Override public void run() { switchTo(next); } });
            }
        });
        LinearLayout.LayoutParams snLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        snLp.leftMargin = dp(8);
        actions.addView(saveNew, snLp);

        TextView save = pillButton("Save", 1, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                saveOverActive();
                switchTo(next);
            }
        });
        LinearLayout.LayoutParams svLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        svLp.leftMargin = dp(8);
        actions.addView(save, svLp);

        card.addView(actions);
        dlg.setContentView(card, new ViewGroup.LayoutParams(dp(480), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.show();
    }

    private void askSave() {
        askSave(null);
    }

    /** Save as: then runs after (if any) once saved. */
    private void askSave(final Runnable after) {
        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout card = m3DialogCard("Save preset as", null);
        final EditText name = m3InputField(active.startsWith("c:") ? active.substring(2) : freeName());
        LinearLayout.LayoutParams inLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        inLp.topMargin = dp(12);
        card.addView(name, inLp);

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        actions.setPadding(0, dp(18), 0, 0);

        TextView cancel = pillButton("Cancel", 2, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) { dlg.dismiss(); }
        });
        actions.addView(cancel, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42)));

        TextView save = pillButton("Save", 1, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                String n = cleanName(name.getText().toString());
                savePreset(n.isEmpty() ? freeName() : n);
                refreshPresets();
                if (after != null) after.run();
            }
        });
        LinearLayout.LayoutParams svLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        svLp.leftMargin = dp(8);
        actions.addView(save, svLp);

        card.addView(actions);
        dlg.setContentView(card, new ViewGroup.LayoutParams(dp(440), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
        dlg.show();
    }

    /**
     * Any name the owner types. A custom preset is a file of its own (files/presets/<name>.conf, id "c:<name>"), apart
     * from the built-ins, so it can share a built-in's name. Only what can't be in a file name goes: "/", control
     * characters, and a leading dot (hidden files).
     */
    static String cleanName(String raw) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < raw.length(); i++) {
            char c = raw.charAt(i);
            if (c != '/' && c != '\\' && !Character.isISOControl(c)) b.append(c);
        }
        String n = b.toString().trim();
        while (n.startsWith(".")) n = n.substring(1).trim();
        while (n.endsWith("*")) n = n.substring(0, n.length() - 1).trim();   // "*" at the end marks an edited preset in files/active
        if (n.length() > 40) n = n.substring(0, 40).trim();
        return n;
    }

    /** "Power" if it's free, otherwise "Power 2", "Power 3"... */
    private String unusedName(String n) {
        if (!new File(presetsDir, n + ".conf").exists()) return n;
        for (int i = 2; ; i++) if (!new File(presetsDir, n + " " + i + ".conf").exists()) return n + " " + i;
    }

    private String freeName() {
        for (int i = 1; ; i++) {
            String n = "My preset " + i;
            if (!new File(presetsDir, n + ".conf").exists()) return n;
        }
    }

    /** New presets start from Synesthesia as it shipped: its smooth response suits most music, and all of it can be changed. */
    private void askNew() {
        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout card = m3DialogCard("New preset",
                "Starts from Synesthesia — choose any mode, colors, or tuning once created.");
        final EditText name = m3InputField(freeName());
        LinearLayout.LayoutParams inLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        inLp.topMargin = dp(12);
        card.addView(name, inLp);

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        actions.setPadding(0, dp(18), 0, 0);

        TextView cancel = pillButton("Cancel", 2, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) { dlg.dismiss(); }
        });
        actions.addView(cancel, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42)));

        TextView create = pillButton("Create", 1, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                String n = cleanName(name.getText().toString());
                n = n.isEmpty() ? freeName() : unusedName(n);
                cfg = readShipped("synesthesia");
                selectedBand = 0;
                writeAllSettings();
                savePreset(n);
                refreshPresets();
                refreshEditor();
            }
        });
        LinearLayout.LayoutParams crLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        crLp.leftMargin = dp(8);
        actions.addView(create, crLp);

        card.addView(actions);
        dlg.setContentView(card, new ViewGroup.LayoutParams(dp(450), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
        dlg.show();
    }

    private void askDelete() {
        final boolean custom = active.startsWith("c:");
        final String id = active;
        List<String> others = presetIds();
        others.remove(id);
        if (others.isEmpty()) return;
        final String next = others.get(0);

        final Dialog dlg = new Dialog(this);
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dlg.getWindow().setBackgroundDrawable(new ColorDrawable(0));

        LinearLayout card = m3DialogCard("Delete " + titleOf(id) + "?",
                "The lights will switch to " + titleOf(next) + "."
                        + (custom ? "" : " Built-in presets can be restored anytime from the ⋮ menu."));

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        actions.setPadding(0, dp(18), 0, 0);

        TextView cancel = pillButton("Cancel", 2, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) { dlg.dismiss(); }
        });
        actions.addView(cancel, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42)));

        if (!custom && overrideOf(id).exists()) {
            TextView reset = pillButton("Reset instead", 0, 14.5f, new View.OnClickListener() {
                @Override public void onClick(View v) {
                    dlg.dismiss();
                    overrideOf(id).delete();
                    applyPreset(id);
                    refreshPresets();
                    refreshEditor();
                }
            });
            LinearLayout.LayoutParams rsLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
            rsLp.leftMargin = dp(8);
            actions.addView(reset, rsLp);
        }

        TextView del = pillButton("Delete", 3, 14.5f, new View.OnClickListener() {
            @Override public void onClick(View v) {
                dlg.dismiss();
                if (custom) {
                    new File(presetsDir, id.substring(2) + ".conf").delete();
                } else {
                    overrideOf(id).delete();
                    writeFile(new File(hiddenDir, id.substring(2)), "1");
                }
                applyPreset(next);
                refreshPresets();
                refreshEditor();
            }
        });
        LinearLayout.LayoutParams dlLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        dlLp.leftMargin = dp(8);
        actions.addView(del, dlLp);

        card.addView(actions);
        dlg.setContentView(card, new ViewGroup.LayoutParams(dp(470), ViewGroup.LayoutParams.WRAP_CONTENT));
        dlg.show();
    }
}
