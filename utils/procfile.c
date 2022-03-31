/*
 * @Author: CALM.WU
 * @Date: 2021-12-02 10:34:06
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-29 16:32:50
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
char *procfile_filename(struct proc_file *pf) {
    if (pf->filename[0])
        return pf->filename;

    char    buffer[XM_FILENAME_MAX] = { 0 };
    int32_t ret = snprintf(buffer, XM_FILENAME_MAX - 1, "/proc/self/fd/%d", pf->fd);
    buffer[ret] = '\0';

    ssize_t l_size = readlink(buffer, pf->filename, FILENAME_MAX - 1);
    if (likely(-1 != l_size)) {
        pf->filename[l_size] = '\0';
    } else {
        snprintf(pf->filename, FILENAME_MAX, "unknown filename for link fd '%s'", buffer);
    }
    return pf->filename;
}

//-------------------------------------------------------------------------------------------------
// lines
static inline struct pf_lines *__new_pflines() {
    struct pf_lines *pfls = (struct pf_lines *)malloc(
        sizeof(struct pf_lines) + PFLINES_INCREASE_STEP * sizeof(struct pf_line));
    pfls->len = 0;
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

static inline size_t *__add_pfline(struct proc_file *pf) {
    struct pf_lines *lines = pf->lines;

    // 容量已满，扩容
    if (unlikely(lines->len == lines->size)) {
        // realloc返回的地址可能与原地址不同，所以需要重新赋值
        pf->lines = (struct pf_lines *)realloc(lines, sizeof(struct pf_lines)
                                                          + (lines->size + PFLINES_INCREASE_STEP)
                                                                * sizeof(struct pf_line));
        lines = pf->lines;
        // 行容量扩容
        lines->size += PFLINES_INCREASE_STEP;
    }

    // 使用一个新行
    struct pf_line *line = &(lines->lines[lines->len]);
    line->words = 0;
    line->first = pf->words->len;

    // debug("adding line %lu at word %lu", lines->len, line->first);

    lines->len++;
    return &line->words;
}

//-------------------------------------------------------------------------------------------------
// words

static inline struct pf_words *__new_pfwords() {
    // 整个内容的word数组
    struct pf_words *pfw =
        (struct pf_words *)malloc(sizeof(struct pf_words) + PFWORDS_INCREASE_STEP * sizeof(char *));
    pfw->len = 0;
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

static void __add_pfword(struct proc_file *pf, char *word) {
    struct pf_words *pfws = pf->words;
    if (unlikely(pfws->len == pfws->size)) {
        pf->words = (struct pf_words *)realloc(
            pfws, sizeof(struct pf_words) + (pfws->size + PFWORDS_INCREASE_STEP) * sizeof(char *));
        pfws = pf->words;
        pfws->size += PFWORDS_INCREASE_STEP;
    }

    pfws->words[pfws->len++] = word;
}

//-------------------------------------------------------------------------------------------------
// file

static void __procfile_parser(struct proc_file *pf) {
    char *s = pf->data;             // 内容起始地址
    char *e = &pf->data[pf->len];   // 内容结束地址
    char *t = pf->data;             // 当前word的首字符地址

    enum procfile_separator_type *seps = pf->separators;

    char   quote = 0;    // the quote character - only when in quoted string
    size_t opened = 0;   // counts the number of open parenthesis

    // 添加第一行，返回行的word地址
    size_t *line_words = __add_pfline(pf);

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
                    __add_pfword(pf, t);
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
            __add_pfword(pf, t);
            (*line_words)++;
            t = ++s;
            // 新开一行，行的word数量为0
            line_words = __add_pfline(pf);
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
                __add_pfword(pf, t);
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
                    __add_pfword(pf, t);
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
        if (unlikely(pf->len >= pf->size)) {
            // we are going to loose the last byte
            s = &pf->data[pf->size - 1];
        }
        *s = '\0';
        __add_pfword(pf, t);
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

static void __procfile_set_separators(struct proc_file *pf, const char *seps) {
    pthread_once(&__procfile_init_defseps_once, __procfile_init_defseps);

    enum procfile_separator_type *ffs = pf->separators, *ffd = __def_seps_type,
                                 *ffe = &__def_seps_type[256];
    // 用defseps初始化ff->seperators
    while (ffd != ffe) {
        *ffs++ = *ffd++;
    }

    // 设置自定义的seps
    if (unlikely(!seps))
        seps = " \t=|";

    ffs = pf->separators;
    const char *s = seps;
    while (*s) {
        ffs[(int)*s++] = PF_CHAR_IS_SEPARATOR;
    }
}

struct proc_file *procfile_readall(struct proc_file *pf) {
    pf->len = 0;
    ssize_t r = 1;

    while (r > 0) {
        ssize_t s = pf->len;        // 使用空间
        ssize_t x = pf->size - s;   // 剩余空间

        if (unlikely(!x)) {
            // 空间不够，扩展
            // debug("procfile %s buffer size not enough, expand to %lu",
            //       procfile_filename(pf), pf->size +
            //       PROCFILE_DATA_BUFFER_SIZE);
            // 再增加一个PROCFILE_DATA_BUFFER_SIZE
            pf = (struct proc_file *)realloc(pf, sizeof(struct proc_file) + pf->size
                                                     + PROCFILE_DATA_BUFFER_SIZE);
            pf->size += PROCFILE_DATA_BUFFER_SIZE;
        }

        // debug("read file '%s', from position %ld with length '%ld'",
        //         procfile_filename(pf), s, (pf->size - s));
        r = read(pf->fd, &pf->data[s], pf->size - s);
        if (unlikely(r < 0)) {
            error("read file '%s' on fd %d failed, error %s", procfile_filename(pf), pf->fd,
                  strerror(errno));
            procfile_close(pf);
            return NULL;
        }
        pf->len += r;
    }

    // debug("rewind file '%s'", procfile_filename(pf));
    lseek(pf->fd, 0, SEEK_SET);

    __reset_pfilines(pf->lines);
    __reset_pfwords(pf->words);
    __procfile_parser(pf);

    debug("read file '%s' done", procfile_filename(pf));

    return pf;
}

// open a /proc or /sys file
struct proc_file *procfile_open(const char *filename, const char *separators, uint32_t flags) {
    debug("open procfile '%s'", filename);

    int32_t fd = open(filename, O_RDONLY, 0666);
    if (unlikely(fd == -1)) {
        error("open procfile '%s' failed. error: %s", filename, strerror(errno));
        return NULL;
    }

    struct proc_file *pf =
        (struct proc_file *)malloc(sizeof(struct proc_file) + PROCFILE_DATA_BUFFER_SIZE);
    // strlcpy(pf->filename, filename, FILENAME_MAX);
    pf->filename[0] = '\0';
    pf->fd = fd;
    pf->size = PROCFILE_DATA_BUFFER_SIZE;
    pf->len = 0;
    pf->flags = flags;
    pf->lines = __new_pflines();
    pf->words = __new_pfwords();

    // set separators 设置分隔符
    __procfile_set_separators(pf, separators);
    return pf;
}

void procfile_close(struct proc_file *pf) {
    if (unlikely(!pf))
        return;

    debug("close procfile %s", procfile_filename(pf));

    if (likely(pf->lines))
        __free_pflines(pf->lines);

    if (likely(pf->words))
        __free_pfwords(pf->words);

    if (likely(pf->fd != -1)) {
        close(pf->fd);
        pf->fd = -1;
    }
    free(pf);
}

struct proc_file *procfile_reopen(struct proc_file *pf, const char *filename,
                                  const char *separators, uint32_t flags) {
    if (unlikely(!pf))
        return procfile_open(filename, separators, flags);

    if (likely(pf->fd != -1)) {
        // info("PROCFILE: closing fd %d", pf->fd);
        close(pf->fd);
    }

    pf->fd = open(filename, O_RDONLY, 0666);
    if (unlikely(pf->fd == -1)) {
        procfile_close(pf);
        return NULL;
    }

    // strlcpy(pf->filename, filename, FILENAME_MAX);
    pf->filename[0] = '\0';
    pf->flags = flags;

    // do not do the separators again if NULL is given
    if (likely(separators))
        __procfile_set_separators(pf, separators);

    return pf;
}

void procfile_set_quotes(struct proc_file *pf, const char *quotes) {
    enum procfile_separator_type *seps = pf->separators;

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

void procfile_set_open_close(struct proc_file *pf, const char *open, const char *close) {
    enum procfile_separator_type *seps = pf->separators;

    // remove all open/close
    int32_t index = 256;
    while (index--) {
        if (unlikely(seps[index] == PF_CHAR_IS_OPEN || seps[index] == PF_CHAR_IS_CLOSE))
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

void procfile_print(struct proc_file *pf) {
    size_t lines = procfile_lines(pf), l;
    char  *s = NULL;

    debug("procfile '%s' has %lu lines and %lu words", procfile_filename(pf), lines,
          pf->words->len);

    for (l = 0; l < lines; l++) {
        size_t words = procfile_linewords(pf, l);

        debug("line %lu starts at word %lu and has %lu words", l, pf->lines->lines[l].first,
              words);   // pf->lines->lines[l].words);

        size_t w;
        for (w = 0; w < words; w++) {
            s = procfile_lineword(pf, l, w);
            debug("\t[%lu.%lu] '%s'", l, w, s);
        }
    }
}
