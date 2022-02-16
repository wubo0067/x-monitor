/*
 * @Author: CALM.WU
 * @Date: 2021-12-23 11:29:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 16:56:47
 */

#include "simple_pattern.h"

#include "common.h"
#include "compiler.h"
#include "log.h"

struct simple_pattern {
    const char *            match;
    size_t                  len;
    enum SIMPLE_PREFIX_MODE mode;
    char                    negative;
    struct simple_pattern * child;
    struct simple_pattern * next;
};

static struct simple_pattern *__parse_pattern(char *str, enum SIMPLE_PREFIX_MODE default_mode) {
    enum SIMPLE_PREFIX_MODE mode;
    struct simple_pattern * child = NULL;

    char *s = str, *c = str;

    // skip asterisks in front
    while (*c == '*')
        c++;

    // find the next asterisk
    while (*c && *c != '*')
        c++;

    // do we have an asterisk in the middle?
    if (*c == '*' && c[1] != '\0') {
        // yes, we have
        child = __parse_pattern(c, default_mode);
        c[1]  = '\0';
    }

    // check what this one matches

    size_t len = strlen(s);
    if (len >= 2 && *s == '*' && s[len - 1] == '*') {
        s[len - 1] = '\0';
        s++;
        mode = SIMPLE_PATTERN_SUBSTRING;
    }
    else if (len >= 1 && *s == '*') {
        s++;
        mode = SIMPLE_PATTERN_SUFFIX;
    }
    else if (len >= 1 && s[len - 1] == '*') {
        s[len - 1] = '\0';
        mode       = SIMPLE_PATTERN_PREFIX;
    }
    else
        mode = default_mode;

    // allocate the structure
    struct simple_pattern *m = calloc(1, sizeof(struct simple_pattern));
    if (*s) {
        m->match = strdup(s);
        m->len   = strlen(m->match);
        m->mode  = mode;
    }
    else {
        m->mode = SIMPLE_PATTERN_SUBSTRING;
    }

    m->child = child;
    return m;
}

static char *__add_wildcarded(const char *matched, size_t matched_size, char *wildcarded, size_t *wildcarded_size) {
    if (unlikely(wildcarded && *wildcarded_size && matched && *matched && matched_size)) {
        size_t wss = *wildcarded_size - 1;
        size_t len = (matched_size < wss) ? matched_size : wss;
        if (likely(len)) {
            strncpy(wildcarded, matched, len);

            *wildcarded_size -= len;
            return &wildcarded[len];
        }
    }

    return wildcarded;
}

static int32_t __match_pattern(struct simple_pattern *m, const char *str, size_t len, char *wildcarded,
                               size_t *wildcarded_size) {
    char *s;

    if (m->len <= len) {
        switch (m->mode) {
        case SIMPLE_PATTERN_SUBSTRING:
            if (!m->len)
                return 1;
            if ((s = strstr(str, m->match))) {
                wildcarded = __add_wildcarded(str, s - str, wildcarded, wildcarded_size);
                if (!m->child) {
                    wildcarded = __add_wildcarded(&s[m->len], len - (&s[m->len] - str), wildcarded, wildcarded_size);
                    return 1;
                }
                return __match_pattern(m->child, &s[m->len], len - (s - str) - m->len, wildcarded, wildcarded_size);
            }
            break;
        case SIMPLE_PATTERN_PREFIX:
            if (unlikely(strncmp(str, m->match, m->len) == 0)) {
                if (!m->child) {
                    wildcarded = __add_wildcarded(&str[m->len], len - m->len, wildcarded, wildcarded_size);
                    return 1;
                }
                return __match_pattern(m->child, &str[m->len], len - m->len, wildcarded, wildcarded_size);
            }
            break;
        case SIMPLE_PATTERN_SUFFIX:
            if (unlikely(strcmp(&str[len - m->len], m->match) == 0)) {
                wildcarded = __add_wildcarded(str, len - m->len, wildcarded, wildcarded_size);
                if (!m->child)
                    return 1;
                return 0;
            }
            break;
        case SIMPLE_PATTERN_EXACT:
        default:
            if (unlikely(strcmp(str, m->match) == 0)) {
                if (!m->child)
                    return 1;
                return 0;
            }
            break;
        }
    }

    return 0;
}

SIMPLE_PATTERN *simple_pattern_create(const char *list, const char *separators, enum SIMPLE_PREFIX_MODE default_mode) {
    struct simple_pattern *root = NULL, *last = NULL;

    if (unlikely(!list || !*list))
        return root;

    int32_t isseparator[256] = {
        [' '] = 1  // space
        ,
        ['\t'] = 1  // tab
        ,
        ['\r'] = 1  // carriage return
        ,
        ['\n'] = 1  // new line
        ,
        ['\f'] = 1  // form feed
        ,
        ['\v'] = 1  // vertical tab
    };

    if (unlikely(separators && *separators)) {
        memset(&isseparator[0], 0, sizeof(isseparator));
        while (*separators)
            isseparator[(unsigned char)*separators++] = 1;
    }

    char *      buf = malloc(strlen(list) + 1);
    const char *s   = list;

    while (s && *s) {
        buf[0]  = '\0';
        char *c = buf;

        char negative = 0;

        // skip all spaces
        while (isseparator[(unsigned char)*s])
            s++;

        if (*s == '!') {
            negative = 1;
            s++;
        }

        // empty string
        if (unlikely(!*s))
            break;

        // find the next space
        char escape = 0;
        while (*s) {
            if (*s == '\\' && !escape) {
                escape = 1;
                s++;
            }
            else {
                if (isseparator[(unsigned char)*s] && !escape) {
                    s++;
                    break;
                }
                *c++   = *s++;
                escape = 0;
            }
        }

        // terminate our string
        *c = '\0';

        // if we matched the empty string, skip it
        if (unlikely(!*buf))
            continue;

        // fprintf(stderr, "FOUND PATTERN: '%s'\n", buf);
        struct simple_pattern *m = __parse_pattern(buf, default_mode);
        m->negative              = negative;

        // link it at the end
        if (unlikely(!root))
            root = last = m;
        else {
            last->next = m;
            last       = m;
        }
    }

    free(buf);
    return (SIMPLE_PATTERN *)root;
}

