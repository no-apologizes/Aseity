#include "arena.h"
#include "ast_print.h"
#include "lexer.h"
#include "parser.h"
#include "string_pool.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *source =
    "import \"math.asei\"|\n"
    "\n"
    "struct Point [\n"
    "    i64 x|\n"
    "    i64 y|\n"
    "]|\n"
    "\n"
    "./ postfix: the operands first, then the operator \\.\n"
    "i64 sum(a: i64, b: i64) [\n"
    "    a b + return|\n"
    "]|\n"
    "\n"
    "i64 origin(argc: i64, argv: str{}) [\n"
    "    Point p|\n"
    "    Point *pp = &p|\n"
    "    p.x = 5|\n"
    "    char c = 'a'|\n"
    "    i64 total = 0|\n"
    "    as (i64 i = 0, i 10 <, i++) [\n"
    "        total += i 2 *|\n"
    "        if (total 20 >) [ total -= 1| ] else [ total += 1| ]|\n"
    "    ]|\n"
    "    i64{3} nums = {1, 2, 3}|\n"
    "    i64{} view = nums{@}|\n"
    "    print(sum(total, nums{0}))|\n"
    "    $AGAIN :|\n"
    "    i64 where = $AGAIN|\n"
    "    if (total 0 <) [ where goto| ]|\n"
    "    total return|\n"
    "]|\n";

int main(void) {
    const bool color = isatty(STDOUT_FILENO);
    const size_t source_bytes = strlen(source);
    size_t lines = 0;
    for (const char *p = source; *p; p++) { lines += *p == '\n'; }

    arena_init(&ast_arena);

    lexer_init(source, "theo");
    size_t tokens = 0;
    while (lexer_next_token().type != TOKEN_EOF) { tokens++; }

    // The whole tree
    StringPool pool;
    string_pool_init(&pool);
    parser_init(&pool);
    lexer_init(source, "theo");
    size_t top_level = 0, nodes = 0;
    printf("%s\n", "─── the AST ─────────────────────────────────────────────────────────");
    for (const ASTNode *stmt = parse_next_statement(); stmt; stmt = parse_next_statement()) {
        nodes += ast_print(&pool, stmt, color);
        top_level++;
    }

    // Stats
    size_t arena_bytes = 0;
    for (const ArenaPage *page = ast_arena.page_head; page; page = page->next) { arena_bytes += page->alloc_position; }
    printf("\n%s\n", "─── numbers ─────────────────────────────────────────────────────────");
    printf("source        %zu bytes, %zu lines, %zu tokens\n", source_bytes, lines, tokens);
    printf("parsed into   %zu top-level statements, %zu AST nodes\n", top_level, nodes);
    printf("names         %u unique strings interned in the string pool\n", pool.handle_count);
    printf("arena         %zu bytes used in total (the tree plus the parser's working stacks)\n", arena_bytes);

    string_pool_destroy(&pool);
    arena_destroy(&ast_arena);
    return 0;
}