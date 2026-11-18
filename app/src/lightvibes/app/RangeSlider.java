package lightvibes.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/**
 * Material Design 3 Expressive Slider: a rounded 12dp track with a crisp vertical bar handle, clean gap around the
 * handle, and end-stop indicators. Supports both single-handle level() and two-handle range modes, while preserving
 * vertical scroll pass-through inside ScrollViews.
 */
final class RangeSlider extends View {
    interface Listener {
        void moved(int low, int high);
        void done(int low, int high);
    }

    private final int min, max, step;
    private int low, high, dragging = -1, gap = 1;
    private final boolean single;
    private Listener listener;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final Path path = new Path();
    private final float density;
    private float downX, downY;
    private boolean sideways;

    static RangeSlider level(Context c, int min, int max, int step, int value, int trackColour, int activeColour, int knobColour) {
        return new RangeSlider(c, min, max, step, min, value, trackColour, activeColour, knobColour, true);
    }

    RangeSlider(Context c, int min, int max, int step, int low, int high, int trackColour, int activeColour, int knobColour) {
        this(c, min, max, step, low, high, trackColour, activeColour, knobColour, false);
    }

    private RangeSlider(Context c, int min, int max, int step, int low, int high, int trackColour, int activeColour, int knobColour, boolean single) {
        super(c);
        this.single = single;
        this.min = min;
        this.max = max;
        this.step = step;
        density = c.getResources().getDisplayMetrics().density;
        this.low = snap(low);
        this.high = snap(high);
        keepApart();
    }

    void setListener(Listener l) {
        listener = l;
    }

    RangeSlider gap(int steps) {
        gap = steps;
        keepApart();
        return this;
    }

    int low() { return low; }
    int high() { return high; }

    private void keepApart() {
        if (single) return;
        if (high < low + gap * step) high = Math.min(max, low + gap * step);
        if (high < low + gap * step) low = high - gap * step;
    }

    private int snap(int v) {
        int s = min + Math.round((v - min) / (float) step) * step;
        return Math.max(min, Math.min(max, s));
    }

    private float dp(float v) { return v * density; }
    private float pad() { return dp(14); }

    private float xOf(int v) {
        if (max <= min) return pad();
        return pad() + (getWidth() - 2 * pad()) * (v - min) / (float) (max - min);
    }

    private int valueAt(float x) {
        float span = Math.max(1f, getWidth() - 2 * pad());
        float f = (x - pad()) / span;
        return snap(Math.round(min + f * (max - min)));
    }

    /** Draws a track segment with asymmetric left/right corner radii (M3 outer 6dp pill cap vs inner 2dp gap edge). */
    private void drawSegment(Canvas c, float l, float t, float r, float b, float rLeft, float rRight, int color) {
        if (r - l < dp(1.5f)) return;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        rect.set(l, t, r, b);
        float maxR = Math.min((b - t) / 2f, (r - l) / 2f);
        float rl = Math.min(rLeft, maxR), rr = Math.min(rRight, maxR);
        path.reset();
        path.addRoundRect(rect, new float[] {rl, rl, rr, rr, rr, rr, rl, rl}, Path.Direction.CW);
        c.drawPath(path, paint);
    }

    @Override protected void onDraw(Canvas c) {
        float y = getHeight() / 2f;
        float halfTrack = dp(6f);      // 12dp M3 expressive track height
        float outerR = dp(6f);
        float innerR = dp(2f);
        float handleGap = dp(5.5f);    // M3 gap between track segments and vertical bar handle
        float leftEnd = pad(), rightEnd = getWidth() - pad();
        float t = y - halfTrack, b = y + halfTrack;

        if (single) {
            float hx = xOf(high);
            drawSegment(c, leftEnd, t, hx - handleGap, b, outerR, innerR, Ui.ACCENT);
            drawSegment(c, hx + handleGap, t, rightEnd, b, innerR, outerR, Ui.TRACK);
            if (rightEnd - (hx + handleGap) > dp(8)) {
                paint.setColor(Ui.ACCENT);
                c.drawCircle(rightEnd - dp(4), y, dp(2f), paint);
            }
            drawHandle(c, hx, y, dragging == 1);
        } else {
            float lx = xOf(low), hx = xOf(high);
            drawSegment(c, leftEnd, t, lx - handleGap, b, outerR, innerR, Ui.TRACK);
            drawSegment(c, lx + handleGap, t, hx - handleGap, b, innerR, innerR, Ui.ACCENT);
            drawSegment(c, hx + handleGap, t, rightEnd, b, innerR, outerR, Ui.TRACK);
            if (lx - handleGap - leftEnd > dp(8)) {
                paint.setColor(Ui.OUTLINE);
                c.drawCircle(leftEnd + dp(4), y, dp(2f), paint);
            }
            if (rightEnd - (hx + handleGap) > dp(8)) {
                paint.setColor(Ui.OUTLINE);
                c.drawCircle(rightEnd - dp(4), y, dp(2f), paint);
            }
            drawHandle(c, lx, y, dragging == 0);
            drawHandle(c, hx, y, dragging == 1);
        }
    }

    private void drawHandle(Canvas c, float x, float y, boolean activeDrag) {
        float hw = activeDrag ? dp(3f) : dp(2.2f);   // 4.4dp resting, 6dp pressed vertical M3 handle
        float hh = dp(14f);                          // 28dp tall handle
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(Ui.ACCENT);
        rect.set(x - hw, y - hh, x + hw, y + hh);
        c.drawRoundRect(rect, hw, hw, paint);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX();
        switch (e.getAction()) {
            case MotionEvent.ACTION_DOWN: {
                float dl = single ? Float.MAX_VALUE : Math.abs(x - xOf(low)), dh = Math.abs(x - xOf(high)), grab = dp(32);
                if (single) {
                    // Allow direct tap/drag anywhere along a single-handle M3 slider track once dragged horizontally, or near thumb
                    if (x < pad() - grab || x > getWidth() - pad() + grab) return false;
                    dragging = 1;
                } else {
                    if (Math.min(dl, dh) > grab) return false;
                    dragging = dl < dh || (dl == dh && x < xOf(low)) ? 0 : 1;
                }
                downX = x;
                downY = e.getY();
                sideways = false;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE:
                if (dragging < 0) return true;
                if (!sideways) {
                    float dx = Math.abs(x - downX), dy = Math.abs(e.getY() - downY);
                    int slop = ViewConfiguration.get(getContext()).getScaledTouchSlop();
                    if (dx < slop || dx < dy) return true;
                    sideways = true;
                    getParent().requestDisallowInterceptTouchEvent(true);
                }
                applyTouch(x);
                return true;
            case MotionEvent.ACTION_UP:
                if (dragging >= 0) {
                    if (!sideways && Math.abs(x - downX) < dp(12) && Math.abs(e.getY() - downY) < dp(12)) {
                        applyTouch(x);
                        sideways = true;
                    }
                    if (sideways && listener != null) listener.done(low, high);
                }
                dragging = -1;
                sideways = false;
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                if (dragging >= 0 && sideways && listener != null) listener.done(low, high);
                dragging = -1;
                sideways = false;
                invalidate();
                return true;
        }
        return super.onTouchEvent(e);
    }

    private void applyTouch(float x) {
        int v = valueAt(x);
        if (dragging == 0) low = Math.max(min, Math.min(v, high - gap * step));
        else high = Math.min(max, single ? v : Math.max(v, low + gap * step));
        invalidate();
        if (listener != null) listener.moved(low, high);
    }
}
