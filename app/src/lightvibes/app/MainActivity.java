package lightvibes.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
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
    /* factory 64-colour custom wheels (Kia EV6 & Hyundai Ioniq 5), same tables as lightvibesd.c: the only colours the lamp reliably accepts */
    private static final int[] KIA_R = {16,16,16,33,47,64,82,100,120,136,155,169,183,193,204,213,221,228,235,239,246,250,254,255,255,255,255,255,255,255,255,255,255,255,254,255,246,233,220,202,166,168,149,132,117,104,93,81,71,62,53,44,37,30,25,20,16,16,16,16,16,16,16,16};
    private static final int[] KIA_G = {67,46,32,28,28,28,28,28,28,29,33,35,35,33,31,27,23,19,14,10,6,4,1,50,68,80,105,120,143,166,195,205,218,232,243,255,255,255,255,252,226,243,235,229,223,221,222,225,231,238,245,250,250,250,250,250,244,230,210,187,163,138,111,89};
    private static final int[] KIA_B = {244,242,239,239,239,239,239,239,239,239,239,239,234,224,210,192,174,153,153,116,96,81,69,29,35,39,48,53,62,70,88,118,158,200,230,255,246,221,188,147,91,98,98,98,98,98,105,118,133,152,169,189,207,225,237,249,252,252,252,252,252,252,252,248};

    private static final int[] HYUNDAI_R = {176,195,205,232,255,255,255,255,255,255,255,255,254,255,254,254,255,255,255,255,255,255,255,254,255,255,255,255,255,255,255,255,255,254,255,254,247,230,210,175,119,58,10,0,0,0,0,0,0,0,0,0,25,55,81,100,109,122,125,128,130,143,156,167};
    private static final int[] HYUNDAI_G = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,107,73,88,113,125,3,34,48,60,65,137,148,160,172,177,191,207,216,237,255,254,255,254,254,255,255,217,180,161,140,125,115,99,85,62,0,0,0,0,42,52,25,0,0,0,0,0};
    private static final int[] HYUNDAI_B = {255,254,254,254,241,195,158,137,117,111,106,101,95,72,58,30,106,116,73,88,113,115,10,0,23,42,49,107,119,111,113,108,123,138,147,165,182,186,188,191,196,198,235,245,255,255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};

    private static int[] WHEEL_R = KIA_R, WHEEL_G = KIA_G, WHEEL_B = KIA_B;

    private static final String DEFAULT_BANDS = "30-150:22:1 150-500:28:2 500-2000:44:4 2000-6000:56:10 6000-16000:8:30";
    private static final String[] BUILT_IN = {"lively", "marigold", "contrast", "pulse3", "glow", "epic", "speed"};
    private static final String FIRST_RUN_PRESET = "b:lively";
    private static final int MAX_BANDS = 8;      // what fits on screen; lightvibesd takes up to 12
    private static final int MIN_SPLIT_HZ = 60;  // each half keeps at least one FFT bin (23.4 Hz each)

    private static final int BG = 0xFF141518, CARD = 0xFF212329, CARD_ON = 0xFF1E5F69;
    private static final int TEXT = 0xFFE2E5E9, MUTED = 0xFF8F959C, ACCENT = 0xFF2AAAB9;

    private File settingsDir, presetsDir, overridesDir, hiddenDir, activeFile;
    private Map<String, String> cfg = new LinkedHashMap<String, String>();
    private String active = FIRST_RUN_PRESET;   // "b:<asset name>" or "c:<custom name>"
    private boolean modified;
    private int selectedBand;
    private boolean responsePage;
    private int speedEnd;          // the speed page's selected end: 0 low, 1 high
    private TextView speedNow;

    private LinearLayout presetList, editorTop, editor;
    private MeterView meter;
    private volatile Thread meterThread;
    private boolean resumed;
    private TextView status;
    private TextView saveButton, deleteButton;
    private final Handler handler = new Handler();
    private final Runnable statusTick = new Runnable() {
        @Override public void run() {
            updateStatus();
            handler.postDelayed(this, 1000);
        }
    };

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

        int rgb() {
            if (colour.startsWith("#")) {
                try { return 0xFF000000 | Integer.parseInt(colour.substring(1), 16); }
                catch (NumberFormatException e) { return Color.GRAY; }
            }
            return wheelColour(intOf(colour, 0));
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

    private static int wheelColour(int i) {
        if (i < 0 || i > 63) i = 0;
        return Color.rgb(WHEEL_R[i], WHEEL_G[i], WHEEL_B[i]);
    }

    private static int intOf(String s, int def) {
        try { return Integer.parseInt(s.trim()); } catch (RuntimeException e) { return def; }
    }

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    // ---------- lifecycle ----------

    @Override protected void onCreate(Bundle saved) {
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
        resumed = true;
        handler.post(statusTick);
        syncMeter();
    }

    @Override protected void onPause() {
        handler.removeCallbacks(statusTick);
        resumed = false;
        syncMeter();
        super.onPause();
    }

    // ---------- settings files ----------

    private void loadCurrent() {
        settingsDir.mkdirs();
        updatePalette();
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
        if (cfg.isEmpty()) applyPreset(FIRST_RUN_PRESET);
    }

    /** enabled, debug and palette belong to the car, not to a preset. */
    private static boolean isSwitchFile(String n) {
        return n.equals("enabled") || n.equals("debug") || n.equals("palette");
    }

    private static String matchMake(String s) {
        if (s == null) return null;
        String l = s.toLowerCase();
        if (l.contains("neev") || l.contains("ceev") || l.contains("ioniq") || l.contains("ecohmc") || l.contains("hyundai")) return "hyundai";
        if (l.contains("cvev") || l.contains("dl3") || l.contains("ev6") || l.contains("ecokia") || l.contains("kia")) return "kia";
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
        if (mode.equals("speed")) {
            return "Speed · " + speedValue(m, "speed_low", 0) + "–" + speedValue(m, "speed_high", 80) + " " + unitTitle(m);
        }
        if (mode.equals("spectrum")) {
            String bands = m.containsKey("bands") ? m.get("bands") : DEFAULT_BANDS;
            return "Spectrum · " + bands.trim().split("[ ,\\t]+").length + " colours";
        }
        return mode.equals("pulse") ? "Pulse · your colour" : "Beat · colour changes";
    }

    // ---------- screen ----------

    private void buildScreen() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(16), dp(8), dp(40), dp(4));   // right: clear of the side panel handle

        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        root.addView(left, new LinearLayout.LayoutParams(dp(300), ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("Light Vibes", 22, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        head.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Switch on = new Switch(this);
        on.setTextOn("On");
        on.setTextOff("Off");
        on.setChecked(!"0".equals(readFirstLine(new File(settingsDir, "enabled"))));
        on.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean checked) {
                writeFile(new File(settingsDir, "enabled"), checked ? "1" : "0");
                updateStatus();
            }
        });
        head.addView(on);
        left.addView(head);

        status = text("", 15, MUTED);
        status.setPadding(0, dp(2), 0, dp(8));
        left.addView(status);

        ScrollView scroll = new ScrollView(this);
        presetList = new LinearLayout(this);
        presetList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(presetList);
        left.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout actions = new LinearLayout(this);
        actions.setPadding(0, dp(8), 0, 0);
        saveButton = button("Save", false, new View.OnClickListener() {
            @Override public void onClick(View v) {
                saveOverActive();
                refreshPresets();
            }
        });
        View saveAs = button("Save as…", false, new View.OnClickListener() {
            @Override public void onClick(View v) { askSave(); }
        });
        deleteButton = button("Delete", false, new View.OnClickListener() {
            @Override public void onClick(View v) { askDelete(); }
        });
        actions.addView(saveButton, weighted(0, 56, 1, 0));
        actions.addView(saveAs, weighted(0, 56, 1.3f, 6));
        actions.addView(deleteButton, weighted(0, 56, 1, 6));
        left.addView(actions);

        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        editorTop = new LinearLayout(this);     // stays put while the settings below scroll
        editorTop.setOrientation(LinearLayout.VERTICAL);
        right.addView(editorTop);
        ScrollView rightScroll = new ScrollView(this);
        editor = new LinearLayout(this);
        editor.setOrientation(LinearLayout.VERTICAL);
        rightScroll.addView(editor);
        right.addView(rightScroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
        rp.leftMargin = dp(20);
        root.addView(right, rp);
        meter = new MeterView(this);

        setContentView(root);
        refreshPresets();
        refreshEditor();
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
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(radiusDp));
        if (strokeDp > 0) g.setStroke(dp(strokeDp), stroke);
        return g;
    }

    private TextView button(String label, boolean selected, View.OnClickListener click) {
        TextView t = text(label, 18, TEXT);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundDrawable(rounded(selected ? CARD_ON : CARD, 0, 0, 8));
        t.setOnClickListener(click);
        return t;
    }

    private void refreshPresets() {
        presetList.removeAllViews();
        for (final String id : presetIds()) {
            boolean sel = id.equals(active);
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(dp(14), dp(8), dp(14), dp(8));
            row.setBackgroundDrawable(rounded(sel ? CARD_ON : CARD, 0, 0, 8));
            row.addView(text(titleOf(id) + (sel && modified ? " (edited)" : ""), 19, TEXT));
            row.addView(text(subtitleOf(sel ? cfg : readPreset(id)), 14, sel ? TEXT : MUTED));
            row.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    applyPreset(id);     // tapping the active preset again undoes edits
                    refreshPresets();
                    refreshEditor();
                }
            });
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            p.bottomMargin = dp(6);
            presetList.addView(row, p);
        }
        saveButton.setEnabled(modified);
        saveButton.setAlpha(modified ? 1f : 0.35f);
        boolean removable = presetIds().size() > 1;
        deleteButton.setEnabled(removable);
        deleteButton.setAlpha(removable ? 1f : 0.35f);

        presetList.addView(listButton("+ New preset", new View.OnClickListener() {
            @Override public void onClick(View v) { askNew(); }
        }));
        String[] hidden = hiddenDir.list();
        if (hidden != null && hidden.length > 0) {
            presetList.addView(listButton("Restore built-in presets", new View.OnClickListener() {
                @Override public void onClick(View v) {
                    String[] names = hiddenDir.list();
                    if (names != null) for (String n : names) new File(hiddenDir, n).delete();
                    refreshPresets();
                }
            }));
        }
    }

    private View listButton(String label, View.OnClickListener click) {
        TextView t = text(label, 18, ACCENT);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundDrawable(rounded(BG, 0xFF3A3F47, 1, 8));
        t.setOnClickListener(click);
        t.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56)));
        ((LinearLayout.LayoutParams) t.getLayoutParams()).bottomMargin = dp(6);
        return t;
    }

    private String mode() {
        return cfg.containsKey("mode") ? cfg.get("mode") : "beat";
    }

    private void refreshEditor() {
        editor.removeAllViews();
        editorTop.removeAllViews();
        editorTop.addView(modeRow());
        String mode = mode();
        speedNow = null;
        syncMeter();
        if (mode.equals("speed")) {
            addSpeed();
            return;
        }
        if (responsePage) {
            LinearLayout.LayoutParams mp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(84));
            mp.topMargin = dp(8);
            editorTop.addView(meter, mp);
            configureMeter();
            addResponse();
            return;
        }
        if (mode.equals("spectrum")) {
            addSpectrum();
        } else if (mode.equals("beat")) {
            addNote("The colour moves around the wheel on every beat. A larger step makes bigger jumps.");
            addStepSlider();
        } else {
            addNote("Keeps your own colour from Settings > Vehicle > Lights > Ambient light. Brightness pulses with the beat.");
        }
        addBrightness();
        if (!mode.equals("pulse")) addFlash();
    }

    private View modeRow() {
        LinearLayout row = new LinearLayout(this);
        String[][] modes = {{"spectrum", "Spectrum"}, {"beat", "Beat"}, {"pulse", "Pulse"}, {"speed", "Speed"}};
        String current = mode();
        for (int i = 0; i < modes.length; i++) {
            final String m = modes[i][0];
            row.addView(button(modes[i][1], m.equals(current), new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (m.equals(mode())) return;
                    if (m.equals("spectrum") && !cfg.containsKey("bands")) {
                        cfg.put("bands", DEFAULT_BANDS);
                        writeFile(new File(settingsDir, "bands"), DEFAULT_BANDS);
                    }
                    set("mode", m);
                    refreshEditor();
                }
            }), weighted(0, 46, 1, i == 0 ? 0 : 8));
        }
        if (!current.equals("speed")) {
            row.addView(tab("Look", false), weighted(0, 46, 0.8f, 24));
            row.addView(tab("Response", true), weighted(0, 46, 1, 6));
        }
        return row;
    }

    private View tab(String label, final boolean response) {
        boolean sel = responsePage == response;
        TextView t = text(label, 18, sel ? TEXT : MUTED);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundDrawable(rounded(sel ? 0xFF3A3F47 : BG, 0xFF3A3F47, 1, 8));
        t.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                responsePage = response;
                refreshEditor();
                syncMeter();
            }
        });
        return t;
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

    private void addSpectrum() {
        final List<Band> bands = bands();
        if (selectedBand >= bands.size()) selectedBand = 0;

        addHeading("Sound band: tap one, then choose its colour");
        addBandChips(bands, 26, true);

        addPalette(bands.isEmpty() ? -1 : intOf(bands.get(selectedBand).colour, -1), new ColourPick() {
            @Override public void pick(int idx) {
                if (bands.isEmpty()) return;
                bands.get(selectedBand).colour = String.valueOf(idx);
                saveBands(bands);
                refreshEditor();
            }
        });
    }

    private interface ColourPick {
        void pick(int wheelIndex);
    }

    private void addPalette(final int current, final ColourPick listener) {
        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.VERTICAL);
        grid.setPadding(0, dp(10), 0, 0);
        for (int r = 0; r < 4; r++) {
            LinearLayout line = new LinearLayout(this);
            for (int c = 0; c < 16; c++) {
                final int idx = r * 16 + c;
                View v = new View(this);
                v.setBackgroundDrawable(rounded(wheelColour(idx), TEXT, idx == current ? 3 : 0, 5));
                v.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View view) { listener.pick(idx); }
                });
                LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, dp(28), 1);
                p.leftMargin = p.rightMargin = dp(2);
                p.topMargin = p.bottomMargin = dp(2);
                line.addView(v, p);
            }
            grid.addView(line);
        }
        editor.addView(grid);
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
        n.colour = String.valueOf(unusedColour(bands, intOf(b.colour, 0)));
        b.hi = n.lo;
        bands.add(selectedBand + 1, n);
        selectedBand++;
        saveBands(bands);
        refreshEditor();
    }

    /** Eight steps round the wheel from the starting colour, avoiding its white stretch and colours in use. */
    private static int unusedColour(List<Band> bands, int from) {
        for (int k = 1; k < 64; k++) {
            int c = (from + k * 8) % 64;
            if (pale(c)) continue;
            boolean used = false;
            for (Band b : bands) if (intOf(b.colour, -1) == c) used = true;
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
        TextView add = button("+", false, new View.OnClickListener() {
            @Override public void onClick(View v) { addBand(bands); }
        });
        TextView remove = button("−", false, new View.OnClickListener() {
            @Override public void onClick(View v) { removeBand(bands); }
        });
        add.setTextSize(24);
        remove.setTextSize(24);
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
            cell.setBackgroundDrawable(rounded(CARD, sel ? TEXT : CARD, 3, 8));
            cell.setPadding(dp(4), dp(6), dp(4), dp(6));
            View swatch = new View(this);
            swatch.setBackgroundDrawable(rounded(b.rgb(), 0, 0, 6));
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(swatchDp));
            sp.leftMargin = sp.rightMargin = dp(6);
            sp.bottomMargin = dp(3);
            cell.addView(swatch, sp);
            TextView name = text(titles[i], bands.size() > 6 ? 14 : 16, TEXT);
            name.setGravity(Gravity.CENTER);
            cell.addView(name);
            TextView range = text(b.range(), bands.size() > 6 ? 10 : 12, MUTED);
            range.setGravity(Gravity.CENTER);
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
            LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(dp(52), ViewGroup.LayoutParams.MATCH_PARENT);
            bp.leftMargin = dp(8);
            row.addView(addRemoveButtons(bands), bp);
        }
        editor.addView(row);
    }

    private void addHeading(String s) {
        TextView t = text(s, 15, MUTED);
        t.setPadding(0, dp(6), 0, dp(4));
        editor.addView(t);
    }

    private void addNote(String s) {
        TextView t = text(s, 17, TEXT);
        t.setPadding(0, dp(16), 0, dp(8));
        editor.addView(t);
    }

    private void addStepSlider() {
        final TextView label = text("", 16, TEXT);
        int step = intOf(cfg.get("beat_step"), 5);
        label.setText("Colour step: " + step);
        label.setPadding(0, dp(8), 0, 0);
        editor.addView(label);
        SeekBar bar = new SeekBar(this);
        bar.setMax(31);
        bar.setProgress(step - 1);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) { label.setText("Colour step: " + (p + 1)); }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) { set("beat_step", String.valueOf(s.getProgress() + 1)); }
        });
        editor.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)));
    }

    /** Levels 1-10, as in the car's own menu. 0 would turn the lamp off, so it isn't offered. */
    private void addBrightness() {
        LinearLayout row = new LinearLayout(this);
        row.setPadding(0, dp(10), 0, 0);
        final int low = Math.max(1, intOf(cfg.get("brightness_min"), 3));
        final int high = Math.max(1, intOf(cfg.get("brightness_max"), 10));
        final TextView lowLabel = text("Dimmest: " + low, 16, TEXT);
        final TextView highLabel = text("Brightest: " + high, 16, TEXT);
        final SeekBar lowBar = new SeekBar(this), highBar = new SeekBar(this);
        lowBar.setMax(9);
        highBar.setMax(9);
        lowBar.setProgress(low - 1);
        highBar.setProgress(high - 1);
        SeekBar.OnSeekBarChangeListener listener = new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                if (user && s == lowBar && p > highBar.getProgress()) highBar.setProgress(p);
                if (user && s == highBar && p < lowBar.getProgress()) lowBar.setProgress(p);
                lowLabel.setText("Dimmest: " + (lowBar.getProgress() + 1));
                highLabel.setText("Brightest: " + (highBar.getProgress() + 1));
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) {
                cfg.put("brightness_max", String.valueOf(highBar.getProgress() + 1));
                writeFile(new File(settingsDir, "brightness_max"), cfg.get("brightness_max"));
                set("brightness_min", String.valueOf(lowBar.getProgress() + 1));
            }
        };
        lowBar.setOnSeekBarChangeListener(listener);
        highBar.setOnSeekBarChangeListener(listener);
        row.addView(labelled(lowLabel, lowBar), weighted(0, 0, 1, 0));
        row.addView(labelled(highLabel, highBar), weighted(0, 0, 1, 16));
        editor.addView(row);
    }

    // ---------- response page: the same settings the presets were tuned with ----------

    private boolean flashOn() {
        String v = cfg.get("flash_on_beat");
        return v != null && !v.equals("-1") ? !v.equals("0") : mode().equals("beat");
    }

    // ---------- speed mode ----------

    private static int speedValue(Map<String, String> m, String key, int def) {
        return intOf(m.get(key), def);
    }

    private static boolean isKmh(Map<String, String> m) {
        return "kmh".equals(m.get("speed_unit"));
    }

    private static String unitTitle(Map<String, String> m) {
        return isKmh(m) ? "km/h" : "mph";
    }

    private static boolean pale(int i) {
        return Math.min(WHEEL_R[i], Math.min(WHEEL_G[i], WHEEL_B[i])) > 150;
    }

    /** The same walk round the wheel as speed_wheel_index() in lightvibesd.c; x is 0 at the low speed, 1 at the high. */
    private static int wheelBetween(int a, int b, double x) {
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

    private void addSpeed() {
        speedNow = text("Waiting for a speed reading", 15, MUTED);
        speedNow.setPadding(0, dp(6), 0, dp(4));
        editor.addView(speedNow);

        final String[] keys = {"speed_color_low", "speed_color_high"};
        int[] colours = {speedValue(cfg, keys[0], 44), speedValue(cfg, keys[1], 22)};
        int[] speeds = {speedValue(cfg, "speed_low", 0), speedValue(cfg, "speed_high", 80)};
        String[] names = {"Low speed", "High speed"};
        LinearLayout ends = new LinearLayout(this);
        for (int i = 0; i < 2; i++) {
            final int end = i;
            LinearLayout cell = new LinearLayout(this);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER);
            cell.setBackgroundDrawable(rounded(CARD, i == speedEnd ? TEXT : CARD, 3, 8));
            cell.setPadding(dp(4), dp(6), dp(4), dp(6));
            View swatch = new View(this);
            swatch.setBackgroundDrawable(rounded(wheelColour(colours[i]), 0, 0, 6));
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(22));
            sp.leftMargin = sp.rightMargin = dp(6);
            sp.bottomMargin = dp(3);
            cell.addView(swatch, sp);
            TextView name = text(names[i] + " colour", 15, TEXT);
            name.setGravity(Gravity.CENTER);
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

        addPalette(colours[speedEnd], new ColourPick() {
            @Override public void pick(int idx) {
                set(keys[speedEnd], String.valueOf(idx));
                refreshEditor();
            }
        });

        // what the fade will look like
        LinearLayout fade = new LinearLayout(this);
        fade.setPadding(0, dp(4), 0, dp(2));
        for (int i = 0; i < 32; i++) {
            View v = new View(this);
            v.setBackgroundColor(wheelColour(wheelBetween(colours[0], colours[1], i / 31.0)));
            fade.addView(v, new LinearLayout.LayoutParams(0, dp(14), 1));
        }
        editor.addView(fade);

        addSpeedSliders(speeds);
    }

    /** 0-160 in the chosen unit, in fives. The low speed always stays below the high one. */
    private void addSpeedSliders(int[] speeds) {
        LinearLayout row = new LinearLayout(this);
        final String unit = unitTitle(cfg);
        final TextView lowLabel = text("Low speed: " + speeds[0] + " " + unit, 16, TEXT);
        final TextView highLabel = text("High speed: " + speeds[1] + " " + unit, 16, TEXT);
        final SeekBar lowBar = new SeekBar(this), highBar = new SeekBar(this);
        lowBar.setMax(32);
        highBar.setMax(32);
        lowBar.setProgress(speeds[0] / 5);
        highBar.setProgress(speeds[1] / 5);
        SeekBar.OnSeekBarChangeListener listener = new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                if (user && s == lowBar && p >= highBar.getProgress()) highBar.setProgress(Math.min(32, p + 1));
                if (user && s == highBar && p <= lowBar.getProgress()) lowBar.setProgress(Math.max(0, p - 1));
                lowLabel.setText("Low speed: " + lowBar.getProgress() * 5 + " " + unit);
                highLabel.setText("High speed: " + highBar.getProgress() * 5 + " " + unit);
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) {
                if (lowBar.getProgress() >= highBar.getProgress()) lowBar.setProgress(Math.max(0, highBar.getProgress() - 1));
                cfg.put("speed_high", String.valueOf(highBar.getProgress() * 5));
                writeFile(new File(settingsDir, "speed_high"), cfg.get("speed_high"));
                set("speed_low", String.valueOf(lowBar.getProgress() * 5));
            }
        };
        lowBar.setOnSeekBarChangeListener(listener);
        highBar.setOnSeekBarChangeListener(listener);
        row.addView(labelled(lowLabel, lowBar), weighted(0, 0, 1, 0));
        row.addView(labelled(highLabel, highBar), weighted(0, 0, 1, 16));
        TextView unitButton = button(isKmh(cfg) ? "km/h" : "mph", false, new View.OnClickListener() {
            @Override public void onClick(View v) { toggleUnit(); }
        });
        LinearLayout.LayoutParams up = new LinearLayout.LayoutParams(dp(80), dp(46));
        up.leftMargin = dp(12);
        up.gravity = Gravity.CENTER_VERTICAL;
        row.addView(unitButton, up);
        editor.addView(row);
    }

    /** Keeps the same real speeds, rounded to the slider's fives. */
    private void toggleUnit() {
        boolean toKmh = !isKmh(cfg);
        double k = toKmh ? 1.609344 : 1 / 1.609344;
        int low = Math.min(160, (int) Math.round(speedValue(cfg, "speed_low", 0) * k / 5) * 5);
        int high = Math.min(160, (int) Math.round(speedValue(cfg, "speed_high", 80) * k / 5) * 5);
        if (high <= low) low = Math.max(0, high - 5);
        cfg.put("speed_low", String.valueOf(low));
        cfg.put("speed_high", String.valueOf(high));
        writeFile(new File(settingsDir, "speed_low"), cfg.get("speed_low"));
        writeFile(new File(settingsDir, "speed_high"), cfg.get("speed_high"));
        set("speed_unit", toKmh ? "kmh" : "mph");
        refreshEditor();
    }

    /** "speed <km/h> <wheel index>" from lightvibesd. */
    private void showSpeed(String line) {
        if (speedNow == null) return;
        String[] p = line.split(" ");
        if (p.length < 3) return;
        try {
            double kmh = Double.parseDouble(p[1]);
            long shown = Math.round(isKmh(cfg) ? kmh : kmh / 1.609344);
            speedNow.setText("Now: " + shown + " " + unitTitle(cfg));
            speedNow.setTextColor(wheelColour(intOf(p[2], 0)));
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
        meter.configure(mode.equals("spectrum"), !mode.equals("spectrum") || flashOn(), !mode.equals("pulse"), colours, names,
                intOf(cfg.get("switch_margin"), 30), intOf(cfg.get("sensitivity"), 150), intOf(cfg.get("loudness_range_db"), 12));
    }

    /** The meter only reads from lightvibesd while the Response page is on screen. */
    private void syncMeter() {
        if (resumed && (responsePage || mode().equals("speed"))) {
            if (meterThread == null) {
                meterThread = new Thread(meterReader, "meter");
                meterThread.start();
            }
        } else {
            meterThread = null;    // meterReader sees this at its next line or timeout, and stops
        }
    }

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
                                if (l.startsWith("speed ")) showSpeed(l);
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

    private void addResponse() {
        String mode = mode();
        boolean spectrum = mode.equals("spectrum"), pulse = mode.equals("pulse");
        List<View> tiles = new ArrayList<View>();
        if (spectrum) {
            tiles.add(slider("Colour smoothing", "Lower reacts faster, higher is calmer", "band_smoothing_ms", 120, 0, 600, 10, " ms"));
            tiles.add(slider("Colour stickiness", "How much louder a new band must get (white line)", "switch_margin", 30, 0, 200, 5, "%"));
        }
        tiles.add(slider("Fastest light change", "Shortest time between changes", "min_interval_ms", 250, 150, 1000, 10, " ms"));
        if (!pulse) tiles.add(slider("Brightness swing", "Lower makes brightness more dramatic", "loudness_range_db", 12, 3, 24, 1, " dB"));
        addHeading("Light");
        addGrid(tiles);

        if (spectrum) {
            List<Band> bands = bands();
            if (selectedBand >= bands.size()) selectedBand = 0;
            addHeading("Band sensitivity: raise a band that never lights up, lower one that wins too often");
            addBandChips(bands, 10, false);
            if (!bands.isEmpty()) editor.addView(boostSlider(bands));
        }

        if (!spectrum || flashOn()) {
            tiles = new ArrayList<View>();
            tiles.add(slider("Beat sensitivity", "Lower catches more beats (white line)", "sensitivity", 150, 105, 500, 5, "%"));
            tiles.add(slider("Shortest beat gap", "Ignores beats closer together", "min_beat_ms", 300, 100, 1000, 10, " ms"));
            tiles.add(slider("Beat average", "Shorter adapts to build-ups faster", "beat_avg_ms", 1500, 200, 5000, 100, " ms"));
            if (pulse) tiles.add(slider("Glow", "Higher fades more slowly", "pulse_decay", 60, 0, 99, 1, "%"));
            else if (flashOn()) tiles.add(slider("Flash length", "How long each flash holds", "flash_ms", 350, 50, 1000, 10, " ms"));
            tiles.add(slider("Beat range, low", "40–150 Hz is kick and bass", "beat_low_hz", 40, 20, 200, 5, " Hz"));
            tiles.add(slider("Beat range, high", "Try 150–400 Hz for snare", "beat_high_hz", 150, 60, 500, 10, " Hz"));
            addHeading("Beat detection");
            addGrid(tiles);
        }
    }

    private void addGrid(List<View> tiles) {
        for (int i = 0; i < tiles.size(); i += 2) {
            LinearLayout row = new LinearLayout(this);
            row.addView(tiles.get(i), weighted(0, 0, 1, 0));
            if (i + 1 < tiles.size()) row.addView(tiles.get(i + 1), weighted(0, 0, 1, 16));
            else row.addView(new View(this), weighted(0, 0, 1, 16));
            editor.addView(row);
        }
    }

    private View tile(TextView label, String hint, SeekBar bar) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.addView(label);
        col.addView(text(hint, 13, MUTED));
        col.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(40)));
        return col;
    }

    private View slider(final String title, String hint, final String key, int def, final int lo, int hi, final int step, final String unit) {
        int value = intOf(cfg.get(key), def);
        final TextView label = text(title + ": " + value + unit, 16, TEXT);
        SeekBar bar = new SeekBar(this);
        bar.setMax((hi - lo) / step);
        bar.setProgress(Math.round((Math.max(lo, Math.min(hi, value)) - lo) / (float) step));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                if (user) label.setText(title + ": " + (lo + p * step) + unit);
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) { set(key, String.valueOf(lo + s.getProgress() * step)); }
        });
        return tile(label, hint, bar);
    }

    /** A band's boost, ×0.1 to ×100 on a log scale: it multiplies that band's energy before the bands compete. */
    private View boostSlider(final List<Band> bands) {
        final Band band = bands.get(selectedBand);
        final String title = titlesOf(bands)[selectedBand];
        double boost = 1;
        try { if (band.boost != null) boost = Double.parseDouble(band.boost); } catch (NumberFormatException ignored) { }
        final TextView label = text(title + " sensitivity: ×" + boostText(boost), 16, TEXT);
        label.setPadding(0, dp(6), 0, 0);
        SeekBar bar = new SeekBar(this);
        bar.setMax(60);
        bar.setProgress((int) Math.round(20 * Math.log10(Math.max(0.1, Math.min(100, boost)) / 0.1)));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                if (user) label.setText(title + " sensitivity: ×" + boostText(boostAt(p)));
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) {
                band.boost = boostText(boostAt(s.getProgress()));
                saveBands(bands);
            }
        });
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.addView(label);
        col.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(40)));
        return col;
    }

    private static double boostAt(int p) {
        return 0.1 * Math.pow(10, p / 20.0);
    }

    private static String boostText(double v) {
        if (v >= 10) return String.valueOf(Math.round(v));
        return String.valueOf(Math.round(v * 10) / 10.0);
    }

    private View labelled(TextView label, SeekBar bar) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.addView(label);
        col.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(40)));
        return col;
    }

    private void addFlash() {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(4), 0, 0);
        row.addView(text("Flash to full brightness on each beat", 16, TEXT), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Switch flash = new Switch(this);
        flash.setTextOn("On");
        flash.setTextOff("Off");
        flash.setChecked(flashOn());
        flash.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean checked) {
                set("flash_on_beat", checked ? "1" : "0");
            }
        });
        row.addView(flash);
        editor.addView(row);
    }

    private void updateStatus() {
        if ("0".equals(readFirstLine(new File(settingsDir, "enabled")))) {
            status.setText("Off");
            return;
        }
        if (mode().equals("speed")) {
            status.setText("Speed mode");
            return;
        }
        String s = readFirstLine(new File("/proc/asound/card0/pcm2c/sub0/status"));
        status.setText(s != null && s.contains("RUNNING") ? "Following the music" : "Waiting for music");
    }

    // ---------- dialogs ----------

    private void askSave() {
        final EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setInputType(InputType.TYPE_CLASS_TEXT);
        name.setText(active.startsWith("c:") ? active.substring(2) : freeName());
        name.selectAll();
        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle("Save preset as")
                .setView(name)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Save", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dialog, int which) {
                        savePreset(cleanName(name.getText().toString()));
                        refreshPresets();
                    }
                })
                .create();
        d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
        d.show();
    }

    private String cleanName(String raw) {
        String n = raw.trim().replaceAll("[^A-Za-z0-9 _-]", "");
        if (n.length() > 24) n = n.substring(0, 24).trim();
        return n.isEmpty() || isBuiltInTitle(n) ? freeName() : n;
    }

    private boolean isBuiltInTitle(String n) {
        for (String b : BUILT_IN) if (titleOf("b:" + b).equalsIgnoreCase(n)) return true;
        return false;
    }

    private String freeName() {
        for (int i = 1; ; i++) {
            String n = "My preset " + i;
            if (!new File(presetsDir, n + ".conf").exists()) return n;
        }
    }

    /** New presets start from Lively as it shipped: its timing suits most music, and all of it can be changed. */
    private void askNew() {
        final EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setInputType(InputType.TYPE_CLASS_TEXT);
        name.setText(freeName());
        name.selectAll();
        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle("New preset")
                .setView(name)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Create", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dialog, int which) {
                        String n = cleanName(name.getText().toString());
                        if (new File(presetsDir, n + ".conf").exists()) n = freeName();
                        cfg = readShipped("lively");
                        selectedBand = 0;
                        writeAllSettings();
                        savePreset(n);
                        refreshPresets();
                        refreshEditor();
                    }
                })
                .create();
        d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
        d.show();
    }

    private void askDelete() {
        final boolean custom = active.startsWith("c:");
        final String id = active;
        List<String> others = presetIds();
        others.remove(id);
        if (others.isEmpty()) return;
        final String next = others.get(0);
        AlertDialog.Builder b = new AlertDialog.Builder(this)
                .setTitle("Delete " + titleOf(id) + "?")
                .setMessage("The lights switch to " + titleOf(next) + "."
                        + (custom ? "" : " You can bring built-in presets back as they came, from the end of the list."))
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Delete", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dialog, int which) {
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
        if (!custom && overrideOf(id).exists()) {
            b.setNeutralButton("Reset instead", new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface dialog, int which) {
                    overrideOf(id).delete();
                    applyPreset(id);
                    refreshPresets();
                    refreshEditor();
                }
            });
        }
        b.show();
    }
}
