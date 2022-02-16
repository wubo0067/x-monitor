/*
 * @Author: CALM.WU
 * @Date: 2021-12-02 10:34:06
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-20 17:08:10
 */

#include "common.h"
#include "compiler.h"
#include "log.h"
#include "procfile.h"

#define PROCFILE_DATA_BUFFER_SIZE 512
#define PFLINES_INCREASE_STEP 10
#define PFWORDS_INCREASE_STEP 200

static pthread_once_t __procfile_init_defseps_once = PTHREAD_ONCE_INIT;

static enum procfile_separator_type __def_seps_type[256];

//-------------------------------------------------------------------------------------------------
// file name
char *procfile_filename(struct proc_file *ff) {
    if (ff->filename[0])
        return ff->filename;

    char buffer[FILENAME_MAX + 1];
    snprintf(buffer, FILENAME_MAX, "/proc/self/fd/%d", ff->fd);

    ssize_t l = readlink(buffer, ff->filename, FILENAME_MAX);
    if (unlikely(l == 0)) {
        ff->filename[l] = 0;
    } else {
        snprintf(ff->filename, FILENAME_MAX, "unknown filename for fd %d",
                 ff->fd);
    }
    return ff->filename;
}

//-------------------------------------------------------------------------------------------------
// lines
static inline struct pf_lines *__new_pflines() {
    struct pf_lines *pfls = (struct pf_lines *)malloc(
        sizeof(struct pf_lines)
        + PFLINES_INCREASE_STEP * sizeof(struct pf_line));
    pfls->len  = 0;
    pfls->size = PFLINES_INCREASE_STEP;
    return pfls;
}

static inline void __reset_pfilines(struct pf_lines *pfls) {
    pfls->len = 0;
}

static inline void __free_pflines(struct pf_lines *pfls) {
    free(pfls);
    pfls = NULL;
}

static inline size_t *__add_pfline(struct proc_file *ff) {
    struct pf_lines *lines = ff->lines;

    // 容量已满，扩容
    if (unlikely(lines->len == lines->size)) {
        // realloc返回的地址可能与原地址不同，所以需要重新赋值
        ff->lines = (struct pf_lines *)realloc(
            lines, sizeof(struct pf_lines)
                       + (lines->size + PFLINES_INCREASE_STEP)
                             * sizeof(struct pf_line));
        lines = ff->lines;
        // 行容量扩容
        lines->size += PFLINES_INCREASE_STEP;
    }

    // 使用一个新行
    struct pf_line *line = &(lines->lines[lines->len]);
    line->words          = 0;
    line->first          = ff->words->len;

    // debug("adding line %lu at word %lu", lines->len, line->first);

    lines->len++;
    return &line->words;
}

//-------------------------------------------------------------------------------------------------
// words

static inline struct pf_words *__new_pfwords() {
    // 整个内容的word数组
    struct pf_words *pfw = (struct pf_words *)malloc(
        sizeof(struct pf_words) + PFWORDS_INCREASE_STEP * sizeof(char *));
    pfw->len  = 0;
    pfw->size = PFWORDS_INCREASE_STEP;
    return pfw;
}

static inline void __reset_pfwords(struct pf_words *pfw) {
    pfw->len = 0;
}

static inline void __free_pfwords(struct pf_words *pfw) {
    free(pfw);
    pfw = NULL;
}

static void __add_pfword(struct proc_file *ff, char *word) {
    struct pf_words *pfws = ff->words;
    if (unlikely(pfws->len == pfws->size)) {
        ff->words = (struct pf_words *)realloc(
            pfws, sizeof(struct pf_words)
                      + (pfws->size + PFWORDS_INCREASE_STEP) * sizeof(char *));
        pfws = ff->words;
        pfws->size += PFWORDS_INCREASE_STEP;
    }

    pfws->words[pfws->len++] = word;
}

//-------------------------------------------------------------------------------------------------
// file

