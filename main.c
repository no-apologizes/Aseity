#include "Headers/lexer.h"
#include <stdio.h>
#include <stdlib.h>

int main() { //const int argc, char **argv
    const char *andie =
        ""
        "str msg = \"Data payload: \\\"poses\\\"\\n\" \n"
        "bool flag = 'ℝ' \n"
        "population 2 * return |";

    printf("Test script:\n");
    printf("%s\n\n", andie);
    printf("Lexical Analysis Output\n\n");

    lexer_init(andie);

    Token t;
    do { t = lexer_next_token();
        printf("Line %2ld, Col %2ld | Type: %2d | Length: %2zu |\n", //" Value: %.*s\n",
               t.line,
               t.column, t.type, t.length);
    } while (t.type != TOKEN_EOF && t.type != TOKEN_UNKNOWN);

    if (t.type == TOKEN_UNKNOWN) {
        printf("\nUnknown Character\n");
    }

    return 0;
}
