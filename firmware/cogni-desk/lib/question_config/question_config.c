#include "question_config.h"

#include <string.h>

/* ---------- JSON: extraer un texto ---------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int put(char *out, size_t cap, size_t *n, char c)
{
    if (*n + 1u >= cap) return -1;
    out[(*n)++] = c;
    return 0;
}

int qc_extract_string(const char *json, const char *key, char *out, size_t cap)
{
    if (json == NULL || key == NULL || out == NULL || cap == 0u) return -1;
    const size_t klen = strlen(key);
    const char *p = json;

    for (;;) {
        p = strchr(p, '"');
        if (p == NULL) return -1;
        const char *start = p + 1;
        /* recorre este texto completo (puede ser una clave o un valor) respetando los escapes */
        const char *e = start;
        while (*e != '\0' && *e != '"') e += (*e == '\\' && e[1] != '\0') ? 2 : 1;
        if (*e == '\0') return -1;
        const char *after = e + 1;
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') ++after;
        if ((size_t)(e - start) == klen && strncmp(start, key, klen) == 0 && *after == ':') {
            ++after;
            while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') ++after;
            if (*after != '"') return -1;
            p = after + 1;
            break;
        }
        p = e + 1;
    }

    size_t n = 0u;
    while (*p != '"') {
        if (*p == '\0') return -1;                       /* texto sin cerrar */
        if (*p != '\\') {
            if (put(out, cap, &n, *p++) != 0) return -2;
            continue;
        }
        ++p;
        char c = 0;
        switch (*p) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'b': case 'f': c = ' '; break;
        case 'u': {
            int v = 0;
            for (int i = 1; i <= 4; ++i) {
                const int h = hexval(p[i]);
                if (h < 0) return -1;
                v = v * 16 + h;
            }
            p += 4;
            if (v < 0x80) c = (char)v;
            else if (v <= 0xFF) {                         /* Latin-1 -> UTF-8 de dos bytes */
                if (put(out, cap, &n, (char)(0xC0 | (v >> 6))) != 0) return -2;
                c = (char)(0x80 | (v & 0x3F));
            } else {
                c = 0;                                    /* fuera de Latin-1: se descarta */
            }
            break;
        }
        default: return -1;
        }
        ++p;
        if (c != 0 && put(out, cap, &n, c) != 0) return -2;
    }
    out[n] = '\0';
    return (int)n;
}

/* ---------- texto del cuidador -> preguntas ---------- */

/* Pasa UTF-8 a ASCII para la OLED: a e i o u n sin tilde, descarta ¿ ¡ y lo desconocido. */
static size_t to_ascii(const char *in, char *out, size_t cap)
{
    size_t n = 0u;
    for (size_t i = 0u; in[i] != '\0' && n + 1u < cap; ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '\r') continue;
        if (c < 0x80u) { out[n++] = (char)(c == '\t' ? ' ' : c); continue; }
        if (c == 0xC3u && in[i + 1] != '\0') {
            const unsigned char d = (unsigned char)in[++i];
            char r = 0;
            switch (d) {
            case 0xA1: case 0x81: r = (d == 0xA1) ? 'a' : 'A'; break;
            case 0xA9: case 0x89: r = (d == 0xA9) ? 'e' : 'E'; break;
            case 0xAD: case 0x8D: r = (d == 0xAD) ? 'i' : 'I'; break;
            case 0xB3: case 0x93: r = (d == 0xB3) ? 'o' : 'O'; break;
            case 0xBA: case 0x9A: case 0xBC: case 0x9C: r = (d == 0xBA || d == 0xBC) ? 'u' : 'U'; break;
            case 0xB1: case 0x91: r = (d == 0xB1) ? 'n' : 'N'; break;
            default: break;
            }
            if (r != 0) out[n++] = r;
            continue;
        }
        /* otro caracter multibyte (¿ ¡ emojis...): se salta con sus bytes de continuacion */
        while (((unsigned char)in[i + 1] & 0xC0u) == 0x80u) ++i;
    }
    out[n] = '\0';
    return n;
}

/* Quita espacios de los extremos (modifica `s`) y devuelve el inicio. */
static char *trim(char *s)
{
    while (*s == ' ') ++s;
    size_t n = strlen(s);
    while (n > 0u && s[n - 1u] == ' ') s[--n] = '\0';
    return s;
}

static int same_ci(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (x == '\0') return 1;
    }
}

/* Parte la pregunta en lineas de hasta GAME_LINE_MAX; false si una palabra no cabe o sobran lineas. */
static int wrap(const char *question, qc_text_t *t)
{
    uint8_t line = 0u;
    size_t len = 0u;
    const char *p = question;
    while (*p != '\0') {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        size_t w = 0u;
        while (p[w] != '\0' && p[w] != ' ') ++w;
        if (w > GAME_LINE_MAX) return 0;
        if (len > 0u && len + 1u + w > GAME_LINE_MAX) {
            t->lines[line][len] = '\0';
            if (++line >= GAME_QUESTION_LINES) return 0;
            len = 0u;
        }
        if (len > 0u) t->lines[line][len++] = ' ';
        memcpy(&t->lines[line][len], p, w);
        len += w;
        p += w;
    }
    if (len == 0u) return 0;
    t->lines[line][len] = '\0';
    return 1;
}

static int build_one(char *entry, qc_text_t *t)
{
    char *field[4];
    char *cur = entry;
    for (int i = 0; i < 4; ++i) {
        field[i] = cur;
        char *bar = strchr(cur, '|');
        if (i < 3) {
            if (bar == NULL) return 0;
            *bar = '\0';
            cur = bar + 1;
        } else if (bar != NULL) {
            return 0;                                /* campos de mas */
        }
    }
    for (int i = 0; i < 4; ++i) {
        field[i] = trim(field[i]);
        if (field[i][0] == '\0') return 0;
    }
    memset(t, 0, sizeof *t);
    if (!wrap(field[0], t)) return 0;
    for (int o = 0; o < (int)GAME_OPTIONS; ++o) {
        const char *opt = field[1 + o];
        if (strlen(opt) > GAME_OPTION_MAX) return 0;
        strcpy(t->options[o], opt);
        for (int k = 0; k < o; ++k) {
            if (same_ci(t->options[k], opt)) return 0;
        }
    }
    return 1;
}

uint8_t qc_parse(const char *text, qc_set_t *set)
{
    static char work[QC_TEXT_MAX + 1u];                /* texto ya en ASCII; no es reentrante */
    memset(set, 0, sizeof *set);
    if (text == NULL) return 0u;

    to_ascii(text, work, sizeof work);

    char *p = work;
    while (*p != '\0') {
        char *end = p;
        while (*end != '\0' && *end != '#' && *end != '\n') ++end;
        const char sep = *end;
        *end = '\0';

        char *entry = trim(p);
        if (entry[0] != '\0') {
            if (set->count < QC_MAX && build_one(entry, &set->text[set->count])) {
                const qc_text_t *t = &set->text[set->count];
                game_question_t *q = &set->q[set->count];
                for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) {
                    q->lines[l] = t->lines[l][0] != '\0' ? t->lines[l] : NULL;
                }
                for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) q->options[o] = t->options[o];
                q->correct = 0u;
                ++set->count;
            } else if (set->rejected < 255u) {
                ++set->rejected;
            }
        }
        if (sep == '\0') break;
        p = end + 1;
    }
    return set->count;
}