static void __procfile_parser(struct proc_file *ff) {
    char *s = ff->data;            // 内容起始地址
    char *e = &ff->data[ff->len];  // 内容结束地址
    char *t = ff->data;            // 当前word的首字符地址

    enum procfile_separator_type *seps = ff->separators;

    char   quote  = 0;  // the quote character - only when in quoted string
    size_t opened = 0;  // counts the number of open parenthesis

    // 添加第一行，返回行的word地址
    size_t *line_words = __add_pfline(ff);

    while (s < e) {
        // 判断字符类型
        enum procfile_separator_type ct = seps[(unsigned char)(*s)];

        if (likely(ct == PF_CHAR_IS_WORD)) {
            s++;
        } else if (likely(ct == PF_CHAR_IS_SEPARATOR)) {
            if (!quote && !opened) {
                if (s != t) {
                    // 这是个分隔符，找到一个word在分隔符前
                    *s = '\0';
                    __add_pfword(ff, t);
                    // 行的word数量+1
                    (*line_words)++;
                    t = ++s;
                } else {
                    // 第一个字符是分隔符，跳过
                    t = ++s;
                }
            } else {
                s++;
            }
        } else if (likely(ct == PF_CHAR_IS_NEWLINE)) {
            // 会不会有/r/n这种windows换行方式，linux是\n
            // 换行符
            *s = '\0';
            __add_pfword(ff, t);
            (*line_words)++;
            t = ++s;
            // 新开一行，行的word数量为0
            line_words = __add_pfline(ff);
        } else if (likely(ct == PF_CHAR_IS_QUOTE)) {
            // 引号
            if (unlikely(!quote && s == t)) {
                // quote opened at the beginning
                quote = *s;
                // 跳过
                t = ++s;
            } else if (unlikely(quote && quote == *s)) {
                // quote closed
                quote = 0;

                *s = '\0';
                // 整体是个word
                __add_pfword(ff, t);
                (*line_words)++;
                // 跳过这个word
                t = ++s;
            } else {
                s++;
            }
        } else if (likely(ct == PF_CHAR_IS_OPEN)) {
            if (s == t) {
                opened++;
                t = ++s;
            } else if (opened) {
                opened++;
                s++;
            } else
                s++;
        } else if (likely(ct == PF_CHAR_IS_CLOSE)) {
            if (opened) {
                opened--;

                if (!opened) {
                    *s = '\0';
                    __add_pfword(ff, t);
                    (*line_words)++;
                    t = ++s;
                } else
                    s++;
            } else
                s++;
        } else {
            fatal("Internal Error: procfile_readall() does not handle all the "
                  "cases.");
        }
    }

    if (likely(s > t && t < e)) {
        if (unlikely(ff->len >= ff->size)) {
            // we are going to loose the last byte
            s = &ff->data[ff->size - 1];
        }
        *s = '\0';
        __add_pfword(ff, t);
        (*line_words)++;
    }
}

static void __procfile_init_defseps() {
    int32_t i = 256;
    while (i--) {
        if (unlikely(i == '\n' || i == '\r'))
            __def_seps_type[i] = PF_CHAR_IS_NEWLINE;

        else if (unlikely(isspace(i) || !isprint(i)))
            __def_seps_type[i] = PF_CHAR_IS_SEPARATOR;

        else
            __def_seps_type[i] = PF_CHAR_IS_WORD;
    }
}

static void __procfile_set_separators(struct proc_file *ff, const char *seps) {
    pthread_once(&__procfile_init_defseps_once, __procfile_init_defseps);

    enum procfile_separator_type *ffs = ff->separators, *ffd = __def_seps_type,
                                 *ffe = &__def_seps_type[256];
    // 用defseps初始化ff->seperators
    while (ffd != ffe) {
        *ffs++ = *ffd++;
    }

    // 设置自定义的seps
    if (unlikely(!seps))
        seps = " \t=|";

    ffs           = ff->separators;
    const char *s = seps;
    while (*s) {
        ffs[(int)*s++] = PF_CHAR_IS_SEPARATOR;
    }
}

struct proc_file *procfile_readall(struct proc_file *ff) {
    ff->len   = 0;
    ssize_t r = 1;

    while (r > 0) {
        ssize_t s = ff->len;       // 使用空间
        ssize_t x = ff->size - s;  // 剩余空间

        if (unlikely(!x)) {
            // 空间不够，扩展
            // debug("procfile %s buffer size not enough, expand to %lu",
            //       procfile_filename(ff), ff->size +
            //       PROCFILE_DATA_BUFFER_SIZE);
            // 再增加一个PROCFILE_DATA_BUFFER_SIZE
            ff = (struct proc_file *)realloc(ff,
                                             sizeof(struct proc_file) + ff->size
                                                 + PROCFILE_DATA_BUFFER_SIZE);
            ff->size += PROCFILE_DATA_BUFFER_SIZE;
        }

        // debug("read file '%s', from position %ld with length '%ld'",
        //         procfile_filename(ff), s, (ff->size - s));
        r = read(ff->fd, &ff->data[s], ff->size - s);
        if (unlikely(r < 0)) {
            error("read file '%s' on fd %d failed, error %s",
                  procfile_filename(ff), ff->fd, strerror(errno));
            procfile_close(ff);
            return NULL;
        }
        ff->len += r;
    }

