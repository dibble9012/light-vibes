package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.View;

/**
 * What the analysis hears, the same numbers the presets were tuned from (lightvibesd's debug=2 meter):
 * band levels with the switching margin, beat strength with its threshold, loudness within the
 * brightness range. Fed one line at a time from the service; see meter_stream() in lightvibesd.c.
 */
final class MeterView extends View {
    /** threshold lines and the beat flash: white on the dark theme, the text colour on the light one */
    private static int markColour() { return night ? 0xFFFFFFFF : TEXT; }
    private static final float BAND_RANGE_DB = 30;
    private static final long STALE_MS = 800, FLASH_MS = 200, PEAK_HOLD_MS = 1000;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final float density;

    private boolean showBands, showBeat, showLoudness;
    private int[] colours = new int[0];
    private String[] names = new String[0];
    private int marginPercent, sensitivity, rangeDb;

    private float db, avgDb, beatPeak, heldPeak;
    private float[] bandDb = new float[0];
    private int selected = -1;
    private long lastData, lastBeat, heldAt;

    private final Runnable staleTick = new Runnable() {
        @Override public void run() {
            invalidate();
            postDelayed(this, 400);
        }
    };

    MeterView(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
    }

    void configure(boolean bands, boolean beat, boolean loudness, int[] colours, String[] names, int marginPercent, int sensitivity, int rangeDb) {
        showBands = bands;
        showBeat = beat;
        showLoudness = loudness;
        this.colours = colours;
        this.names = names;
        this.marginPercent = marginPercent;
        this.sensitivity = sensitivity;
        this.rangeDb = rangeDb;
        invalidate();
    }

    /** db average_db beat_peak beats selected_band nbands band_db... */
    void update(String line) {
        String[] p = line.trim().split(" ");
        if (p.length < 6) return;
        try {
            db = Float.parseFloat(p[0]);
            avgDb = Float.parseFloat(p[1]);
            beatPeak = Float.parseFloat(p[2]);
            int beats = Integer.parseInt(p[3]);
            selected = Integer.parseInt(p[4]);
            int n = Integer.parseInt(p[5]);
            if (p.length < 6 + n) return;
            float[] levels = new float[n];
            for (int i = 0; i < n; i++) levels[i] = Float.parseFloat(p[6 + i]);
            bandDb = levels;
            long now = SystemClock.uptimeMillis();
            lastData = now;
            if (beats > 0) lastBeat = now;
            if (beatPeak >= heldPeak || now - heldAt > PEAK_HOLD_MS) {
                heldPeak = beatPeak;
                heldAt = now;
            }
            invalidate();
        } catch (NumberFormatException ignored) {
        }
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        post(staleTick);
    }

    @Override protected void onDetachedFromWindow() {
        removeCallbacks(staleTick);
        super.onDetachedFromWindow();
    }

    private float dp(float v) {
        return v * density;
    }

    @Override protected void onDraw(Canvas c) {
        float w = getWidth(), h = getHeight(), pad = dp(10);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(CARD);
        rect.set(0, 0, w, h);
        c.drawRoundRect(rect, dp(8), dp(8), paint);

        long now = SystemClock.uptimeMillis();
        boolean live = lastData != 0 && now - lastData < STALE_MS;

        float left = pad, right = w - pad;
        if (showBands) {
            float end = showBeat || showLoudness ? w * 0.58f : right;
            drawBands(c, left, pad / 2, end, h - pad / 2, live);
            left = end + dp(14);
        }
        int rows = (showBeat ? 1 : 0) + (showLoudness ? 1 : 0);
        if (rows > 0) {
            float rowH = (h - pad) / rows, y = pad / 2;
            if (showBeat) {
                drawBeat(c, left, y, right, y + rowH, live, now);
                y += rowH;
            }
            if (showLoudness) drawLoudness(c, left, y, right, y + rowH, live);
        }
        if (!live) {
            paint.setColor((CARD & 0x00FFFFFF) | 0xCC000000);
            rect.set(0, 0, w, h);
            c.drawRoundRect(rect, dp(8), dp(8), paint);
            paint.setColor(TEXT);
            paint.setTextSize(dp(17));
            paint.setTextAlign(Paint.Align.CENTER);
            c.drawText("Meter starts when music plays", w / 2, h / 2 + dp(6), paint);
            paint.setTextAlign(Paint.Align.LEFT);
        }
    }

