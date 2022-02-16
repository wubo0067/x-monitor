/*
 * @Author: CALM.WU
 * @Date: 2021-11-12 16:43:11
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-12 16:47:15
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

int debugLevel = 0;

FILE *debugFile = 0;

static const char *prefix = "-debug";

/** This routine will initialize the variables from the arguments
 *  passed to <tt>main()</tt>. If the <b>first</b> argument is, or starts with
 *  <tt>-debug</tt> the class is initialized and the first parameter is
 *  removed from the <tt>args</tt>. The first parameter may have one of
 *  three forms:
 *  <ol>
 *  <li><tt>-debug</tt> - turn on debugging at level 1</li>
 *  <li><tt>-debugValue</tt> - set debugging level to <tt>Value</tt>
 *  (e.g. <tt>-debug5</tt>)</li>
 *  <li><tt>-debugValue@fileName</tt> set debugging level to <tt>Value</tt>
 *  and send debugging output to the file <tt>fileName</tt>. If you use
 *  this option, the file must b closed using <tt>debugClose()</tt>.</li>
 *  </ol>
 *  On return, <tt>argc</tt> and <tt>argv[]</tt> may be modified.
 *  @param  argc - the number of parameters passed to <tt>main</tt>
 *  @param  args the array of arguments passed to <tt>main</tt>
 */

void debugInit(int *argc, const char *argv[]) {
    debugFile = stderr;

    if (*argc > 1) {
        const char *arg1 = argv[ 1 ];
        size_t      len  = strlen(prefix);

        if (strncmp(arg1, prefix, len) == 0) {
            debugLevel = 1;

            char *ats = strchr(arg1, '@');

            if (ats) {
                *ats = '\0';
                debugToFile(ats + 1);
            }

            if (strlen(arg1) > len)
                debugLevel = atoi(arg1 + len);

            (*argc)--;  // decrement number of arguments

            for (int i = 1; i < *argc; i++)  // remove first argument
                argv[ i ] = argv[ i + 1 ];
        }
    }
}

/** Send debugging output to a file.
 *  @param fileName name of file to send output to
 */

void debugToFile(const char *fileName) {
    debugClose();

    FILE *f = fopen(fileName, "w");  // "w+" ?

    if (f)
        debugFile = f;
}

/** Close the output file if it was set in <tt>toFile()</tt> */

void debugClose(void) {
    if (debugFile && (debugFile != stderr)) {
        fclose(debugFile);
        debugFile = stderr;
    }
}
