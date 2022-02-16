/*
 * @Author: CALM.WU
 * @Date: 2022-01-06 14:36:08
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-06 15:07:57
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// https://stackoverflow.com/questions/11761703/overloading-macro-on-number-of-arguments

#define GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define FOO(...) GET_MACRO(__VA_ARGS__, FOO3, FOO2, FOO1)(__VA_ARGS__)

#define FOO1(x) printf("FOO1:%d\n", x)
#define FOO2(x, y) printf("FOO2: %d %d\n", x, y)
#define FOO3(x, y, z) printf("FOO3: %d %d %d\n", x, y, z)

int32_t main(int argc, char const *argv[]) {
    /* code */
    FOO(1);        // ---> GET_MACOR(1, FOO3, FOO2, FOO)
    FOO(1, 2);     // ---> GET_MACRO(1, 2, FOO3, FOO2, FOO)   ---> NAME = FOO2
    FOO(1, 2, 3);  // ---> GET_MACRO(1, 2, 3, FOO3, FOO2, FOO) ---> NAME = FOO3

    return 0;
}
