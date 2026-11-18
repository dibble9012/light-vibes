package lightvibes.app;

import java.util.Map;

/** ./build.sh test runs this to verify lamp test verdicts and summary formatting. */
public class ReportTest {
    static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    static final String BASE = "stage=done\nv=0.7.0\nproduct=NEEV.USA.0000.V015.012.260507\nname=x\nusm=0\nhmg=1.1.0\nmods=auto-dst,nemesis-shortcut,wireless-carplay-aa\n"
            + "mode=synesthesia\nenabled=1\nrange=1-10\nloop=on\nrate=0\nsongs=4\npeak=-14\nsilence=-55\nsent=120/88\nchk=3/0/0/0\nsvc=00000000\nreply=1\nuser=382,471,277,7,1\ndim=1\nc=3/3\ndm=1\nb=3/3\nrestored=1\n";

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

        check(seenNot.title().equals("colour not changing on NEEV"), seenNot.title());
        String sum = ok.summary();
        check(sum.contains("LV 0.7.0") && sum.contains("user 382,471,277,7,1"), sum);
        check(!sum.contains("github.com"), sum);
        System.out.println("report summary " + sum.length() + " characters; report tests passed");
    }
}
