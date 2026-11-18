package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Handler;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Material Design 3 LED Testing & Color Capture page (USB-only, zero external/GitHub links):
 * - Left card: 15-second RGB & brightness hardware check with on-screen verdict and direct USB log export.
 * - Right card: Instant "Capture current color" and hands-off 1 Hz "Auto-capture" (for stock themes & Drive Modes),
 *   saving directly to USB "Light Vibes reports/".
 */
final class ReportPage extends LinearLayout {
    private static final int START_TIMEOUT_MS = 12000, TEST_TIMEOUT_MS = 60000, SAVE_WAIT_MS = 8000;
    private static final int SNAP_TIMEOUT_MS = 6000, POLL_MS = 450;

    private static final int MODE_NONE = 0, MODE_TEST = 1, MODE_SAVE = 2, MODE_SNAP = 3, MODE_WATCH = 4;

    private final File diag, request, result;
    private final Runnable close;
    private final Handler handler = new Handler();
    private Map<String, String> testMap = new HashMap<String, String>();
    private String seenColour, seenBright;
    private int activeOp = MODE_NONE;
    private long startedAt;
    private boolean polling;

    private LinearLayout leftBody;
    private TextView stageLine, usbLine;
    private static final String[] STEPS = {"start", "red", "green", "blue", "low", "high", "mid", "restoring"};
    private static final String[] STEP_NAMES = {"Read your color", "Red", "Green", "Blue",
            "Low bright (1)", "High bright (10)", "Mid bright (5)", "Restore settings"};
    private static final int[] STEP_SWATCH = {0, 0xFFFE0145, 0xFF75DF62, 0xFF1043F4, 0x40FFFFFF, 0xFFFFFFFF, 0x99FFFFFF, 0};
    private TextView[] stepRows;
    private View[] stepSwatches;

