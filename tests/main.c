//
//  main.c
//  tests
//
//  Created by kejinlu on 2026/4/21.
//

#include "unity.h"
#include <stdio.h>

void run_uobject_tests(void);
void run_ucache_tests(void);
void run_uhash_tests(void);

int main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;

    printf("\n========================================\n");
    printf("  libuobject Test Suite (Unity)\n");
    printf("========================================\n\n");

    run_uobject_tests();
    run_ucache_tests();
    run_uhash_tests();

    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");

    return UnityEnd();
}
