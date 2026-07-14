#include "Headers/lexer.h"
#include <stdio.h>

// Link
extern void lexer_init(const char *source);
extern Token lexer_next_token(void);

int main(void) {
    const char *test_script =
        "i64 population = 5000 |\n"
        "f64 ratio = 3.1415 |\n"
        "population 2 * return |";


    printf("Test script:\n");
    printf("%s\n\n", test_script);
    printf("Lexical Analysis Output\n\n");

    lexer_init(test_script);

    Token t;
    do {
        t = lexer_next_token();

        // %.*s requires two arguments: the length (cast to int) and the char pointer
        printf("Line %2d, Col %2d | Type: %2d | Length: %2zu | Value: %.*s\n",
               t.line,
               t.column,
               t.type,
               t.length,
               (int)t.length,
               t.start);

    } while (t.type != TOKEN_EOF && t.type != TOKEN_UNKNOWN);

    if (t.type == TOKEN_UNKNOWN) {
        printf("Unkown token");
    }

    return 0;
}