    private void drawBands(Canvas c, float l, float t, float r, float b, boolean live) {
        int n = Math.min(colours.length, live ? bandDb.length : colours.length);
        if (n == 0) return;
        float labelH = dp(16), barBottom = b - labelH, gap = dp(8), cellW = (r - l + gap) / n;
        // the level a band must beat to take over from the selected one
        float marginDb = (float) (10 * Math.log10(1 + marginPercent / 100.0));
        float top = -200;
        for (int i = 0; i < n && live; i++) top = Math.max(top, bandDb[i]);
        float line = selected >= 0 && selected < n && live ? bandDb[selected] + marginDb : top;
        top = Math.max(top, line);

        paint.setTextSize(dp(12));
        paint.setTextAlign(Paint.Align.CENTER);
        for (int i = 0; i < n; i++) {
            float x0 = l + i * cellW, x1 = x0 + cellW - gap;
            paint.setColor(TRACK);
            rect.set(x0, t, x1, barBottom);
            c.drawRoundRect(rect, dp(3), dp(3), paint);
            boolean sel = live && i == selected;
            if (live) {
                float f = clamp((bandDb[i] - top + BAND_RANGE_DB) / BAND_RANGE_DB);
                paint.setColor(colours[i]);
                paint.setAlpha(sel ? 255 : 110);
                rect.set(x0, barBottom - f * (barBottom - t), x1, barBottom);
                c.drawRoundRect(rect, dp(3), dp(3), paint);
                paint.setAlpha(255);
            }
            paint.setColor(sel ? TEXT : MUTED);
            paint.setFakeBoldText(sel);
            c.drawText(i < names.length ? names[i] : "", (x0 + x1) / 2, b - dp(2), paint);
            paint.setFakeBoldText(false);
        }
        paint.setTextAlign(Paint.Align.LEFT);
        if (live) {
            float y = barBottom - clamp((line - top + BAND_RANGE_DB) / BAND_RANGE_DB) * (barBottom - t);
            paint.setColor(markColour());
            c.drawRect(l, y - dp(1), r, y + dp(1), paint);
        }
    }

    private void drawBeat(Canvas c, float l, float t, float r, float b, boolean live, long now) {
        float maxPct = Math.max(400, sensitivity * 1.5f);
        float y0 = rowBar(c, l, t, r, b, "Beat peak", live ? Math.round(heldPeak * 100) + "%" : "");
        float x0 = l, x1 = r - dp(22);
        paint.setColor(TRACK);
        rect.set(x0, y0, x1, b - dp(4));
        c.drawRoundRect(rect, dp(3), dp(3), paint);
        if (live) {
            float f = clamp((beatPeak * 100 - 100) / (maxPct - 100));
            paint.setColor(ACCENT);
            rect.set(x0, y0, x0 + f * (x1 - x0), b - dp(4));
            c.drawRoundRect(rect, dp(3), dp(3), paint);
        }
        float tick = x0 + clamp((sensitivity - 100) / (maxPct - 100)) * (x1 - x0);
        paint.setColor(markColour());
        c.drawRect(tick - dp(1), y0 - dp(2), tick + dp(1), b - dp(2), paint);
        boolean flash = live && now - lastBeat < FLASH_MS;
        paint.setColor(flash ? markColour() : TRACK);
        c.drawCircle(r - dp(8), (y0 + b - dp(4)) / 2, dp(7), paint);
    }

    private void drawLoudness(Canvas c, float l, float t, float r, float b, boolean live) {
        float diff = db - avgDb;
        String value = live ? (diff >= 0 ? "+" : "") + Math.round(diff) + " dB" : "";
        float y0 = rowBar(c, l, t, r, b, "Loudness", value);
        paint.setColor(TRACK);
        rect.set(l, y0, r, b - dp(4));
        c.drawRoundRect(rect, dp(3), dp(3), paint);
        float mid = (l + r) / 2;
        if (live) {
            float x = l + clamp(0.5f + diff / (2f * Math.max(1, rangeDb))) * (r - l);
            paint.setColor(ACCENT);
            rect.set(Math.min(mid, x), y0, Math.max(mid, x), b - dp(4));
            c.drawRect(rect, paint);
        }
        paint.setColor(MUTED);
        c.drawRect(mid - dp(1), y0, mid + dp(1), b - dp(4), paint);
    }

    /** Draws a row's caption and value; returns where its bar starts. */
    private float rowBar(Canvas c, float l, float t, float r, float b, String caption, String value) {
        paint.setTextSize(dp(13));
        paint.setColor(MUTED);
        c.drawText(caption, l, t + dp(14), paint);
        paint.setColor(TEXT);
        paint.setTextAlign(Paint.Align.RIGHT);
        c.drawText(value, r, t + dp(14), paint);
        paint.setTextAlign(Paint.Align.LEFT);
        return t + dp(18);
    }

    private static float clamp(float v) {
        return v < 0 ? 0 : v > 1 ? 1 : v;
    }
}
