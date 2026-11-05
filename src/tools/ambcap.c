/* ambcap: open an ALSA capture PCM and print signal levels every 100 ms.
   usage: ambcap <device> <seconds> [rate] [channels] [card]           */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <tinyalsa/asoundlib.h>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: ambcap <device> <seconds> [rate] [channels] [card]\n"); return 2; }
    unsigned dev = atoi(argv[1]), secs = atoi(argv[2]);
    unsigned rate = argc > 3 ? atoi(argv[3]) : 48000;
    unsigned ch = argc > 4 ? atoi(argv[4]) : 2;
    unsigned card = argc > 5 ? atoi(argv[5]) : 0;
    struct pcm_config cfg = {0};
    cfg.channels = ch; cfg.rate = rate; cfg.period_size = 1024; cfg.period_count = 4;
    cfg.format = PCM_FORMAT_S16_LE;
    struct pcm *pcm = pcm_open(card, dev, PCM_IN, &cfg);
    if (!pcm || !pcm_is_ready(pcm)) {
        printf("AMBCAP open card%u dev%u rate%u ch%u FAILED: %s\n", card, dev, rate, ch, pcm ? pcm_get_error(pcm) : "null");
        return 1;
    }
    printf("AMBCAP open card%u dev%u rate%u ch%u OK\n", card, dev, rate, ch);
    unsigned win = rate / 10;                 /* 100 ms */
    short *buf = malloc(win * ch * sizeof(short));
    double lp = 0, a = 1.0 - exp(-2.0 * M_PI * 150.0 / rate); /* ~150 Hz low-pass for "bass" */
    for (unsigned w = 0; w < secs * 10; w++) {
        if (pcm_readi(pcm, buf, win) < 0) { printf("AMBCAP read error: %s\n", pcm_get_error(pcm)); break; }
        double s[8] = {0}, bass = 0; int peak = 0, nonzero = 0;
        for (unsigned i = 0; i < win; i++) {
            double mono = 0;
            for (unsigned c = 0; c < ch && c < 8; c++) {
                int v = buf[i * ch + c];
                s[c] += (double)v * v; mono += v;
                if (abs(v) > peak) peak = abs(v);
                if (v) nonzero++;
            }
            mono /= ch; lp += a * (mono - lp); bass += lp * lp;
        }
        printf("AMBCAP t=%04u", w * 100);
        for (unsigned c = 0; c < ch && c < 8; c++) {
            double r = sqrt(s[c] / win);
            printf(" ch%u=%6.1fdB", c, r > 0 ? 20 * log10(r / 32768.0) : -120.0);
        }
        double br = sqrt(bass / win);
        printf(" bass=%6.1fdB peak=%5d nz=%u\n", br > 0 ? 20 * log10(br / 32768.0) : -120.0, peak, nonzero);
        fflush(stdout);
    }
    pcm_close(pcm);
    return 0;
}
