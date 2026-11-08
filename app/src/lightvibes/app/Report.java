package lightvibes.app;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Turns the lamp test's result into words for the owner and a link for a GitHub issue. No Android classes:
 * test/ReportTest.java runs it on the Mac. Only what's named here leaves the car, and only when the owner scans the code.
 */
final class Report {
    private static final String ISSUE = "https://github.com/dibble9012/light-vibes/issues/new?template=problem.yml";

    final String heading, detail;

    private final Map<String, String> r;
    private final String seenColour, seenBright, kind;

    /** @param seenColour "yes", "no" or null if the owner wasn't asked */
    Report(Map<String, String> result, String seenColour, String seenBright) {
        r = result;
        this.seenColour = seenColour;
        this.seenBright = seenBright;
        String[] v = verdict();
        kind = v[0];
        heading = v[1];
        detail = v[2];
    }

    static Map<String, String> parse(String text) {
        Map<String, String> m = new LinkedHashMap<String, String>();
        for (String line : text.split("\n")) {
            int eq = line.indexOf('=');
            if (eq > 0) m.put(line.substring(0, eq), line.substring(eq + 1).trim());
        }
        return m;
    }

    private String get(String k) {
        String v = r.get(k);
        return v == null ? "?" : v;
    }

    private boolean all(String k) {
        String[] p = get(k).split("/");
        return p.length == 2 && p[0].equals(p[1]);
    }

    private boolean none(String k) {
        return get(k).startsWith("0/");
    }

    private String[] verdict() {
        if (!r.containsKey("stage")) {
            return new String[] {"no service", "The Light Vibes service didn't answer",
                    "Nothing was tested. Check that Light Vibes is on in HMG-API, restart the head unit, and try again."};
        }
        String restored = "1".equals(r.get("restored")) ? "" : "\n\nThe lamp didn't confirm your colour came back. If it looks wrong, set it again in Settings > Vehicle > Lights > Ambient light.";
        if ("missing".equals(r.get("svc"))) {
            return new String[] {"no lamp service", "This head unit has no lamp service",
                    "The service Light Vibes uses to control the light doesn't exist on this head unit, so it needs adding for your car." + restored};
        }
        boolean c = "yes".equals(seenColour), b = "yes".equals(seenBright);
        if (c && b) {
            return new String[] {"lamp ok", "The lights work with Light Vibes",
                    audioNote() + restored};
        }
        StringBuilder d = new StringBuilder();
        String what = !c && !b ? "colour and brightness" : !c ? "colour" : "brightness";
        if ("0".equals(r.get("reply"))) {
            d.append("The lamp never answered, so Light Vibes can't tell what it did. This car may control its light another way.");
        } else if (!c && all("c") || !b && all("b")) {
            d.append("The car reported the change, but you didn't see it. The light itself may be set differently, or this car may use another control.");
        } else {
            if (!c && none("c")) d.append("The lamp ignored the test colours. This car may use different factory colours.");
            if (!b && !all("b")) d.append(d.length() > 0 ? " " : "").append("The lamp didn't take the test brightness levels.");
            if (d.length() == 0) d.append("The lamp took only some of the test values.");
        }
        return new String[] {what + " not changing", "The " + what + " didn't change", d + restored};
    }

    private String audioNote() {
        String peak = get("peak");
        if (peak.equals("none") || peak.equals("?")) {
            return "The light responds, and no music has reached Light Vibes since the head unit started. Play music, then run this again if the lights don't follow it.";
        }
        return "The light responds. The loudest music Light Vibes has heard is " + peak + " dB, and it treats anything under " + get("silence")
                + " dB as silence. If the lights still don't follow the music, send this report.";
    }

    /** What goes in the issue: short, because it has to fit a QR code a phone can read off the screen. */
    String summary() {
        StringBuilder s = new StringBuilder();
        s.append("LV ").append(get("v")).append(" HMG ").append(get("hmg")).append('\n');
        s.append(get("product")).append('\n');
        s.append("saw colour ").append(seenColour == null ? "?" : seenColour).append(" brightness ").append(seenBright == null ? "?" : seenBright).append('\n');
        s.append("lamp svc ").append(get("svc")).append(" reply ").append(get("reply")).append(" c ").append(get("c")).append(" b ").append(get("b"))
                .append(" restored ").append(get("restored")).append('\n');
        s.append("user ").append(get("user")).append(" dim ").append(get("dim")).append(" dm ").append(get("dm")).append(" usm ").append(get("usm")).append('\n');
        s.append("mode ").append(get("mode")).append(" on ").append(get("enabled")).append(" range ").append(get("range")).append('\n');
        s.append("audio loop ").append(get("loop")).append(" rate ").append(get("rate")).append(" songs ").append(get("songs")).append(" peak ").append(get("peak"))
                .append(" sent ").append(get("sent")).append(" chk ").append(get("chk")).append('\n');
        s.append("mods ").append(get("mods"));
        return s.toString();
    }

    String title() {
        String product = get("product");
        int dot = product.indexOf('.');
        return kind + (dot > 0 ? " on " + product.substring(0, dot) : "");
    }

    String issueUrl() {
        String sum = summary();
        String url = link(sum);
        if (url.length() > 700) {       // many other mods: their names are in the full report anyway
            url = link(sum.substring(0, sum.lastIndexOf('\n')));
        }
        return url;
    }

    private String link(String sum) {
        try {
            return ISSUE + "&title=" + URLEncoder.encode(title(), "UTF-8") + "&summary=" + URLEncoder.encode(sum, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            return ISSUE;
        }
    }
}
