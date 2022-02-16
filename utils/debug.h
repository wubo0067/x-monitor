/*
 * @Author: CALM.WU
 * @Date: 2021-11-12 16:41:34
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-29 11:59:11
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>

/** Initialize the variable <tt>debugLevel</tt> depending on the value
 *  of <tt>argv[1]</tt>. Normally called from <tt>main</tt> with the program
 *  arguments. If <tt>argv[1]</tt> is or begins with <tt>-debug</tt>, the
 *  value of <tt>debugLevel</tt> is set and <tt>argc, argv</tt> are
 *  modified appropriately. An entry of <tt>-debug5</tt> sets the level to 5.
 *  If the function is not called, the user is responsible for setting
 *  <tt>debugLevel</tt> in other code.
 *  @param argc the number of command line arguments
 *  @param argv the array of command line arguments.
 */
void debugInit(int *argc, const char *argv[]);

/** Send the debug output to a file
 *  @param fileName name of file to write debug output to
 */
void debugToFile(const char *fileName);

/** Close the external file and reset <tt>debugFile</tt> to <tt>stderr</tt>
 */
void debugClose(void);

/** Control how much debug output is produced. Higher values produce more
 * output. See the use in <tt>lDebug()</tt>.
 */
extern int debugLevel;

/** The file where debug output is written. Defaults to <tt>stderr</tt>.
 *  <tt>debugToFile()</tt> allows output to any file.
 */
extern FILE *debugFile;

#ifdef DEBUG
#define DEBUG_ENABLED 1  // debug code available at runtime
#else
/** This macro controls whether all debugging code is optimized out of the
 *  executable, or is compiled and controlled at runtime by the
 *  <tt>debugLevel</tt> variable. The value (0/1) depends on whether
 *  the macro <tt>DEBUG</tt> is defined during the compile.
 */
#define DEBUG_ENABLED 0  // all debug code optimized out
#endif

/** Print the file name, line number, function name and "HERE" */
#define HERE debug("HERE")

/** Expand a name into a string and a value
 *  @param name name of variable
 */
#define debugV(name) #name, (name)

/** Output the name and value of a single variable
 *  @param name name of the variable to print
 */
#define vDebug(fmt, name) debug("%s=(" fmt ")", debugV(name))

/** Simple alias for <tt>lDebug()</tt> */
#define debug(fmt, ...) lDebug(1, fmt, ##__VA_ARGS__)

/** Print this message if the variable <tt>debugLevel</tt> is greater
 *  than or equal to the parameter.
 *  @param level the level at which this information should be printed
 *  @param fmt the formatting string (<b>MUST</b> be a literal
 */
#define lDebug(level, fmt, ...)                                                                                                     \
    do {                                                                                                                            \
        if (DEBUG_ENABLED && (debugLevel >= level)) {                                                                               \
            char       ts[ 32 ];                                                                                                    \
            time_t     t;                                                                                                           \
            struct tm *tm;                                                                                                          \
            time(&t);                                                                                                               \
            tm = localtime(&t);                                                                                                     \
            strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", tm);                                                                         \
            fprintf((debugFile ? debugFile : stderr), "%s %s[%d] %s() " fmt "\n", ts, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
        }                                                                                                                           \
    } while (0)

#ifdef __cplusplus
}
#endif