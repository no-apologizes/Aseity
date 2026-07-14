#include "Headers/lexer.h"
#include <stdio.h>

int main(void) {
    const char *test_script =
        "str msg = \"Data payload: \\\"Aseity\\\"\\n\" |\n"
        "bool flag = 'ℝ' |\n"
        "population 2 * return |";

    printf("Test script:\n");
    printf("%s\n\n", test_script);
    printf("Lexical Analysis Output\n\n");

    lexer_init(test_script);

    Token t;
    do { t = lexer_next_token();

        printf("Line %2d, Col %2d | Type: %2d | Length: %2zu | Value: %.*s\n",
               t.line,
               t.column,
               t.type,
               t.length,
               (int)t.length,
               t.start);

    } while (t.type != TOKEN_EOF && t.type != TOKEN_UNKNOWN);

    if (t.type == TOKEN_UNKNOWN) {
        printf("\nUnknown Character\n");
    }

    return 0;
}