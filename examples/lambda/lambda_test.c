#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "utils/compiler.h"

typedef struct {
	int32_t (*func)(int32_t, void *);
	void *context;
} lambda_t;

// 定义函数__fun__，构造 lambda_t 结构体
#define lambda(return_type, context_type, context_ptr, body)                   \
	({                                                                     \
		return_type __fn__(int32_t x, void *context) body lambda_t     \
			l = {.func = __fn__,                                   \
			     .context = (void *)context_ptr };                 \
		l;                                                             \
	})

int32_t *map(int32_t *arr, int32_t size, lambda_t lambda_func)
{
	int32_t *result = malloc(size * sizeof(int32_t));
	if (result == NULL) {
		fprintf(stderr, "malloc failed\n");
		return NULL;
	}
	for (int32_t i = 0; i < size; i++) {
		result[i] = lambda_func.func(arr[i], lambda_func.context);
	}
	return result;
}

int32_t main(int32_t argc, char **argv)
{
	int32_t arr[] = { 1, 2, 3, 4, 5 };
	int32_t size = ARRAY_SIZE(arr);

	int32_t multiplier = 3;
	lambda_t tripler = lambda(int32_t, int32_t, &multiplier,
				  { return x * (*(int32_t *)context); });
	int32_t *tripled = map(arr, size, tripler);
	if (tripled == NULL) {
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Tripled: ");
	for (int32_t i = 0; i < size; i++) {
		fprintf(stderr, "%d ", tripled[i]);
	}
	return EXIT_SUCCESS;
}