// host test for the pad profile files (profile_text.c): the format there and
// back, what is refused, and every file given on the command line
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "profile_text.h"
#include "web_page.h"

// profiles.c and web_page.c are not linked: what profile_text.c needs of them
bool profiles_name_ok(char const *name)
{
    size_t length = strlen(name);
    if (!length || (length >= PROFILE_NAME_LEN) || (name[0] == ' ') || (name[length - 1] == ' ')) return false;
    for (size_t i = 0; i < length; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" -_.+", c))) return false;
    }
    return true;
}
char const *web_page_key_name(uint8_t usage, char buf[8])
{
    if (usage == 0x2c) return "space";
    if (usage == 0xe1) return "lshift";
    snprintf(buf, 8, "0x%02x", usage);
    return buf;
}
int web_page_key_usage(char const *name)
{
    unsigned x;
    if (!strcmp(name, "space")) return 0x2c;
    if (!strcmp(name, "lshift")) return 0xe1;
    if (!strcmp(name, "rshift")) return 0xe5;
    if (!strcmp(name, "down")) return 0x51;
    if (!strcmp(name, "f7")) return 0x40;
    if (sscanf(name, "0x%x", &x) == 1) return (int)x;
    return -1;
}

static bool reads(char const *text, profile_t *p, char *error)
{
    return profile_text_read(text, strlen(text), p, error, 96);
}

int main(int argc, char **argv)
{
    profile_t p, q;
    char text[PROFILE_TEXT_MAX], error[96];
    size_t n;

    // there and back, every field used
    memset(&p, 0, sizeof(p));
    strcpy(p.name, "A b-c_1.+");
    p.machine = PROFILE_FOR_C64; p.pad_directions = SETTINGS_PAD_DIR_STICK; p.pad_triangle = SETTINGS_PAD_TRIANGLE_AUTOFIRE;
    p.pad_autofire = SETTINGS_PAD_AUTOFIRE_20;
    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++)
        p.combo[i] = (settings_combo_t){ (uint8_t)(16 - i), (uint8_t)(0xff >> i), (i & 1) ? 0x2c : 0xe1 };
    n = profile_text_write(&p, text, sizeof(text));
    assert(n && n < PROFILE_TEXT_MAX - 60 && strlen(text) == n);
    assert(profile_text_read(text, n, &q, error, sizeof(error)) && memcmp(&p, &q, sizeof(p)) == 0);
    assert(profile_text_write(&p, text, 40) == 0);

    // cr lf, a byte order mark, comments, blank lines, numbers for buttons, capitals; defaults for what is left out
    assert(reads("\xef\xbb\xbfNinepin-UX pad profile\r\n# x\r\n\r\nNAME=Two\r\ncombo=6 Down FIRE1 Auto KEY=space  \r\n", &q, error));
    assert(!strcmp(q.name, "Two") && q.machine == PROFILE_FOR_ANY && q.pad_autofire == SETTINGS_PAD_AUTOFIRE_10
        && q.combo[0].button == 6 && q.combo[0].outputs == (0x02 | 0x10 | 0x80) && q.combo[0].key == 0x2c && q.combo[1].button == 0);

    // refused
    assert(!reads("", &q, error) && strstr(error, "not a pad profile"));
    assert(!reads("name=x\n", &q, error) && strstr(error, "Line 1"));
    assert(!reads("ninepin-ux pad profile\n", &q, error) && strstr(error, "no name"));
    assert(!reads("ninepin-ux pad profile\nname=way too long a name\n", &q, error) && strstr(error, "Line 2"));
    assert(!reads("ninepin-ux pad profile\nname=x\nmachine=vic20\n", &q, error) && strstr(error, "Line 3"));
    assert(!reads("ninepin-ux pad profile\nname=x\ncombo=r9 up\n", &q, error) && strstr(error, "no such button"));
    assert(!reads("ninepin-ux pad profile\nname=x\ncombo=r1 sideways\n", &q, error));
    assert(!reads("ninepin-ux pad profile\nname=x\ncombo=r1 key=nokey\n", &q, error));
    assert(!reads("ninepin-ux pad profile\nname=x\ncombo=17\n", &q, error));
    assert(!reads("ninepin-ux pad profile\nname=x\nspeed=3\n", &q, error) && strstr(error, "not understood"));
    assert(!reads("ninepin-ux pad profile\nname=x\nautofire=11\n", &q, error));
    assert(!reads("ninepin-ux pad profile\nname=x\ncombo=1\ncombo=2\ncombo=3\ncombo=4\ncombo=5\ncombo=6\ncombo=7\n", &q, error)
        && strstr(error, "Line 9"));
    {
        char longline[200]; memset(longline, 'a', sizeof(longline) - 1); longline[sizeof(longline) - 1] = 0;
        assert(!profile_text_read(longline, strlen(longline), &q, error, sizeof(error)) && strstr(error, "too long"));
    }

    // the files in the repository
    for (int i = 1; i < argc; i++) {
        static char file[4096];
        FILE *f = fopen(argv[i], "rb");
        size_t got;
        assert(f);
        got = fread(file, 1, sizeof(file), f);
        fclose(f);
        if (!profile_text_read(file, got, &q, error, sizeof(error))) {
            printf("%s: %s\n", argv[i], error);
            return 1;
        }
        printf("  %s: %s\n", argv[i], q.name);
    }
    printf("profile files: all passed\n");
    return 0;
}
