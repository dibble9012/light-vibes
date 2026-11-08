package lightvibes.app;

import android.content.Context;
import android.content.res.Configuration;

/**
 * The app's colours, in the head unit's own day (light) and night (dark) themes. The car switches with Android's
 * standard night mode (its theme settings call UiModeManager.setNightMode, and the stock apps carry -night resources),
 * so this works on any car the app runs on; Android restarts the activity when it changes.
 * Colours read from the stock apps (VehicleSettings, ThemeSettings) on the EV6: list text #62797F / #B6C1C8, selected
 * #17BEB0 / #15757E, backgrounds #F0F0F0 / #161618.
 */
final class Ui {
    static boolean night;
    static int BG, CARD, CARD_ON, ON_TEXT, TEXT, MUTED, ACCENT, RULE, TRACK, OUTLINE;

    static void apply(Context c) {
        night = (c.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        if (night) {
            BG = 0xFF161618; CARD = 0xFF232529; CARD_ON = 0xFF15757E; ON_TEXT = 0xFFE6ECEF;
            TEXT = 0xFFB6C1C8; MUTED = 0xFF7D878D; ACCENT = 0xFF17BEB0;
            RULE = 0xFF2E3135; TRACK = 0xFF3A3F47; OUTLINE = 0xFF5A616B;
        } else {
            BG = 0xFFF0F0F0; CARD = 0xFFFFFFFF; CARD_ON = 0xFF17BEB0; ON_TEXT = 0xFFFFFFFF;
            TEXT = 0xFF62797F; MUTED = 0xFF8E9DA2; ACCENT = 0xFF17BEB0;
            RULE = 0xFFD5DADD; TRACK = 0xFFC8CFD3; OUTLINE = 0xFFB9C2C7;
        }
    }

    private Ui() { }
}