    // debug("rewind file '%s'", procfile_filename(ff));
    lseek(ff->fd, 0, SEEK_SET);

    __reset_pfilines(ff->lines);
    __reset_pfwords(ff->words);
    __procfile_parser(ff);

    debug("read file '%s' done", procfile_filename(ff));

    return ff;
}

// open a /proc or /sys file
struct proc_file *procfile_open(const char *filename, const char *separators,
                                uint32_t flags) {
    debug("open procfile '%s'", filename);

    int32_t fd = open(filename, O_RDONLY, 0666);
    if (unlikely(fd == -1)) {
        error("open procfile '%s' failed. error: %s", filename,
              strerror(errno));
        return NULL;
    }

    struct proc_file *ff = (struct proc_file *)malloc(
        sizeof(struct proc_file) + PROCFILE_DATA_BUFFER_SIZE);
    strncpy(ff->filename, filename, FILENAME_MAX);
    // ff->filename[0] = '\0';
    ff->fd    = fd;
    ff->size  = PROCFILE_DATA_BUFFER_SIZE;
    ff->len   = 0;
    ff->flags = flags;
    ff->lines = __new_pflines();
    ff->words = __new_pfwords();

    // set separators 设置分隔符
    __procfile_set_separators(ff, separators);
    return ff;
}

void procfile_close(struct proc_file *ff) {
    if (unlikely(!ff))
        return;

    debug("close procfile %s", procfile_filename(ff));

    if (likely(ff->lines))
        __free_pflines(ff->lines);

    if (likely(ff->words))
        __free_pfwords(ff->words);

    if (likely(ff->fd != -1)) {
        close(ff->fd);
        ff->fd = -1;
    }
    free(ff);
}

void procfile_set_quotes(struct proc_file *ff, const char *quotes) {
    enum procfile_separator_type *seps = ff->separators;

    // remote all quotes
    int32_t index = 256;
    while (index--) {
        if (seps[index] == PF_CHAR_IS_QUOTE)
            seps[index] = PF_CHAR_IS_WORD;
    }

    // if nothing given, return
    if (unlikely(!quotes || !*quotes))
        return;

    // 字符串
    const char *qs = quotes;
    while (*qs) {
        seps[(int)*qs++] = PF_CHAR_IS_QUOTE;
    }
}

void procfile_set_open_close(struct proc_file *ff, const char *open,
                             const char *close) {
    enum procfile_separator_type *seps = ff->separators;

    // remove all open/close
    int32_t index = 256;
    while (index--) {
        if (unlikely(seps[index] == PF_CHAR_IS_OPEN
                     || seps[index] == PF_CHAR_IS_CLOSE))
            seps[index] = PF_CHAR_IS_WORD;
    }

    // if nothing given, return
    if (unlikely(!open || !*open || !close || !*close))
        return;

    // set the opens
    const char *s = open;
    while (*s) {
        seps[(int)*s++] = PF_CHAR_IS_OPEN;
    }

    s = close;
    while (*s) {
        seps[(int)*s++] = PF_CHAR_IS_CLOSE;
    }
}

void procfile_print(struct proc_file *ff) {
    size_t lines = procfile_lines(ff), l;
    char * s     = NULL;

    debug("procfile '%s' has %lu lines and %lu words", procfile_filename(ff),
          lines, ff->words->len);

    for (l = 0; l < lines; l++) {
        size_t words = procfile_linewords(ff, l);

        debug("line %lu starts at word %lu and has %lu words", l,
              ff->lines->lines[l].first,
              words);  // ff->lines->lines[l].words);

        size_t w;
        for (w = 0; w < words; w++) {
            s = procfile_lineword(ff, l, w);
            debug("\t[%lu.%lu] '%s'", l, w, s);
        }
    }
}
