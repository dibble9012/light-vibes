package lightvibes.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/**
 * The app's slider: one track with two handles for a low and a high value, or one handle (level()) for a plain level
 * such as brightness. Only a touch on a handle drags it; anywhere else on it, the page scrolls as usual.
 * Values are whole numbers from min to max in steps; the listener hears every move, and done() when the finger lifts.
 * gap() sets how many steps apart the two handles must stay (1 by default; 0 lets them meet).
 */
final class RangeSlider extends View {
    interface Listener {
        void moved(int low, int high);
        void done(int low, int high);
    }

    private final int min, max, step;
    private int low, high, dragging = -1, gap = 1;
    private boolean single;
    private Listener listener;
    private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG), active = new Paint(Paint.ANTI_ALIAS_FLAG),
            knob = new Paint(Paint.ANTI_ALIAS_FLAG), ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private float downX, downY;
    private boolean sideways;     // the finger has moved sideways: until then a vertical swipe is the page's to scroll

    /** One handle, for a plain level: the value is high(). */
    static RangeSlider level(Context c, int min, int max, int step, int value, int trackColour, int activeColour, int knobColour) {
        RangeSlider r = new RangeSlider(c, min, max, step, min, value, trackColour, activeColour, knobColour, true);
        return r;
    }

    RangeSlider(Context c, int min, int max, int step, int low, int high, int trackColour, int activeColour, int knobColour) {
        this(c, min, max, step, low, high, trackColour, activeColour, knobColour, false);
    }

    private RangeSlider(Context c, int min, int max, int step, int low, int high, int trackColour, int activeColour, int knobColour, boolean single) {
        super(c);
        this.single = single;
        this.min = min; this.max = max; this.step = step;
        density = c.getResources().getDisplayMetrics().density;
        track.setColor(trackColour);
        active.setColor(activeColour);
        knob.setColor(knobColour);
        ring.setColor(activeColour);
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeWidth(3 * density);
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

    private float pad() { return 16 * density; }

    private float xOf(int v) {
        return pad() + (getWidth() - 2 * pad()) * (v - min) / (float) (max - min);
    }

    private int valueAt(float x) {
        float f = (x - pad()) / (getWidth() - 2 * pad());
        return snap(Math.round(min + f * (max - min)));
    }

    /* The handles are drawn like the head unit's own sliders (a darker disc with a bright centre dot, on a thin line), in
       the app's teal. Everything is drawn around the line's centre, so the
       handles sit exactly on it. */

    @Override protected void onDraw(Canvas c) {
        float y = getHeight() / 2f, h = 1.5f * density;
        c.drawRect(pad(), y - h, getWidth() - pad(), y + h, track);
        active.setColor(Ui.ACCENT);
        c.drawRect(single ? pad() : xOf(low), y - h, xOf(high), y + h, active);
        knob.setColor(Widgets.halo());
        ring.setStyle(Paint.Style.FILL);
        ring.setColor(Ui.ACCENT);
        for (int i = single ? 1 : 0; i < 2; i++) {
            float x = xOf(i == 0 ? low : high);
            c.drawCircle(x, y, 14 * density, knob);
            c.drawCircle(x, y, 3.75f * density, ring);
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX();
        switch (e.getAction()) {
            case MotionEvent.ACTION_DOWN: {
                float dl = single ? Float.MAX_VALUE : Math.abs(x - xOf(low)), dh = Math.abs(x - xOf(high)), grab = 32 * density;
                if (Math.min(dl, dh) > grab) return false;
                dragging = dl < dh || (dl == dh && x < xOf(low)) ? 0 : 1;
                downX = x; downY = e.getY(); sideways = false;
                return true;     // the page can still take a vertical swipe: it isn't told to keep off until we move sideways
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
                int v = valueAt(x);
                if (dragging == 0) low = Math.max(min, Math.min(v, high - gap * step));
                else high = Math.min(max, single ? v : Math.max(v, low + gap * step));
                invalidate();
                if (listener != null) listener.moved(low, high);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (dragging >= 0 && sideways && listener != null) listener.done(low, high);
                dragging = -1;
                sideways = false;
                return true;
        }
        return super.onTouchEvent(e);
    }
}
