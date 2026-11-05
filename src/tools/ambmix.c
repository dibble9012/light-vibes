/* ambmix: dump ALSA mixer controls.  usage: ambmix [card]   |   ambmix <card> set <id> <value>   |   ambmix <card> get <id> */
#include <stdio.h>
#include <stdlib.h>
#include <tinyalsa/asoundlib.h>
int main(int argc, char **argv) {
    unsigned card = argc > 1 ? atoi(argv[1]) : 0;
    struct mixer *m = mixer_open(card);
    if (!m) { printf("AMBMIX card%u open FAILED\n", card); return 1; }
    if (argc > 3) {
        unsigned id = atoi(argv[3]);
        struct mixer_ctl *c = mixer_get_ctl(m, id);
        if (!c) { printf("AMBMIX no control %u\n", id); mixer_close(m); return 1; }
        if (argv[2][0] == 's' && argc > 4) {
            int r = mixer_ctl_set_value(c, 0, atoi(argv[4]));
            printf("AMBMIX set %u (%s) = %d -> %s\n", id, mixer_ctl_get_name(c), atoi(argv[4]), r ? "FAILED" : "ok");
        }
        printf("AMBMIX get %u (%s) = %d\n", id, mixer_ctl_get_name(c), mixer_ctl_get_value(c, 0));
        mixer_close(m);
        return 0;
    }
    unsigned n = mixer_get_num_ctls(m);
    printf("AMBMIX card%u name=%s controls=%u\n", card, mixer_get_name(m), n);
    for (unsigned i = 0; i < n; i++) {
        struct mixer_ctl *c = mixer_get_ctl(m, i);
        enum mixer_ctl_type t = mixer_ctl_get_type(c);
        unsigned nv = mixer_ctl_get_num_values(c);
        printf("%u\t%s\t%s\t", i, mixer_ctl_get_type_string(c), mixer_ctl_get_name(c));
        if (t == MIXER_CTL_TYPE_ENUM) {
            printf("cur=%s opts=", mixer_ctl_get_enum_string(c, mixer_ctl_get_value(c, 0)));
            for (unsigned e = 0; e < mixer_ctl_get_num_enums(c); e++) printf("%s%s", e ? "|" : "", mixer_ctl_get_enum_string(c, e));
        } else if (t == MIXER_CTL_TYPE_BOOL || t == MIXER_CTL_TYPE_INT) {
            for (unsigned v = 0; v < nv && v < 16; v++) printf("%s%d", v ? "," : "", mixer_ctl_get_value(c, v));
            if (t == MIXER_CTL_TYPE_INT) printf(" range=%d..%d", mixer_ctl_get_range_min(c), mixer_ctl_get_range_max(c));
        } else printf("(%u values)", nv);
        printf("\n");
    }
    mixer_close(m);
    return 0;
}
