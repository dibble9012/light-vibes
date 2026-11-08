package lightvibes.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;

/** A QR code, black on white with its quiet zone, drawn in whole pixels so a phone can read it off the screen. */
final class QrView extends View {
    private final QrCode code;
    private final Paint paint = new Paint();

    QrView(Context c, String text) {
        super(c);
        QrCode q = null;
        try {
            q = new QrCode(text.getBytes("UTF-8"));
        } catch (Exception e) {
            // can't happen for a link this short; the screen still shows the words
        }
        code = q;
        paint.setColor(Color.BLACK);
    }

    @Override protected void onDraw(Canvas canvas) {
        canvas.drawColor(Color.WHITE);
        if (code == null) return;
        int side = Math.min(getWidth(), getHeight());
        int cell = side / (code.size + 8);
        if (cell < 1) return;
        int left = (getWidth() - cell * code.size) / 2, top = (getHeight() - cell * code.size) / 2;
        for (int y = 0; y < code.size; y++) {
            for (int x = 0; x < code.size; x++) {
                if (code.get(x, y)) canvas.drawRect(left + x * cell, top + y * cell, left + (x + 1) * cell, top + (y + 1) * cell, paint);
            }
        }
    }
}
