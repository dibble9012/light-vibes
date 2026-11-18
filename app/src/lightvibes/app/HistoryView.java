package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * The current drive's reading over time (speed, power or efficiency), drawn in the colour the ambient light had, or
 * would have had, at each moment with the colours and range set now. lightvibesd records a row a second; the app reads
 * what's on disk (/data/hmgapi/public/light-vibes/drive_0.csv) and gets every new row live on the meter socket
 * ("hist <row>", "histdrive <start>"); see history_tick() in lightvibesd.c.
 */
final class HistoryView extends View {
    /** What the line's colour is for a value: the same pick as lightvibesd's. */
    interface Colours {
        int at(double value);
    }

    /** Which reading the graph shows, in the unit shown (NaN where there wasn't one). */
    interface Reading {
        double of(Drive d, int i);
    }

    /** A drive as lightvibesd records it: seconds since the epoch, km/h, kW, and mi/kWh (this trip, car's average). */
    static final class Drive {
        long start;
        int n;
        long[] t = new long[256];
        double[] speed = new double[256], power = new double[256], trip = new double[256], avg = new double[256];

        long seconds() { return n < 2 ? 0 : t[n - 1] - t[0]; }

        /** one row as lightvibesd writes it: "epoch km/h kW trip avg", "-" for none; older or repeated rows are skipped */
        boolean add(String line) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 5) return false;
            long ts = parseLong(p[0]);
            if (ts == 0 || (n > 0 && ts <= t[n - 1])) return false;
            if (n == t.length) {
                int c = n * 2;
                t = Arrays.copyOf(t, c); speed = Arrays.copyOf(speed, c); power = Arrays.copyOf(power, c);
                trip = Arrays.copyOf(trip, c); avg = Arrays.copyOf(avg, c);
            }
            t[n] = ts; speed[n] = num(p[1]); power[n] = num(p[2]); trip[n] = num(p[3]); avg[n] = num(p[4]);
            n++;
            if (start == 0) start = ts;
            return true;
        }
    }

    static final String DIR = "/data/hmgapi/public/light-vibes";

    /** the current (or last) drive, as far as it's on disk */
    static Drive load() {
        Drive d = new Drive();
        BufferedReader r = null;
        try {
            r = new BufferedReader(new FileReader(new File(DIR, "drive_0.csv")));
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("# drive ")) {
                    String[] p = line.substring(8).trim().split("\\s+");
                    if (p.length >= 3) d.start = parseLong(p[2]);
                } else d.add(line);
            }
        } catch (IOException ignored) {
        } finally {
            if (r != null) try { r.close(); } catch (IOException ignored) { }
        }
        return d;
    }

    private static long parseLong(String s) {
        try { return Long.parseLong(s); } catch (NumberFormatException e) { return 0; }
    }

    private static double num(String s) {
        if ("-".equals(s)) return Double.NaN;
        try { return Double.parseDouble(s); } catch (NumberFormatException e) { return Double.NaN; }
    }

    /** "Started 4:12 PM  ·  23 min" */
    static String describe(Drive d) {
        String when = new SimpleDateFormat("h:mm a", Locale.US).format(new Date(d.start * 1000));
        long min = d.seconds() / 60;
        return "Started " + when + "  ·  " + (min < 60 ? min + " min" : (min / 60) + " h " + (min % 60) + " min");
    }

    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG), guide = new Paint(Paint.ANTI_ALIAS_FLAG),
            label = new Paint(Paint.ANTI_ALIAS_FLAG), card = new Paint(Paint.ANTI_ALIAS_FLAG),
            border = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final float density;
    private Drive drive = new Drive();
    private Reading reading;
    private double low, high;
    private boolean zeroLine;
    private Colours colours;

    HistoryView(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(3 * density);
        line.setStrokeCap(Paint.Cap.ROUND);
        guide.setStyle(Paint.Style.STROKE);
        guide.setStrokeWidth(1 * density);
        guide.setPathEffect(new DashPathEffect(new float[] {4 * density, 4 * density}, 0));
        border.setStyle(Paint.Style.STROKE);
        border.setStrokeWidth(1 * density);
        label.setTextSize(12 * density);
    }

    /** low and high are the range's ends in the unit shown */
    void set(Drive d, Reading reading, double low, double high, boolean zeroLine, Colours colours) {
        drive = d; this.reading = reading; this.zeroLine = zeroLine; this.colours = colours;
        setRange(low, high);
    }

    Drive drive() { return drive; }
    double low() { return low; }
    double high() { return high; }

    /** the range moved: the guide lines and every colour follow */
    void setRange(double low, double high) {
        this.low = low; this.high = high;
        invalidate();
    }

    @Override protected void onDraw(Canvas c) {
        float w = getWidth(), h = getHeight(), pad = 8 * density, half = 0.5f * density;
        card.setColor(SURFACE_LOW);
        border.setColor(RULE);
        rect.set(half, half, w - half, h - half);
        c.drawRoundRect(rect, 12 * density, 12 * density, card);
        c.drawRoundRect(rect, 12 * density, 12 * density, border);
        Drive d = drive;
        int n = d.n;
        if (n < 2 || colours == null || reading == null) {
            label.setColor(MUTED);
            label.setTextAlign(Paint.Align.CENTER);
            c.drawText("Fills in live once the car is READY", w / 2f, h / 2f + 4 * density, label);
            return;
        }

        float left = 54 * density, right = w - pad, top = pad, bottom = h - 20 * density;
        long t0 = d.t[0], t1 = d.t[n - 1]; if (t1 <= t0) t1 = t0 + 1;

        // one point per pixel column at most: an hour-long drive is 3600 rows, more than the graph is wide
        int cols = Math.max(2, (int) (right - left));
        double[] sum = new double[cols]; int[] cnt = new int[cols];
        for (int i = 0; i < n; i++) {
            double v = reading.of(d, i);
            if (Double.isNaN(v)) continue;
            int x = (int) ((d.t[i] - t0) / (double) (t1 - t0) * (cols - 1));
            sum[x] += v; cnt[x]++;
        }

        // the scale: everything recorded, and the range, with a little room
        double lo = Math.min(low, high), hi = Math.max(low, high);
        for (int x = 0; x < cols; x++) if (cnt[x] > 0) { double v = sum[x] / cnt[x]; lo = Math.min(lo, v); hi = Math.max(hi, v); }
        if (zeroLine) { lo = Math.min(lo, 0); hi = Math.max(hi, 0); }
        double span = hi - lo; if (span <= 0) span = 1;
        lo -= span * 0.06; hi += span * 0.06;

        // guide lines at the range's ends (and Power's zero), labelled on the left
        label.setColor(MUTED);
        label.setTextAlign(Paint.Align.RIGHT);
        guide.setColor(MUTED);
        double[] guides = zeroLine ? new double[] {low, 0, high} : new double[] {low, high};
        for (double g : guides) {
            float y = (float) (bottom - (g - lo) / (hi - lo) * (bottom - top));
            c.drawLine(left, y, right, y, guide);
            c.drawText(fmt(g), left - 6 * density, y + 4 * density, label);
        }
        // time along the bottom, in minutes into the drive
        label.setTextAlign(Paint.Align.LEFT);
        c.drawText("0 min", left, h - 5 * density, label);
        label.setTextAlign(Paint.Align.CENTER);
        c.drawText(minutes((t1 - t0) / 2), (left + right) / 2, h - 5 * density, label);
        label.setTextAlign(Paint.Align.RIGHT);
        c.drawText(minutes(t1 - t0), right, h - 5 * density, label);

        // the line, column to column in its colour; a gap in the readings (over 30 s) is a gap in the line
        double secPerCol = (t1 - t0) / (double) (cols - 1);
        int gapCols = Math.max(1, (int) Math.ceil(30 / Math.max(secPerCol, 1e-6)));
        int prev = -1;
        for (int x = 0; x < cols; x++) {
            if (cnt[x] == 0) continue;
            if (prev >= 0 && x - prev <= gapCols) {
                double a = sum[prev] / cnt[prev], b = sum[x] / cnt[x];
                float x0 = left + prev, x1 = left + x;
                float y0 = (float) (bottom - (a - lo) / (hi - lo) * (bottom - top));
                float y1 = (float) (bottom - (b - lo) / (hi - lo) * (bottom - top));
                line.setColor(colours.at((a + b) / 2));
                c.drawLine(x0, y0, x1, y1, line);
            }
            prev = x;
        }
    }

    private static String minutes(long seconds) {
        return seconds < 120 ? seconds + " s" : Math.round(seconds / 60.0) + " min";
    }

    private String fmt(double x) {
        return Math.abs(x - Math.round(x)) < 0.05 ? String.valueOf(Math.round(x)) : String.format(Locale.US, "%.1f", x);
    }
}
