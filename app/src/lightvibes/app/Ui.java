package lightvibes.app;

import android.content.Context;
import android.content.res.Configuration;

/**
 * Material Design 3 tonal color tokens (Automotive Teal/Cyan seed), adapting automatically to the head unit's
 * Day (light) and Night (dark) themes via UiModeManager.
 */
final class Ui {
    static boolean night;

    // Surface hierarchy (M3 surface -> surfaceContainerLowest..Highest)
    static int BG;              // surface
    static int SURFACE_LOW;     // surfaceContainerLow (navigation rail / preset pane)
    static int CARD;            // surfaceContainer (standard cards & parameter tiles)
    static int SURFACE_HIGH;    // surfaceContainerHigh (elevated chips, menus, dialogs)
    static int TRACK;           // surfaceContainerHighest (inactive slider tracks, subtle fills)

    // Primary & Secondary tonal roles
    static int ACCENT;          // primary
    static int ON_PRIMARY;      // onPrimary
    static int PRIMARY_CONT;    // primaryContainer
    static int ON_PRIMARY_CONT; // onPrimaryContainer
    static int CARD_ON;         // secondaryContainer (selected nav pills, segmented buttons, cards)
    static int ON_TEXT;         // onSecondaryContainer

    // Error / Destructive tonal roles
    static int ERR_CONT;        // errorContainer
    static int ON_ERR_CONT;     // onErrorContainer

    // Content & Borders
    static int TEXT;            // onSurface
    static int MUTED;           // onSurfaceVariant
    static int OUTLINE;         // outline
    static int RULE;            // outlineVariant (subtle 1dp dividers & card hairlines)

    static void apply(Context c) {
        night = (c.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        if (night) {
            BG = 0xFF0E1415;
            SURFACE_LOW = 0xFF161D1E;
            CARD = 0xFF1B2224;
            SURFACE_HIGH = 0xFF252C2E;
            TRACK = 0xFF303739;

            ACCENT = 0xFF4FDAD0;
            ON_PRIMARY = 0xFF003733;
            PRIMARY_CONT = 0xFF00504A;
            ON_PRIMARY_CONT = 0xFF9CF2E9;
            CARD_ON = 0xFF213A38;
            ON_TEXT = 0xFFCEEAE7;

            ERR_CONT = 0xFF5C1D1D;
            ON_ERR_CONT = 0xFFFFDAD6;

            TEXT = 0xFFDEE4E4;
            MUTED = 0xFF9AA6A6;
            OUTLINE = 0xFF6E7979;
            RULE = 0xFF2E3738;
        } else {
            BG = 0xFFF4FAFB;
            SURFACE_LOW = 0xFFECF3F4;
            CARD = 0xFFE5ECEE;
            SURFACE_HIGH = 0xFFDCE4E6;
            TRACK = 0xFFD2DCDE;

            ACCENT = 0xFF006A63;
            ON_PRIMARY = 0xFFFFFFFF;
            PRIMARY_CONT = 0xFF9CF2E9;
            ON_PRIMARY_CONT = 0xFF00201D;
            CARD_ON = 0xFFC7E7E3;
            ON_TEXT = 0xFF05201E;

            ERR_CONT = 0xFFFFDAD6;
            ON_ERR_CONT = 0xFF410002;

            TEXT = 0xFF161D1E;
            MUTED = 0xFF445050;
            OUTLINE = 0xFF6F7979;
            RULE = 0xFFC2CCCC;
        }
    }

    private Ui() { }
}
