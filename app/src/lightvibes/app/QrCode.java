package lightvibes.app;

import java.util.Arrays;

/**
 * A QR code for a short piece of text: byte mode, error correction level L, versions 1 to 25.
 * That's all the report link needs, so the app carries no library. No Android classes: test/ReportTest.java runs it on the Mac.
 */
final class QrCode {
    private static final int MAX_VERSION = 25;
    /* QR Code specification, table 9, level L */
    private static final int[] ECC_PER_BLOCK = {0, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26};
    private static final int[] BLOCKS = {0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12};

    final int size;
    private final boolean[][] dark;
    private final boolean[][] fixed;

    boolean get(int x, int y) {
        return dark[y][x];
    }

    /** @throws IllegalArgumentException if the text doesn't fit version 25 */
    QrCode(byte[] text) {
        int version = 1;
        while (version <= MAX_VERSION && capacity(version) < headerBits(version, text.length)) version++;
        if (version > MAX_VERSION) throw new IllegalArgumentException("too long for a QR code: " + text.length);
        size = version * 4 + 17;
        dark = new boolean[size][size];
        fixed = new boolean[size][size];
        drawFixed(version);
        placeData(withErrorCorrection(version, encode(version, text)));
        int mask = bestMask(version);
        applyMask(mask);
        drawFormat(mask);
    }

    private static int countBits(int version) {
        return version < 10 ? 8 : 16;
    }

    private static int headerBits(int version, int bytes) {
        return 4 + countBits(version) + bytes * 8;
    }