    // Right card: Color capture & 1 Hz auto-capture
    private TextView snapBtn, watchBtn, capStatus;
    private LinearLayout capList;
    private final List<int[]> captured = new ArrayList<int[]>(); // each: {r, g, b, br, dim, dm}

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            tick();
            if (polling) handler.postDelayed(this, POLL_MS);
        }
    };

    ReportPage(Context c, File filesDir, final Runnable close) {
        super(c);
        setOrientation(VERTICAL);
        diag = new File(filesDir, "diag");
        request = new File(diag, "request");
        result = new File(diag, "result");
        this.close = close;

        // Top M3 Header Row
        LinearLayout head = new LinearLayout(c);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(0, dp(2), 0, dp(8));

        LinearLayout titles = new LinearLayout(c);
        titles.setOrientation(VERTICAL);
        TextView h = text("LED testing & color capture", 18.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        titles.addView(h);
        TextView sub = text("Test lamp control or capture stock & drive-mode colors directly to USB (\"Light Vibes reports/\")", 12f, MUTED);
        sub.setSingleLine(true);
        sub.setEllipsize(TextUtils.TruncateAt.END);
        titles.addView(sub);
        head.addView(titles, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        TextView back = pill("Back to settings", 4, 14f, new OnClickListener() {
            @Override public void onClick(View v) {
                stopAllAndClose();
            }
        });
        head.addView(back, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38)));
        addView(head);

        // Two-column M3 Split Content
        LinearLayout cols = new LinearLayout(c);
        cols.setBaselineAligned(false);

        leftBody = new LinearLayout(c);
        leftBody.setOrientation(VERTICAL);
        leftBody.setBackgroundDrawable(Widgets.shape(CARD, RULE, dp(1), dp(18)));
        leftBody.setPadding(dp(16), dp(14), dp(16), dp(14));
        cols.addView(leftBody, new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.0f));

        LinearLayout rightCard = buildCaptureCard(c);
        LayoutParams rp = new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.22f);
        rp.leftMargin = dp(12);
        cols.addView(rightCard, rp);

        addView(cols, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        showTestIntro();
        refreshCapturedList();
    }

    private void stopAllAndClose() {
        cancelAndStop();
        close.run();
    }

    void cancelAndStop() {
        if (activeOp == MODE_WATCH) writeRequest("watch stop");
        activeOp = MODE_NONE;
        stop();
    }

    void stop() {
        polling = false;
        handler.removeCallbacks(poll);
    }

    void resume() {
        if (!polling && activeOp != MODE_NONE) startPolling();
    }

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private TextView text(String s, float sp, int colour) {
        TextView t = new TextView(getContext());
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(colour);
        return t;
    }

    /** kind: 0 = tonal, 1 = filled primary, 2 = outlined, 3 = destructive tonal, 4 = primary container */
    private TextView pill(String label, int kind, float sp, OnClickListener click) {
        TextView t = new TextView(getContext());
        t.setText(label);
        t.setTextSize(sp);
        t.setSingleLine(true);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(14), 0, dp(14), 0);
        stylePill(t, kind);
        t.setOnClickListener(click);
        return t;
    }

    private void stylePill(TextView t, int kind) {
        int fill, fg, stroke;
        if (kind == 1) { fill = ACCENT; fg = ON_PRIMARY; stroke = 0; }
        else if (kind == 2) { fill = 0x00000000; fg = TEXT; stroke = OUTLINE; }
        else if (kind == 3) { fill = ERR_CONT; fg = ON_ERR_CONT; stroke = 0; }
        else if (kind == 4) { fill = PRIMARY_CONT; fg = ON_PRIMARY_CONT; stroke = 0; }
        else { fill = SURFACE_HIGH; fg = TEXT; stroke = RULE; }
        t.setTextColor(fg);
        t.setTypeface(kind == 1 || kind == 3 || kind == 4 ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        t.setBackgroundDrawable(Widgets.shape(fill, stroke, stroke != 0 ? dp(1) : 0, dp(20)));
    }

    // ---------- Right Card: Color Capture & 1 Hz Auto-Capture ----------

    private LinearLayout buildCaptureCard(Context c) {
        LinearLayout card = new LinearLayout(c);
        card.setOrientation(VERTICAL);
        card.setBackgroundDrawable(Widgets.shape(CARD, RULE, dp(1), dp(18)));
        card.setPadding(dp(16), dp(14), dp(16), dp(14));

        TextView h = text("Stock & drive-mode color capture", 16.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(h);

        TextView desc = text("Read the lamp's live 16-bit RGB immediately, or start 1 Hz auto-capture while tapping swatches in the car's Ambient Light menu or cycling Drive Modes.", 12f, MUTED);
        desc.setPadding(0, dp(2), 0, dp(10));
        card.addView(desc);

        LinearLayout btnRow = new LinearLayout(c);
        btnRow.setGravity(Gravity.CENTER_VERTICAL);

        snapBtn = pill("Capture current", 1, 13.5f, new OnClickListener() {
            @Override public void onClick(View v) { startSnap(); }
        });
        btnRow.addView(snapBtn, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38)));

        watchBtn = pill("Auto-capture", 0, 13.5f, new OnClickListener() {
            @Override public void onClick(View v) { toggleWatch(); }
        });
        LayoutParams wp = new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38));
        wp.leftMargin = dp(8);
        btnRow.addView(watchBtn, wp);

        card.addView(btnRow);

        capStatus = text("Ready · saves to USB \"Light Vibes reports/captured-colors.txt\"", 12f, ACCENT);
        capStatus.setPadding(0, dp(8), 0, dp(6));
        card.addView(capStatus);

        ScrollView scroll = new ScrollView(c);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setBackgroundDrawable(Widgets.shape(SURFACE_LOW, RULE, dp(1), dp(12)));
        capList = new LinearLayout(c);
        capList.setOrientation(VERTICAL);
        capList.setPadding(dp(10), dp(8), dp(10), dp(8));
        scroll.addView(capList);
        card.addView(scroll, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        return card;
    }

    private void syncCaptureButtons() {
        if (watchBtn != null) {
            boolean w = activeOp == MODE_WATCH;
            watchBtn.setText(w ? "Stop auto-capture" : "Auto-capture");
            stylePill(watchBtn, w ? 3 : 0);
        }
        boolean busyOther = activeOp == MODE_TEST || activeOp == MODE_SAVE || activeOp == MODE_SNAP;
        if (snapBtn != null) {
            snapBtn.setEnabled(!busyOther);
            snapBtn.setAlpha(snapBtn.isEnabled() ? 1f : 0.45f);
        }
    }

    private void startSnap() {
        if (activeOp == MODE_TEST || activeOp == MODE_SAVE) return;
        diag.mkdirs();
        result.delete();
        activeOp = MODE_SNAP;
        startedAt = System.currentTimeMillis();
        capStatus.setText("Reading current ambient color from MICOM…");
        capStatus.setTextColor(ACCENT);
        syncCaptureButtons();
        writeRequest("snap");
        startPolling();
    }

    private void toggleWatch() {
        if (activeOp == MODE_TEST || activeOp == MODE_SAVE) return;
        if (activeOp == MODE_WATCH) {
            writeRequest("watch stop");
            activeOp = MODE_NONE;
            stop();
            capStatus.setText("Auto-capture stopped · " + captured.size() + " color(s) logged this session");
            capStatus.setTextColor(MUTED);
            syncCaptureButtons();
            return;
        }
        diag.mkdirs();
        result.delete();
        activeOp = MODE_WATCH;
        startedAt = System.currentTimeMillis();
        capStatus.setText("Watching lamp (1 Hz)… switch to car Ambient Light or cycle Drive Modes");
        capStatus.setTextColor(ACCENT);
        syncCaptureButtons();
        writeRequest("watch");
        startPolling();
    }

    private void addCapturedUnique(int r, int g, int b, int br, int dim, int dm) {
        for (int i = 0; i < captured.size(); i++) {
            int[] e = captured.get(i);
            if (e[0] == r && e[1] == g && e[2] == b && e[5] == dm) {
                captured.remove(i);
                break;
            }
        }
        captured.add(0, new int[] {r, g, b, br, dim, dm});
    }

    private void mergeItemsString(String items) {
        if (items == null || items.isEmpty()) return;
        String[] entries = items.split(";");
        for (int i = entries.length - 1; i >= 0; i--) {
            String[] f = entries[i].split(",");
            if (f.length < 6) continue;
            try {
                int r = Integer.parseInt(f[0].trim());
                int g = Integer.parseInt(f[1].trim());
                int b = Integer.parseInt(f[2].trim());
                int br = Integer.parseInt(f[3].trim());
                int dim = Integer.parseInt(f[4].trim());
                int dm = Integer.parseInt(f[5].trim());
                addCapturedUnique(r, g, b, br, dim, dm);
            } catch (NumberFormatException ignored) { }
        }
    }

    private void refreshCapturedList() {
        if (capList == null) return;
        capList.removeAllViews();
        if (captured.isEmpty()) {
            TextView empty = text("No colors captured yet in this session.\nTap \"Capture current\" to log the active color, or \"Auto-capture\" before cycling colors in the car's Ambient Light settings.", 12.5f, MUTED);
            capList.addView(empty);
            return;
        }
        for (int i = 0; i < captured.size(); i++) {
            int[] e = captured.get(i);
            int r = e[0], g = e[1], b = e[2], br = e[3], dim = e[4], dm = e[5];
            LinearLayout row = new LinearLayout(getContext());
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(4), dp(4), dp(4), dp(4));

            View sw = new View(getContext());
            int preview = Color.rgb(r & 0xFF, g & 0xFF, b & 0xFF);
            sw.setBackgroundDrawable(Widgets.shape(preview, OUTLINE, dp(1), dp(6)));
            LayoutParams sp = new LayoutParams(dp(22), dp(22));
            sp.rightMargin = dp(10);
            row.addView(sw, sp);

            boolean stock01 = (r >= 256 || g >= 256 || b >= 256);
            String tag = dm == 2 ? "Drive Mode" : stock01 ? "Stock 0x01xx" : "Wheel";
            int uiBr = Math.max(0, br - 1);
            String line = String.format("RGB %d, %d, %d  (0x%04X, 0x%04X, 0x%04X)  ·  %s  ·  Bright %d%s",
                    r, g, b, r, g, b, tag, uiBr, dim == 2 ? " · DimDark" : "");
            TextView tv = text(line, 12.5f, TEXT);
            if (i == 0) tv.setTypeface(Typeface.DEFAULT_BOLD);
            tv.setSingleLine(true);
            tv.setEllipsize(TextUtils.TruncateAt.END);
            row.addView(tv, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            capList.addView(row);
        }
    }

    // ---------- Left Card: 15s LED & Brightness Test (USB-only) ----------

    private void showTestIntro() {
        leftBody.removeAllViews();
        TextView h = text("15-second LED & brightness test", 16.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        leftBody.addView(h);

        TextView t = text("Run while parked. Cycles Red, Green and Blue, then Low (1), High (10) and Mid (5) brightness, and restores your own ambient color and brightness.\n\n"
                + "Afterward, you can save the full hardware & audio diagnostic log directly to a USB stick (\"Light Vibes reports/\").", 13f, MUTED);
        t.setPadding(0, dp(6), 0, dp(14));
        leftBody.addView(t);

        leftBody.addView(new View(getContext()), new LayoutParams(1, 0, 1));

        TextView go = pill("Start 15s LED test", 1, 14f, new OnClickListener() {
            @Override public void onClick(View v) { startTest(); }
        });
        leftBody.addView(go, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));
    }

    private void startTest() {
        if (activeOp == MODE_WATCH) writeRequest("watch stop");
        diag.mkdirs();
        result.delete();
        testMap = new HashMap<String, String>();
        seenColour = seenBright = null;
        activeOp = MODE_TEST;
        startedAt = System.currentTimeMillis();
        syncCaptureButtons();
        writeRequest("test");

        leftBody.removeAllViews();
        TextView h = text("Watch the ambient light", 16.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        leftBody.addView(h);

        stageLine = text("Starting…", 13.5f, ACCENT);
        stageLine.setPadding(0, dp(2), 0, dp(8));
        leftBody.addView(stageLine);

        stepRows = new TextView[STEPS.length];
        stepSwatches = new View[STEPS.length];
        LinearLayout grid = new LinearLayout(getContext());
        grid.setOrientation(HORIZONTAL);
        LinearLayout c1 = new LinearLayout(getContext()); c1.setOrientation(VERTICAL);
        LinearLayout c2 = new LinearLayout(getContext()); c2.setOrientation(VERTICAL);

        for (int i = 0; i < STEPS.length; i++) {
            LinearLayout row = new LinearLayout(getContext());
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, dp(4), dp(6), dp(4));
            View sw = new View(getContext());
            stepSwatches[i] = sw;
            if (STEP_SWATCH[i] != 0) swatch(sw, STEP_SWATCH[i]);
            row.addView(sw, new LayoutParams(dp(18), dp(18)));
            stepRows[i] = text(STEP_NAMES[i], 13f, MUTED);
            stepRows[i].setPadding(dp(8), 0, 0, 0);
            stepRows[i].setSingleLine(true);
            row.addView(stepRows[i]);
            (i < 4 ? c1 : c2).addView(row);
        }
        grid.addView(c1, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        grid.addView(c2, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        leftBody.addView(grid);
        startPolling();
    }

    private void swatch(View v, int colour) {
        v.setBackgroundDrawable(Widgets.shape(colour, OUTLINE, dp(1), dp(4)));
    }

    private int userColour() {
        String u = testMap.get("user");
        if (u == null) return 0;
        String[] p = u.split(",");
        if (p.length < 3) return 0;
        try {
            return 0xFF000000 | ((Integer.parseInt(p[0].trim()) & 0xFF) << 16)
                    | ((Integer.parseInt(p[1].trim()) & 0xFF) << 8)
                    | (Integer.parseInt(p[2].trim()) & 0xFF);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private void showStep(String stage) {
        int now = -1;
        for (int i = 0; i < STEPS.length; i++) if (STEPS[i].equals(stage)) now = i;
        if (now < 0 || stepRows == null) return;
        int own = userColour();
        if (own != 0) { swatch(stepSwatches[0], own); swatch(stepSwatches[STEPS.length - 1], own); }
        stageLine.setText(now == 0 ? "Starting…" : now == STEPS.length - 1 ? "Restoring your color…" : "The light should now show:");
        for (int i = 0; i < stepRows.length; i++) {
            TextView t = stepRows[i];
            t.setText((i <= now ? "✓ " : "") + STEP_NAMES[i]);
            t.setTextColor(i == now ? ACCENT : i < now ? TEXT : MUTED);
            t.setTypeface(i == now ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        }
    }

    private void showQuestions() {
        leftBody.removeAllViews();
        TextView h = text("What did you see?", 16.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        h.setPadding(0, 0, 0, dp(6));
        leftBody.addView(h);

        leftBody.addView(question("Did the color change?", true));
        leftBody.addView(question("Did the brightness change?", false));

        leftBody.addView(new View(getContext()), new LayoutParams(1, 0, 1));

        TextView again = pill("Test again", 0, 13.5f, new OnClickListener() {
            @Override public void onClick(View v) { startTest(); }
        });
        leftBody.addView(again, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38)));
    }

    private View question(final String q, final boolean colour) {
        LinearLayout row = new LinearLayout(getContext());
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));
        row.addView(text(q, 14f, TEXT), new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        String now = colour ? seenColour : seenBright;
        String[] answers = {"yes", "no"};
        for (final String a : answers) {
            LayoutParams p = new LayoutParams(dp(74), dp(38));
            p.leftMargin = dp(8);
            row.addView(pill(a.equals("yes") ? "Yes" : "No", a.equals(now) ? 1 : 0, 13.5f, new OnClickListener() {
                @Override public void onClick(View v) {
                    if (colour) seenColour = a; else seenBright = a;
                    if (seenColour != null && seenBright != null) showTestResult(true);
                    else showQuestions();
                }
            }), p);
        }
        return row;
    }

    private void showTestResult(boolean triggerSave) {
        leftBody.removeAllViews();
        Report report = new Report(testMap, seenColour, seenBright);

        TextView h = text(report.heading, 16.5f, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        h.setPadding(0, 0, 0, dp(6));
        leftBody.addView(h);

        TextView det = text(report.detail, 12.5f, TEXT);
        leftBody.addView(det);

        usbLine = text(testMap.containsKey("stage") ? "Saving report to USB…" : "", 12f, ACCENT);
        usbLine.setPadding(0, dp(8), 0, dp(8));
        leftBody.addView(usbLine);

        leftBody.addView(new View(getContext()), new LayoutParams(1, 0, 1));

        LinearLayout actions = new LinearLayout(getContext());
        TextView saveUsb = pill("Save to USB", 1, 13.5f, new OnClickListener() {
            @Override public void onClick(View v) { requestUsbSave(); }
        });
        actions.addView(saveUsb, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38)));

        TextView again = pill("Test again", 0, 13.5f, new OnClickListener() {
            @Override public void onClick(View v) { startTest(); }
        });
        LayoutParams ap = new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(38));
        ap.leftMargin = dp(8);
        actions.addView(again, ap);
        leftBody.addView(actions);

        if (triggerSave && testMap.containsKey("stage")) {
            requestUsbSave();
        }
    }

    private void requestUsbSave() {
        if (!testMap.containsKey("stage")) return;
        result.delete();
        if (usbLine != null) {
            usbLine.setText("Saving report to USB…");
            usbLine.setTextColor(ACCENT);
        }
        activeOp = MODE_SAVE;
        startedAt = System.currentTimeMillis();
        syncCaptureButtons();
        writeRequest("save colour=" + (seenColour == null ? "unknown" : seenColour)
                + " bright=" + (seenBright == null ? "unknown" : seenBright));
        startPolling();
    }

    private void showUsb(String usb, String kept) {
        if (usbLine == null) return;
        if (usb != null && !usb.equals("none")) {
            usbLine.setText("Saved to USB: Light Vibes reports/");
            usbLine.setTextColor(ACCENT);
        } else if ("1".equals(kept)) {
            usbLine.setText("No USB stick detected — plug in a USB drive and tap \"Save to USB\".");
            usbLine.setTextColor(MUTED);
        } else {
            usbLine.setText("Couldn't write report — check USB drive and tap \"Save to USB\".");
            usbLine.setTextColor(MUTED);
        }
    }

    // ---------- Shared Polling & Result Dispatch ----------

    private void startPolling() {
        polling = true;
        handler.removeCallbacks(poll);
        handler.post(poll);
    }

    private void writeRequest(String v) {
        diag.mkdirs();
        File tmp = new File(diag, ".request.tmp");
        try {
            FileOutputStream o = new FileOutputStream(tmp);
            try { o.write((v + "\n").getBytes("UTF-8")); } finally { o.close(); }
            tmp.renameTo(request);
        } catch (IOException e) {
            tmp.delete();
        }
    }

    private Map<String, String> readResult() {
        StringBuilder s = new StringBuilder();
        BufferedReader r = null;
        try {
            r = new BufferedReader(new FileReader(result));
            String line;
            while ((line = r.readLine()) != null) s.append(line).append('\n');
        } catch (IOException e) {
            return new HashMap<String, String>();
        } finally {
            if (r != null) try { r.close(); } catch (IOException ignored) { }
        }
        return Report.parse(s.toString());
    }

    private static String usbShort(String usb) {
        return (usb != null && !usb.equals("none") && !usb.equals("ready")) ? "Saved to USB" : "No USB stick (shown on screen)";
    }

    private void tick() {
        Map<String, String> m = readResult();
        String stage = m.get("stage");
        long waited = System.currentTimeMillis() - startedAt;

        if (activeOp == MODE_SAVE) {
            if ("saved".equals(stage)) {
                testMap = m;
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                showUsb(m.get("usb"), m.get("kept"));
            } else if (waited > SAVE_WAIT_MS) {
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                showUsb(null, null);
            }
            return;
        }

        if (activeOp == MODE_TEST) {
            if (stage == null) {
                if (waited > START_TIMEOUT_MS) {
                    request.delete();
                    activeOp = MODE_NONE;
                    polling = false;
                    syncCaptureButtons();
                    testMap = m;
                    showTestResult(false);
                }
                return;
            }
            testMap = m;
            if (stage.equals("done") || stage.equals("saved")) {
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                showQuestions();
            } else if (waited > TEST_TIMEOUT_MS) {
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                showQuestions();
            } else if (stageLine != null) {
                showStep(stage);
            }
            return;
        }

        if (activeOp == MODE_SNAP) {
            if ("snap_done".equals(stage)) {
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                if ("1".equals(m.get("reply")) && m.get("rgb") != null) {
                    String[] p = m.get("rgb").split(",");
                    if (p.length >= 3) {
                        try {
                            int r = Integer.parseInt(p[0].trim());
                            int g = Integer.parseInt(p[1].trim());
                            int b = Integer.parseInt(p[2].trim());
                            int br = Integer.parseInt(m.get("bright") != null ? m.get("bright") : "1");
                            int dim = Integer.parseInt(m.get("dim") != null ? m.get("dim") : "1");
                            int dm = Integer.parseInt(m.get("dm") != null ? m.get("dm") : "1");
                            addCapturedUnique(r, g, b, br, dim, dm);
                            refreshCapturedList();
                            capStatus.setText(String.format("Captured RGB %d, %d, %d (%s) · %s",
                                    r, g, b, m.get("hex"), usbShort(m.get("usb"))));
                            capStatus.setTextColor(ACCENT);
                            return;
                        } catch (NumberFormatException ignored) { }
                    }
                }
                capStatus.setText("Lamp didn't reply to status request — make sure ignition/accessory is on");
                capStatus.setTextColor(MUTED);
            } else if (waited > SNAP_TIMEOUT_MS) {
                activeOp = MODE_NONE;
                polling = false;
                syncCaptureButtons();
                capStatus.setText("Light Vibes service didn't answer — check HMG-API service status");
                capStatus.setTextColor(MUTED);
            }
            return;
        }

        if (activeOp == MODE_WATCH) {
            if ("watching".equals(stage) || "watch_done".equals(stage)) {
                mergeItemsString(m.get("items"));
                refreshCapturedList();
                String cnt = m.get("watch_count") != null ? m.get("watch_count") : String.valueOf(captured.size());
                String usb = m.get("usb");
                String usbTxt = (usb != null && !usb.equals("none")) ? "USB ready" : "no USB drive";
                if ("watch_done".equals(stage)) {
                    activeOp = MODE_NONE;
                    polling = false;
                    syncCaptureButtons();
                    capStatus.setText("Auto-capture finished · " + cnt + " unique color(s) (" + usbTxt + ")");
                    capStatus.setTextColor(MUTED);
                } else {
                    capStatus.setText("Watching (1 Hz): " + cnt + " unique color(s) logged (" + usbTxt + ") · switch to car settings");
                    capStatus.setTextColor(ACCENT);
                }
            }
            return;
        }
    }
}
