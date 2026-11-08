package lightvibes.app;

import java.io.PrintWriter;
import java.util.Map;

/** ./build.sh test runs this. With a file name, also writes the code as text for a QR reader to check. */
public class ReportTest {
    static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    static final String BASE = "stage=done\nv=0.6.0\nproduct=SG2EV.USA.0000.V015.012.260507\nname=x\nusm=0\nhmg=1.0.2\nmods=auto-dst,auto-heat-and-cool-seats,nemesis-shortcut,wireless-carplay-aa\n"
            + "mode=spectrum\nenabled=1\nrange=3-10\nloop=on\nrate=0\nsongs=4\npeak=-14\nsilence=-55\nsent=120/88\nchk=3/0/0/0\nsvc=00000000\nreply=1\nuser=255,68,35,2,1\ndim=1\nc=3/3\ndm=1\nb=3/3\nrestored=1\n";

    public static void main(String[] a) throws Exception {
        Map<String, String> m = Report.parse(BASE);
        Report ok = new Report(m, "yes", "yes");
        check(ok.heading.contains("work"), ok.heading);
        check(ok.detail.contains("-14"), ok.detail);
        check(new Report(Report.parse(BASE.replace("peak=-14", "peak=none")), "yes", "yes").detail.contains("no music"), "no music");

        Report seenNot = new Report(m, "no", "yes");
        check(seenNot.heading.contains("colour") && seenNot.detail.contains("didn't see"), seenNot.detail);
        Report ignored = new Report(Report.parse(BASE.replace("c=3/3", "c=0/3")), "no", "yes");
        check(ignored.detail.contains("factory colours"), ignored.detail);
        Report dim = new Report(Report.parse(BASE.replace("b=3/3", "b=1/3")), "yes", "no");
        check(dim.heading.contains("brightness") && !dim.heading.contains("colour"), dim.heading);
        check(dim.detail.contains("brightness levels"), dim.detail);
        Report silent = new Report(Report.parse(BASE.replace("reply=1", "reply=0").replace("restored=1", "restored=0")), "no", "no");
        check(silent.detail.contains("never answered") && silent.detail.contains("set it again"), silent.detail);
        check(silent.heading.contains("colour and brightness"), silent.heading);
        Report missing = new Report(Report.parse(BASE.replace("svc=00000000", "svc=missing")), "no", "no");
        check(missing.heading.contains("no lamp service"), missing.heading);
        Report dead = new Report(Report.parse(""), null, null);
        check(dead.heading.contains("didn't answer"), dead.heading);
        check(dead.issueUrl().startsWith("https://github.com/dibble9012/light-vibes/issues/new?template=problem.yml&title="), dead.issueUrl());

        String url = seenNot.issueUrl();
        check(url.contains("title=colour+not+changing+on+SG2EV"), url);
        check(url.length() < 700, "long link: " + url.length());
        QrCode q = new QrCode(url.getBytes("UTF-8"));
        check(q.size <= 97, "code too dense: " + q.size);
        System.out.println("link " + url.length() + " characters, code " + q.size + " modules");

        boolean tooLong = false;
        try { new QrCode(new byte[2000]); } catch (IllegalArgumentException e) { tooLong = true; }
        check(tooLong, "over-long text");

        if (a.length > 0) {
            int[] lengths = {17, 134, 271, 520, 858, 1273};   // exactly full: no padding, so any encoder gives the same cells
            try (PrintWriter w = new PrintWriter(a[0], "UTF-8")) {
                emit(w, url);
                for (int n : lengths) {
                    StringBuilder s = new StringBuilder();
                    for (int i = 0; i < n; i++) s.append((char) ('a' + i * 7 % 26));
                    emit(w, s.toString());
                }
            }
        }
        System.out.println("report tests passed");
    }

    static void emit(PrintWriter w, String s) throws Exception {
        QrCode q = new QrCode(s.getBytes("UTF-8"));
        w.println(s);
        for (int y = 0; y < q.size; y++) {
            StringBuilder b = new StringBuilder();
            for (int x = 0; x < q.size; x++) b.append(q.get(x, y) ? '1' : '0');
            w.println(b);
        }
        w.println();
    }
}
