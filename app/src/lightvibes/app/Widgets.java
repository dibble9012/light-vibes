package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.widget.RadioButton;
import android.widget.Switch;

/**
 * Switches and radio buttons in the app's teal instead of Android 4.4's Holo blue. Only how they look changes: they
 * are still the system's own widgets, so they behave and read (accessibility) as before.
 */
final class Widgets {
    /** The handle colour for sliders: the accent, darkened, so the bright centre dot stands out. */
    static int halo() {
        return darker(ACCENT, 0.70f);
    }

    static int darker(int c, float f) {
        return 0xFF000000 | ((int) (((c >> 16) & 255) * f) << 16) | ((int) (((c >> 8) & 255) * f) << 8) | (int) ((c & 255) * f);
    }

    private static GradientDrawable box(int fill, float radius, int w, int h) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(radius);
        g.setSize(w, h);
        return g;
    }

    static void teal(Switch s) {
        float d = s.getResources().getDisplayMetrics().density;
        StateListDrawable thumb = new StateListDrawable();
        thumb.addState(new int[] {android.R.attr.state_checked}, box(ACCENT, 4 * d, (int) (52 * d), (int) (34 * d)));
        thumb.addState(new int[] {}, box(night ? 0xFF6B747A : 0xFFA9B4B9, 4 * d, (int) (52 * d), (int) (34 * d)));
        s.setThumbDrawable(thumb);
        s.setTrackDrawable(box(night ? 0xFF2E3135 : 0xFFD9DEE1, 4 * d, (int) (104 * d), (int) (34 * d)));
        s.setTextOn("On");
        s.setTextOff("Off");
    }

    static void teal(RadioButton r) {
        r.setButtonDrawable(new Dot(r.getResources().getDisplayMetrics().density));
    }

    /** A radio button: a ring, filled with a dot when chosen. */
    private static final class Dot extends Drawable {
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG), dot = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float d;
        private boolean checked;

        Dot(float density) {
            d = density;
            ring.setStyle(Paint.Style.STROKE);
            ring.setStrokeWidth(2.5f * d);
            dot.setColor(ACCENT);
        }

        @Override public boolean isStateful() { return true; }

        @Override protected boolean onStateChange(int[] state) {
            boolean c = false;
            for (int s : state) if (s == android.R.attr.state_checked) c = true;
            if (c == checked) return false;
            checked = c;
            invalidateSelf();
            return true;
        }

        @Override public int getIntrinsicWidth() { return (int) (36 * d); }
        @Override public int getIntrinsicHeight() { return (int) (36 * d); }

        @Override public void draw(Canvas c) {
            float cx = getBounds().exactCenterX(), cy = getBounds().exactCenterY();
            ring.setColor(checked ? ACCENT : OUTLINE);
            c.drawCircle(cx, cy, 11 * d, ring);
            if (checked) c.drawCircle(cx, cy, 6 * d, dot);
        }

        @Override public void setAlpha(int a) { }
        @Override public void setColorFilter(ColorFilter f) { }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    private Widgets() { }
}
