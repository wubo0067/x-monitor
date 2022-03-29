/*
 * @Author: CALM.WU
 * @Date: 2021-12-01 16:58:42
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-29 16:32:14
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

#define PROCFILE_FLAG_DEFAULT 0x00000000
#define PROCFILE_FLAG_NO_ERROR_ON_FILE_IO 0x00000001

struct pf_words {
    size_t len;       // used entries 挡墙字数
    size_t size;      // capacity 字的最大容量
    char  *words[];   // array of pointers
};

struct pf_line {
    size_t words;   // 一行word的数量
    size_t first;   // 一行开始word在words中的下标
};

struct pf_lines {
    size_t         len;       // used entries 行数
    size_t         size;      // capacity 行数最大容量
    struct pf_line lines[];   // array of lines
};

enum procfile_separator_type {
    PF_CHAR_IS_SEPARATOR,   // NUL '\0' (null character)
    PF_CHAR_IS_NEWLINE,     // SOH (start of heading)
    PF_CHAR_IS_WORD,        // STX (start of text)
    PF_CHAR_IS_QUOTE,       // ETX (end of text)
    PF_CHAR_IS_OPEN,        // EOT (end of transmission)
    PF_CHAR_IS_CLOSE        // ENQ (enquiry)
};

struct proc_file {
    char     filename[FILENAME_MAX + 1];   //
    uint32_t flags;
    int      fd;     // the file descriptor
    size_t   len;    // the bytes we have placed into data
    size_t   size;   // the bytes we have allocated for data

    struct pf_lines *lines;
    struct pf_words *words;

    enum procfile_separator_type separators[256];

    char data[];   // allocated buffer to keep file contents
};

// close the proc file and free all related memory
extern void procfile_close(struct proc_file *pf);

// (re)read and parse the proc file
extern struct proc_file *procfile_readall(struct proc_file *pf);

// open a /proc or /sys file
extern struct proc_file *procfile_open(const char *filename, const char *separators,
                                       uint32_t flags);

extern struct proc_file *procfile_reopen(struct proc_file *pf, const char *filename,
                                         const char *separators, uint32_t flags);

//
extern char *procfile_filename(struct proc_file *pf);

//
extern void procfile_set_quotes(struct proc_file *pf, const char *quotes);

//
extern void procfile_set_open_close(struct proc_file *pf, const char *open, const char *close);

//
extern void procfile_print(struct proc_file *pf);

//
#define procfile_lines(pf) ((pf)->lines->len)
//
#define procfile_linewords(pf, line) \
    (((pf)->lines->len > (line)) ? (pf)->lines->lines[(line)].words : 0)

// return the Nth word of the file, or empty string
#define procfile_word(pf, word) (((word) < (pf)->words->len) ? (pf)->words->words[(word)] : "")

// return the first word of the Nth line, or empty string
#define procfile_line(pf, line) \
    (((line) < procfile_lines(pf)) ? procfile_word((pf), (pf)->lines->lines[(line)].first) : "")

// return the Nth word of the current line
#define procfile_lineword(pf, line, word)                                         \
    (((line) < procfile_lines(pf) && (word) < procfile_linewords((pf), (line))) ? \
         procfile_word((pf), (pf)->lines->lines[(line)].first + (word)) :         \
         "")
