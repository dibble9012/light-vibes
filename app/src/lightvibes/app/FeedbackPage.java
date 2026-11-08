package lightvibes.app;

import static lightvibes.app.Ui.*;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;

/**
 * Feedback: ideas and requests, as a GitHub issue labelled "enhancement" (the feedback.yml form). The code opens the
 * form on the owner's phone with the app version, head unit and current mode filled in; nothing is sent from the car.
 */
final class FeedbackPage extends LinearLayout {
    private static final String ISSUE = "https://github.com/dibble9012/light-vibes/issues/new?template=feedback.yml&labels=enhancement";   // the form adds the label too

    FeedbackPage(Context c, String mode, final Runnable close) {
        super(c);
        setOrientation(HORIZONTAL);
        LinearLayout left = new LinearLayout(c);
        left.setOrientation(VERTICAL);
        TextView h = text("Send feedback", 22, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        h.setPadding(0, dp(4), 0, dp(8));
        left.addView(h);
        left.addView(text("Have an idea or something you'd like changed? Scan the code with your phone to open a suggestion "
                + "on GitHub, describe it, and submit it.", 17, TEXT));
        TextView note = text("Nothing is sent from the car. The code only adds the app version, head unit and current mode.", 15, MUTED);
        note.setPadding(0, dp(12), 0, 0);
        left.addView(note);
        TextView done = text("Close", 18, TEXT);
        done.setGravity(Gravity.CENTER);
        GradientDrawable g = new GradientDrawable();
        g.setColor(CARD);
        g.setCornerRadius(dp(8));
        done.setBackgroundDrawable(g);
        done.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View v) { close.run(); }
        });
        LayoutParams dp = new LayoutParams(dp(180), dp(56));
        dp.topMargin = dp(20);
        left.addView(done, dp);
        addView(left, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        LayoutParams qp = new LayoutParams(dp(390), dp(390));
        qp.leftMargin = dp(16);
        addView(new QrView(c, link(c, mode)), qp);
    }

    static String link(Context c, String mode) {
        String version = "?";
        try { version = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName; } catch (Exception ignored) { }
        String about = "LV " + version + "\n" + prop("ro.build.product") + "\nmode " + mode;
        try {
            return ISSUE + "&about=" + URLEncoder.encode(about, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            return ISSUE;
        }
    }

    private static String prop(String key) {
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            String v = (String) sp.getMethod("get", String.class).invoke(null, key);
            return v == null || v.isEmpty() ? "?" : v;
        } catch (Exception e) {
            return "?";
        }
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
}