    private static int rawModules(int v) {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) {
            int align = v / 7 + 2;
            r -= (25 * align - 10) * align - 55;
            if (v >= 7) r -= 36;
        }
        return r;
    }

    /** Data bits a version holds at level L. */
    private static int capacity(int v) {
        return (rawModules(v) / 8 - ECC_PER_BLOCK[v] * BLOCKS[v]) * 8;
    }

    private static byte[] encode(int version, byte[] text) {
        int total = capacity(version) / 8;
        byte[] out = new byte[total];
        int[] bit = {0};
        put(out, bit, 4, 4);       // byte mode
        put(out, bit, text.length, countBits(version));
        for (byte b : text) put(out, bit, b & 0xFF, 8);
        put(out, bit, 0, Math.min(4, total * 8 - bit[0]));   // terminator
        bit[0] = (bit[0] + 7) / 8 * 8;
        for (int pad = 0xEC; bit[0] < total * 8; pad ^= 0xEC ^ 0x11) put(out, bit, pad, 8);
        return out;
    }

    private static void put(byte[] out, int[] bit, int value, int count) {
        for (int i = count - 1; i >= 0; i--, bit[0]++) {
            if (((value >> i) & 1) != 0) out[bit[0] >> 3] |= 1 << (7 - (bit[0] & 7));
        }
    }

    // ---------- error correction: Reed-Solomon over GF(256), polynomial 0x11D ----------

    private static int gfMul(int a, int b) {
        int r = 0;
        for (int i = 7; i >= 0; i--) {
            r = (r << 1) ^ ((r >>> 7) * 0x11D);
            r ^= ((b >>> i) & 1) * a;
        }
        return r;
    }

    private static int[] divisor(int degree) {
        int[] d = new int[degree];
        d[degree - 1] = 1;
        int root = 1;
        for (int i = 0; i < degree; i++) {
            for (int j = 0; j < degree; j++) {
                d[j] = gfMul(d[j], root);
                if (j + 1 < degree) d[j] ^= d[j + 1];
            }
            root = gfMul(root, 2);
        }
        return d;
    }

    private static byte[] withErrorCorrection(int version, byte[] data) {
        int blocks = BLOCKS[version], ecc = ECC_PER_BLOCK[version], raw = rawModules(version) / 8;
        int shortBlocks = blocks - raw % blocks, shortLen = raw / blocks;
        int[] div = divisor(ecc);
        byte[][] parts = new byte[blocks][];
        for (int i = 0, k = 0; i < blocks; i++) {
            int len = shortLen - ecc + (i < shortBlocks ? 0 : 1);
            int[] rem = new int[ecc];
            parts[i] = new byte[shortLen + 1];
            for (int j = 0; j < len; j++, k++) {
                int b = data[k] & 0xFF;
                parts[i][j] = data[k];
                int factor = b ^ rem[0];
                System.arraycopy(rem, 1, rem, 0, ecc - 1);
                rem[ecc - 1] = 0;
                for (int m = 0; m < ecc; m++) rem[m] ^= gfMul(div[m], factor);
            }
            for (int m = 0; m < ecc; m++) parts[i][shortLen + 1 - ecc + m] = (byte) rem[m];
        }
        byte[] out = new byte[raw];
        int n = 0;
        for (int col = 0; col <= shortLen; col++) {
            for (int i = 0; i < blocks; i++) {
                if (col != shortLen - ecc || i >= shortBlocks) out[n++] = parts[i][col];   // short blocks have no byte in that column
            }
        }
        return out;
    }

    // ---------- the fixed patterns ----------

    private void set(int x, int y, boolean on) {
        dark[y][x] = on;
        fixed[y][x] = true;
    }

    private void drawFixed(int version) {
        for (int i = 0; i < size; i++) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(size - 4, 3);
        finder(3, size - 4);
        int[] centres = alignment(version);
        for (int a = 0; a < centres.length; a++) {
            for (int b = 0; b < centres.length; b++) {
                boolean corner = (a == 0 && b == 0) || (a == 0 && b == centres.length - 1) || (a == centres.length - 1 && b == 0);
                if (corner) continue;
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) set(centres[a] + dx, centres[b] + dy, Math.max(Math.abs(dx), Math.abs(dy)) != 1);
                }
            }
        }
        drawFormat(0);    // reserves the cells; the real mask is drawn at the end
        if (version >= 7) {
            int rem = version;
            for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >>> 11) * 0x1F25);
            int bits = version << 12 | rem;
            for (int i = 0; i < 18; i++) {
                boolean on = ((bits >>> i) & 1) != 0;
                int a = size - 11 + i % 3, b = i / 3;
                set(a, b, on);
                set(b, a, on);
            }
        }
    }

    private void finder(int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy, d = Math.max(Math.abs(dx), Math.abs(dy));
                if (x >= 0 && x < size && y >= 0 && y < size) set(x, y, d != 2 && d != 4);
            }
        }
    }

    private int[] alignment(int v) {
        if (v == 1) return new int[0];
        int n = v / 7 + 2, step = (v * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
        int[] r = new int[n];
        r[0] = 6;
        for (int i = n - 1, pos = size - 7; i >= 1; i--, pos -= step) r[i] = pos;
        return r;
    }

    private void drawFormat(int mask) {
        int data = 1 << 3 | mask, rem = data;       // level L is 01
        for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;
        for (int i = 0; i <= 5; i++) set(8, i, bit(bits, i));
        set(8, 7, bit(bits, 6));
        set(8, 8, bit(bits, 7));
        set(7, 8, bit(bits, 8));
        for (int i = 9; i < 15; i++) set(14 - i, 8, bit(bits, i));
        for (int i = 0; i < 8; i++) set(size - 1 - i, 8, bit(bits, i));
        for (int i = 8; i < 15; i++) set(8, size - 15 + i, bit(bits, i));
        set(8, size - 8, true);
    }

    private static boolean bit(int v, int i) {
        return ((v >>> i) & 1) != 0;
    }

    // ---------- data and mask ----------

    private void placeData(byte[] codewords) {
        int i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int v = 0; v < size; v++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    boolean up = ((right + 1) & 2) == 0;
                    int y = up ? size - 1 - v : v;
                    if (!fixed[y][x] && i < codewords.length * 8) {
                        dark[y][x] = bit(codewords[i >>> 3], 7 - (i & 7));
                        i++;
                    }
                }
            }
        }
    }

    private void applyMask(int mask) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (fixed[y][x]) continue;
                boolean flip;
                switch (mask) {
                    case 0: flip = (x + y) % 2 == 0; break;
                    case 1: flip = y % 2 == 0; break;
                    case 2: flip = x % 3 == 0; break;
                    case 3: flip = (x + y) % 3 == 0; break;
                    case 4: flip = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: flip = x * y % 2 + x * y % 3 == 0; break;
                    case 6: flip = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    default: flip = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                dark[y][x] ^= flip;
            }
        }
    }

    private int bestMask(int version) {
        int best = 0, lowest = Integer.MAX_VALUE;
        for (int m = 0; m < 8; m++) {
            applyMask(m);
            drawFormat(m);
            int p = penalty();
            if (p < lowest) {
                lowest = p;
                best = m;
            }
            applyMask(m);    // undoes it
        }
        return best;
    }

    private int penalty() {
        int score = 0, darkCount = 0;
        for (int pass = 0; pass < 2; pass++) {
            for (int a = 0; a < size; a++) {
                int run = 0;
                boolean colour = false;
                int history = 0;
                for (int b = 0; b < size; b++) {
                    boolean c = pass == 0 ? dark[a][b] : dark[b][a];
                    if (b > 0 && c == colour) {
                        run++;
                        if (run == 5) score += 3;
                        else if (run > 5) score++;
                    } else {
                        colour = c;
                        run = 1;
                    }
                    history = (history << 1 | (c ? 1 : 0)) & 0x7FF;
                    if (b >= 10 && (history == 0x05D || history == 0x5D0)) score += 40;
                }
            }
        }
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (dark[y][x]) darkCount++;
                if (x + 1 < size && y + 1 < size && dark[y][x] == dark[y][x + 1] && dark[y][x] == dark[y + 1][x] && dark[y][x] == dark[y + 1][x + 1]) score += 3;
            }
        }
        int total = size * size, k = (Math.abs(darkCount * 20 - total * 10) + total - 1) / total - 1;
        return score + Math.max(k, 0) * 10;
    }
}
