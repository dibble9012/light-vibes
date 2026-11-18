package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Material Design 3 custom controls & vector icons built natively for Android API 19 without external library bloat:
 * - M3Switch: 52x32dp stadium pill switch with expanding thumb and vector checkmark
 * - Segmented: M3 single-select segmented button group with stadium outer corners and leading checkmark
 * - Icon: crisp anti-aliased vector drawables (plus, overflow 3-dots, chevron, check, back)
 */
final class Widgets {

    interface Pick {
        void onPick(String value);
    }

    interface Toggle {
        void onToggle(boolean checked);
    }

    static GradientDrawable shape(int fill, int stroke, float strokePx, float radiusPx) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(radiusPx);
        if (strokePx > 0) g.setStroke(Math.max(1, Math.round(strokePx)), stroke);
        return g;
    }

    static GradientDrawable segmentShape(int fill, int stroke, float strokePx, float rLeft, float rRight) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadii(new float[] {rLeft, rLeft, rRight, rRight, rRight, rRight, rLeft, rLeft});
        if (strokePx > 0) g.setStroke(Math.max(1, Math.round(strokePx)), stroke);
        return g;
    }

    /** Material 3 52x32dp stadium switch with 16dp -> 24dp thumb and checkmark. */
    static final class M3Switch extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();
        private final Path check = new Path();
        private final float d;
        private boolean checked;
        private Toggle listener;

        M3Switch(Context c, boolean initial, Toggle onToggle) {
            super(c);
            d = c.getResources().getDisplayMetrics().density;
            checked = initial;
            listener = onToggle;
            setClickable(true);
            setOnClickListener(new OnClickListener() {
                @Override public void onClick(View v) {
                    if (!isEnabled()) return;
                    checked = !checked;
                    invalidate();
                    if (listener != null) listener.onToggle(checked);
                }
            });
        }

        void setChecked(boolean c) {
            if (checked == c) return;
            checked = c;
            invalidate();
        }

        boolean isChecked() { return checked; }

        @Override public void setEnabled(boolean enabled) {
            super.setEnabled(enabled);
            setAlpha(enabled ? 1f : 0.38f);
            invalidate();
        }

        @Override protected void onMeasure(int ws, int hs) {
            setMeasuredDimension(Math.round(52 * d), Math.round(32 * d));
        }

        @Override protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight(), r = h / 2f;
            float inset = 1.5f * d;
            rect.set(inset, inset, w - inset, h - inset);
            if (checked) {
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(ACCENT);
                c.drawRoundRect(rect, r, r, paint);

                float cx = w - h / 2f, cy = h / 2f;
                paint.setColor(ON_PRIMARY);
                c.drawCircle(cx, cy, 12 * d, paint);

                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(2.0f * d);
                paint.setStrokeCap(Paint.Cap.ROUND);
                paint.setStrokeJoin(Paint.Join.ROUND);
                paint.setColor(ACCENT);
                check.reset();
                check.moveTo(cx - 4.5f * d, cy + 0.2f * d);
                check.lineTo(cx - 1.2f * d, cy + 3.5f * d);
                check.lineTo(cx + 4.8f * d, cy - 3.2f * d);
                c.drawPath(check, paint);
            } else {
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(TRACK);
                c.drawRoundRect(rect, r, r, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(1.8f * d);
                paint.setColor(OUTLINE);
                c.drawRoundRect(rect, r, r, paint);

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(OUTLINE);
                c.drawCircle(h / 2f, h / 2f, 8 * d, paint);
            }
        }
    }

    /** Material 3 Single-Select Segmented Button bar. */
    static LinearLayout segmented(final Context c, final String[][] options, final String current, float heightDp, final Pick listener) {
        final float d = c.getResources().getDisplayMetrics().density;
        final LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setBaselineAligned(false);
        final String[] state = {current};
        final int n = options.length;
        final float pillR = (heightDp / 2f) * d;

        for (int i = 0; i < n; i++) {
            final int idx = i;
            final String val = options[i][0];
            final LinearLayout seg = new LinearLayout(c);
            seg.setGravity(Gravity.CENTER);
            int padH = Math.round((n >= 3 ? 12 : 16) * d);
            seg.setPadding(padH, 0, padH, 0);

            final View checkIcon = new View(c);
            checkIcon.setBackgroundDrawable(new Icon(d, Icon.CHECK, ON_TEXT));
            LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(Math.round(16 * d), Math.round(16 * d));
            ip.rightMargin = Math.round(5 * d);
            seg.addView(checkIcon, ip);

            final TextView lbl = new TextView(c);
            lbl.setText(options[i][1]);
            lbl.setTextSize(14.5f);
            lbl.setSingleLine(true);
            seg.addView(lbl);

            styleSegment(seg, checkIcon, lbl, val.equals(current), idx == 0 ? pillR : 0f, idx == n - 1 ? pillR : 0f, d);

            seg.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (val.equals(state[0])) return;
                    state[0] = val;
                    for (int k = 0; k < n; k++) {
                        LinearLayout child = (LinearLayout) bar.getChildAt(k);
                        boolean sel = options[k][0].equals(val);
                        styleSegment(child, child.getChildAt(0), (TextView) child.getChildAt(1), sel,
                                k == 0 ? pillR : 0f, k == n - 1 ? pillR : 0f, d);
                    }
                    if (listener != null) listener.onPick(val);
                }
            });

            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, Math.round(heightDp * d));
            if (i > 0) lp.leftMargin = -Math.max(1, Math.round(1 * d));   // shared 1dp border
            bar.addView(seg, lp);
        }
        return bar;
    }

    /** Full-width equally-weighted M3 Segmented Button bar (great inside cards). */
    static LinearLayout segmentedFull(final Context c, final String[][] options, final String current, float heightDp, final Pick listener) {
        LinearLayout bar = segmented(c, options, current, heightDp, listener);
        for (int i = 0; i < bar.getChildCount(); i++) {
            LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) bar.getChildAt(i).getLayoutParams();
            lp.width = 0;
            lp.weight = 1f;
        }
        return bar;
    }

    private static void styleSegment(LinearLayout seg, View check, TextView lbl, boolean sel, float rLeft, float rRight, float d) {
        seg.setBackgroundDrawable(segmentShape(sel ? CARD_ON : 0x00000000, OUTLINE, 1f * d, rLeft, rRight));
        check.setVisibility(sel ? View.VISIBLE : View.GONE);
        lbl.setTextColor(sel ? ON_TEXT : TEXT);
        lbl.setTypeface(sel ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
    }

    /** Crisp vector icons drawn directly on Canvas so Android 4.4 never depends on missing font glyphs. */
    static final class Icon extends Drawable {
        static final int PLUS = 1, MORE = 2, CHEVRON_DOWN = 3, CHECK = 4, BACK = 5;

        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final float d;
        private final int kind, color;

        Icon(float density, int kind, int color) {
            this.d = density;
            this.kind = kind;
            this.color = color;
        }

        @Override public void draw(Canvas c) {
            float w = getBounds().width(), h = getBounds().height();
            float cx = getBounds().exactCenterX(), cy = getBounds().exactCenterY();
            p.setColor(color);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);

            switch (kind) {
                case PLUS:
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(2.1f * d);
                    float arm = Math.min(w, h) * 0.26f;
                    c.drawLine(cx - arm, cy, cx + arm, cy, p);
                    c.drawLine(cx, cy - arm, cx, cy + arm, p);
                    break;
                case MORE:
                    p.setStyle(Paint.Style.FILL);
                    float r = 1.9f * d, gap = 5.2f * d;
                    c.drawCircle(cx, cy - gap, r, p);
                    c.drawCircle(cx, cy, r, p);
                    c.drawCircle(cx, cy + gap, r, p);
                    break;
                case CHEVRON_DOWN:
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(2.0f * d);
                    float cw = 4.5f * d, ch = 2.6f * d;
                    path.reset();
                    path.moveTo(cx - cw, cy - ch * 0.5f);
                    path.lineTo(cx, cy + ch);
                    path.lineTo(cx + cw, cy - ch * 0.5f);
                    c.drawPath(path, p);
                    break;
                case CHECK:
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(2.1f * d);
                    path.reset();
                    path.moveTo(cx - 5.0f * d, cy + 0.3f * d);
                    path.lineTo(cx - 1.4f * d, cy + 3.8f * d);
                    path.lineTo(cx + 5.2f * d, cy - 3.6f * d);
                    c.drawPath(path, p);
                    break;
                case BACK:
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(2.1f * d);
                    float aw = 5.5f * d;
                    c.drawLine(cx - aw, cy, cx + aw, cy, p);
                    path.reset();
                    path.moveTo(cx - 1.0f * d, cy - 4.8f * d);
                    path.lineTo(cx - aw, cy);
                    path.lineTo(cx - 1.0f * d, cy + 4.8f * d);
                    c.drawPath(path, p);
                    break;
            }
        }

        @Override public void setAlpha(int a) { p.setAlpha(a); }
        @Override public void setColorFilter(ColorFilter cf) { p.setColorFilter(cf); }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    private Widgets() { }
}