int32_t simple_pattern_matches_extract(SIMPLE_PATTERN *list, const char *str, char *wildcarded,
                                       size_t wildcarded_size) {
    struct simple_pattern *m, *root = (struct simple_pattern *)list;

    if (unlikely(!root || !str || !*str))
        return 0;

    size_t len = strlen(str);
    for (m = root; m; m = m->next) {
        char * ws  = wildcarded;
        size_t wss = wildcarded_size;
        if (unlikely(ws))
            *ws = '\0';

        if (__match_pattern(m, str, len, ws, &wss)) {

            // if(ws && wss)
            //    fprintf(stderr, "FINAL WILDCARDED '%s' of length %zu\n", ws, strlen(ws));

            if (m->negative)
                return 0;
            return 1;
        }
    }

    return 0;
}

static void __free_pattern(struct simple_pattern *m) {
    if (!m)
        return;

    __free_pattern(m->child);
    __free_pattern(m->next);
    free((void *)m->match);
    free(m);
}

void simple_pattern_free(SIMPLE_PATTERN *list) {
    if (!list)
        return;

    __free_pattern(((struct simple_pattern *)list));
}

void simple_pattern_dump(SIMPLE_PATTERN *p) {
    struct simple_pattern *root = (struct simple_pattern *)p;
    if (root == NULL) {
        debug("dump_pattern(NULL)");
        return;
    }
    debug("dump_pattern(%p) child=%p next=%p mode=%u match=%s", root, root->child, root->next, root->mode, root->match);
    if (root->child != NULL)
        simple_pattern_dump((SIMPLE_PATTERN *)root->child);
    if (root->next != NULL)
        simple_pattern_dump((SIMPLE_PATTERN *)root->next);
}

/* Heuristic: decide if the pattern could match a DNS name.

   Although this functionality is used directly by socket.c:connection_allowed() it must be in this file
   because of the SIMPLE_PATTERN/simple_pattern structure hiding.
   Based on RFC952 / RFC1123. We need to decide if the pattern may match a DNS name, or not. For the negative
   cases we need to be sure that it can only match an ipv4 or ipv6 address:
     * IPv6 addresses contain ':', which are illegal characters in DNS.
     * IPv4 addresses cannot contain alpha- characters.
     * DNS TLDs must be alphanumeric to distinguish from IPv4.
   Some patterns (e.g. "*a*" ) could match multiple cases (i.e. DNS or IPv6).
   Some patterns will be awkward (e.g. "192.168.*") as they look like they are intended to match IPv4-only
   but could match DNS (i.e. "192.168.com" is a valid name).
*/
static void __scan_is_potential_name(struct simple_pattern *p, int32_t *alpha, int32_t *colon, int32_t *wildcards) {
    while (p) {
        if (p->match) {
            if (p->mode == SIMPLE_PATTERN_EXACT && !strcmp("localhost", p->match)) {
                p = p->child;
                continue;
            }
            char const *scan = p->match;
            while (*scan != 0) {
                if ((*scan >= 'a' && *scan <= 'z') || (*scan >= 'A' && *scan <= 'Z'))
                    *alpha = 1;
                if (*scan == ':')
                    *colon = 1;
                scan++;
            }
            if (p->mode != SIMPLE_PATTERN_EXACT)
                *wildcards = 1;
            p = p->child;
        }
    }
}

int32_t simple_pattern_is_potential_name(SIMPLE_PATTERN *p) {
    int32_t alpha = 0, colon = 0, wildcards = 0;

    struct simple_pattern *root = (struct simple_pattern *)p;
    while (root != NULL) {
        if (root->match != NULL) {
            __scan_is_potential_name(root, &alpha, &colon, &wildcards);
        }
        if (root->mode != SIMPLE_PATTERN_EXACT)
            wildcards = 1;
        root = root->next;
    }
    return (alpha || wildcards) && !colon;
}

char *simple_pattern_iterate(SIMPLE_PATTERN **p) {
    struct simple_pattern * root  = (struct simple_pattern *)*p;
    struct simple_pattern **Proot = (struct simple_pattern **)p;

    (*Proot) = (*Proot)->next;
    return (char *)root->match;
}

char *simple_pattern_trim_around_equal(char *src) {
    char *store = malloc(strlen(src) + 1);
    if (!store)
        return NULL;

    char *dst = store;
    while (*src) {
        if (*src == '=') {
            if (*(dst - 1) == ' ')
                dst--;

            *dst++ = *src++;
            if (*src == ' ')
                src++;
        }

        *dst++ = *src++;
    }
    *dst = 0x00;

    return store;
}