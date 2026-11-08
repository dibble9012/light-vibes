package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

/**
 * Report a problem: lightvibesd tests the lamp while the owner watches, the owner says what they saw, and a QR code
 * opens a filled-in GitHub issue on their phone. The service does the work, through files in files/diag/ (see
 * "lamp test" in lightvibesd.c). The car needs no internet: the phone sends the report, and only if the owner submits it.
 */
final class ReportPage extends LinearLayout {
    private static final int START_TIMEOUT_MS = 12000, TEST_TIMEOUT_MS = 60000, SAVE_WAIT_MS = 8000, POLL_MS = 500;

    private final File diag, request, result;
    private final Runnable close;
    private final Handler handler = new Handler();
    private Map<String, String> map = new HashMap<String, String>();
    private String seenColour, seenBright;
    private long startedAt;
    private boolean polling, saving;
    private TextView stageLine, usbLine;
    /* the test's steps, as lightvibesd reports them in "stage": what each one sends to the light */
    private static final String[] STEPS = {"start", "red", "green", "blue", "low", "high", "mid", "restoring"};
    private static final String[] STEP_NAMES = {"Reading your color", "Red", "Green", "Blue", "Low brightness (1)",
            "High brightness (10)", "Mid brightness (5)", "Putting your color and brightness back"};
    private static final int[] STEP_SWATCH = {0, 0xFFFE0145, 0xFF75DF62, 0xFF1043F4, 0x40FFFFFF, 0xFFFFFFFF, 0x99FFFFFF, 0};
    private TextView[] stepRows;
    private View[] stepSwatches;

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            tick();
            if (polling) handler.postDelayed(this, POLL_MS);
        }
    };

    ReportPage(Context c, File filesDir, Runnable close) {
        super(c);
        setOrientation(VERTICAL);
        diag = new File(filesDir, "diag");
        request = new File(diag, "request");
        result = new File(diag, "result");
        this.close = close;
        showIntro();
    }

    /** The page went away: the test carries on in the service and puts the colour back by itself. */
    void stop() {
        polling = false;
        handler.removeCallbacks(poll);
    }

    /** Back on screen after a pause. */
    void resume() {
        if (!polling && startedAt != 0 && (saving || seenColour == null)) startPolling();
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

    private TextView button(String label, boolean on, View.OnClickListener click) {
        TextView t = text(label, 18, on ? ON_TEXT : TEXT);
        t.setGravity(Gravity.CENTER);
        GradientDrawable g = new GradientDrawable();
        g.setColor(on ? CARD_ON : CARD);
        g.setCornerRadius(dp(8));
        t.setBackgroundDrawable(g);
        t.setOnClickListener(click);
        return t;
    }

    private void heading(String s) {
        TextView t = text(s, 22, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, dp(4), 0, dp(8));
        addView(t);
    }

    private void buttons(String first, View.OnClickListener a, String second, View.OnClickListener b) {
        LinearLayout row = new LinearLayout(getContext());
        row.setPadding(0, dp(16), 0, 0);
        row.addView(button(first, true, a), new LayoutParams(dp(220), dp(56)));
        LayoutParams p = new LayoutParams(dp(140), dp(56));
        p.leftMargin = dp(10);
        row.addView(button(second, false, b), p);
        addView(row);
    }

    private final View.OnClickListener closeClick = new View.OnClickListener() {
        @Override public void onClick(View v) {
            stop();
            close.run();
        }
    };

    // ---------- 1: what's about to happen ----------

    private void showIntro() {
        removeAllViews();
        heading("Report a problem");
        TextView t = text("First, test the ambient light. Do this while parked.\n\n"
                + "For about 15 seconds the light changes to red, green and blue, then goes low, high and mid brightness. "
                + "Watch it, because you'll be asked what you saw. Your own colour and brightness come back at the end.\n\n"
                + "Then a code on this screen opens a report on your phone. Nothing is sent unless you submit it.", 17, TEXT);
        addView(t);
        buttons("Start the test", new View.OnClickListener() {
            @Override public void onClick(View v) { start(); }
        }, "Close", closeClick);
    }

    // ---------- 2: the test ----------

    private void start() {
        diag.mkdirs();
        result.delete();
        map = new HashMap<String, String>();
        seenColour = seenBright = null;
        saving = false;
        writeRequest("test");
        startedAt = System.currentTimeMillis();
        removeAllViews();
        heading("Watch the ambient light");
        stageLine = text("Starting…", 18, ACCENT);
        stageLine.setPadding(0, 0, 0, dp(10));
        addView(stageLine);
        stepRows = new TextView[STEPS.length];
        stepSwatches = new View[STEPS.length];
        for (int i = 0; i < STEPS.length; i++) {
            LinearLayout row = new LinearLayout(getContext());
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, dp(3), 0, dp(3));
            View sw = new View(getContext());
            stepSwatches[i] = sw;
            if (STEP_SWATCH[i] != 0) swatch(sw, STEP_SWATCH[i]);
            row.addView(sw, new LayoutParams(dp(22), dp(22)));
            stepRows[i] = text(STEP_NAMES[i], 18, MUTED);
            stepRows[i].setPadding(dp(14), 0, 0, 0);
            row.addView(stepRows[i]);
            addView(row);
        }
        startPolling();
    }

    private void startPolling() {
        polling = true;
        handler.removeCallbacks(poll);
        handler.post(poll);
    }

    private void writeRequest(String v) {
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

    private void tick() {
        Map<String, String> m = readResult();
        String stage = m.get("stage");
        long waited = System.currentTimeMillis() - startedAt;
        if (saving) {
            if ("saved".equals(stage)) {
                map = m;
                polling = false;
                showUsb(m.get("usb"), m.get("kept"));
            } else if (waited > SAVE_WAIT_MS) {
                polling = false;
                showUsb(null, null);
            }
            return;
        }
        if (stage == null) {
            if (waited > START_TIMEOUT_MS) {   // no service to answer: the report can still say so
                request.delete();
                polling = false;
                map = m;
                showResult();
            }
            return;
        }
        map = m;
        if (stage.equals("done") || stage.equals("saved")) {
            polling = false;
            showQuestions();
        } else if (waited > TEST_TIMEOUT_MS) {
            polling = false;
            showQuestions();
        } else if (stageLine != null) {
            showStep(stage);
        }
    }

    private void swatch(View v, int colour) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(colour);
        g.setCornerRadius(dp(4));
        g.setStroke(dp(1), OUTLINE);
        v.setBackgroundDrawable(g);
    }

    /** The owner's own colour, from the service's "user=r,g,b,..." once it has read it; 0 until then. */
    private int userColour() {
        String u = map.get("user");
        if (u == null) return 0;
        String[] p = u.split(",");
        if (p.length < 3) return 0;
        try {
            return 0xFF000000 | Math.min(255, Integer.parseInt(p[0].trim())) << 16 | Math.min(255, Integer.parseInt(p[1].trim())) << 8
                    | Math.min(255, Integer.parseInt(p[2].trim()));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /** Ticks each step as soon as it's sent, so the owner knows what the light should be showing now; the current one stands out. */
    private void showStep(String stage) {
        int now = -1;
        for (int i = 0; i < STEPS.length; i++) if (STEPS[i].equals(stage)) now = i;
        if (now < 0 || stepRows == null) return;
        int own = userColour();
        if (own != 0) { swatch(stepSwatches[0], own); swatch(stepSwatches[STEPS.length - 1], own); }
        stageLine.setText(now == 0 ? "Starting…" : now == STEPS.length - 1 ? "Almost done" : "The light should now show:");
        for (int i = 0; i < stepRows.length; i++) {
            TextView t = stepRows[i];
            t.setText((i <= now ? "✓  " : "") + STEP_NAMES[i]);
            t.setTextColor(i == now ? ACCENT : i < now ? TEXT : MUTED);
            t.setTypeface(i == now ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        }
    }

    // ---------- 3: what the owner saw ----------

    private void showQuestions() {
        removeAllViews();
        heading("What did you see?");
        addView(question("Did the colour change?", true));
        addView(question("Did the brightness change?", false));
        // missed it? run the same test again before answering
        LinearLayout row = new LinearLayout(getContext());
        row.setPadding(0, dp(18), 0, 0);
        row.addView(button("Test again", false, new View.OnClickListener() {
            @Override public void onClick(View v) { start(); }
        }), new LayoutParams(dp(200), dp(56)));
        addView(row);
    }

    private View question(final String q, final boolean colour) {
        LinearLayout row = new LinearLayout(getContext());
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(8), 0, dp(8));
        row.addView(text(q, 18, TEXT), new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        String now = colour ? seenColour : seenBright;
        String[] answers = {"yes", "no"};
        for (final String a : answers) {
            LayoutParams p = new LayoutParams(dp(110), dp(56));
            p.leftMargin = dp(10);
            row.addView(button(a.equals("yes") ? "Yes" : "No", a.equals(now), new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (colour) seenColour = a; else seenBright = a;
                    if (seenColour != null && seenBright != null) showResult();
                    else showQuestions();
                }
            }), p);
        }
        return row;
    }

    // ---------- 4: the result and the code ----------

    private void showResult() {
        removeAllViews();
        Report report = new Report(map, seenColour, seenBright);
        LinearLayout row = new LinearLayout(getContext());
        LinearLayout left = new LinearLayout(getContext());
        left.setOrientation(VERTICAL);
        TextView h = text(report.heading, 22, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        h.setPadding(0, dp(4), 0, dp(8));
        left.addView(h);
        left.addView(text(report.detail, 16, TEXT));
        TextView how = text("Scan the code with your phone to open the report, add your car, and submit it.", 16, ACCENT);
        how.setPadding(0, dp(12), 0, 0);
        left.addView(how);
        usbLine = text(map.containsKey("stage") ? "Saving the full report…" : "", 14, MUTED);
        usbLine.setPadding(0, dp(10), 0, 0);
        left.addView(usbLine);
        LinearLayout actions = new LinearLayout(getContext());
        actions.setPadding(0, dp(14), 0, 0);
        actions.addView(button("Test again", false, new View.OnClickListener() {
            @Override public void onClick(View v) { showIntro(); }
        }), new LayoutParams(0, dp(52), 1));
        LayoutParams cp = new LayoutParams(0, dp(52), 1);
        cp.leftMargin = dp(8);
        actions.addView(button("Close", false, closeClick), cp);
        left.addView(actions);
        row.addView(left, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        LayoutParams qp = new LayoutParams(dp(390), dp(390));
        qp.leftMargin = dp(16);
        row.addView(new QrView(getContext(), report.issueUrl()), qp);
        addView(row);

        if (map.containsKey("stage")) {
            result.delete();     // so we only read the service's answer to this save
            writeRequest("save colour=" + answer(seenColour) + " bright=" + answer(seenBright));
            saving = true;
            startedAt = System.currentTimeMillis();
            startPolling();
        }
    }

    private static String answer(String a) {
        return a == null ? "unknown" : a;
    }

    private void showUsb(String usb, String kept) {
        if (usbLine == null) return;
        if (usb != null && !usb.equals("none")) {
            usbLine.setText("Full report saved on your USB stick, in \"Light Vibes reports\". You can attach it to the issue.");
        } else if ("1".equals(kept)) {
            usbLine.setText("To attach the full report as well, plug in a USB stick and test again.");
        } else {
            usbLine.setText("The full report couldn't be saved. The code still works.");
        }
    }

}
