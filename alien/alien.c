#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAR_COUNT 26
#define INPUT_PROMPT "... "
#define ZERO_EPS 1e-12

typedef enum {
    TK_EOF = 0,
    TK_NUMBER,
    TK_IDENT,
    TK_PRINT,
    TK_INPUT,
    TK_IF,
    TK_WHILE,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_EQUALS,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_SEMI,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,
    TK_EQEQ,
    TK_NEQ
} TokenType;

typedef struct {
    TokenType type;
    double number;
    char ident;
    int line;
} Token;

typedef enum {
    AST_NUMBER,
    AST_VAR,
    AST_BINOP,
    AST_ASSIGN,
    AST_PRINT,
    AST_INPUT,
    AST_IF,
    AST_WHILE,
    AST_BLOCK
} ASTType;

typedef struct AST AST;

struct AST {
    ASTType type;
    int line;
    double number;
    char var_name;
    TokenType op; /* for binary expressions */
    AST *left;
    AST *right;
    AST **stmts;
    size_t stmt_count;
    size_t stmt_cap;
};

static const char *source = NULL;
static size_t source_len = 0;
static size_t source_pos = 0;
static int current_line = 1;
static Token current_token;

static double variables[VAR_COUNT];

static void fail(int line, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)))
    __attribute__((noreturn));

static void fail(int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error (line %d): ", line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(EXIT_FAILURE);
}

static void *xrealloc(void *ptr, size_t size)
{
    void *res = realloc(ptr, size);
    if (!res) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    return res;
}

static void skip_whitespace(void)
{
    while (source_pos < source_len) {
        char c = source[source_pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            source_pos++;
        } else if (c == '\n') {
            current_line++;
            source_pos++;
        } else if (c == '/' && source_pos + 1 < source_len && source[source_pos + 1] == '/') {
            source_pos += 2;
            while (source_pos < source_len && source[source_pos] != '\n') {
                source_pos++;
            }
        } else {
            break;
        }
    }
}

static void next_token(void)
{
    skip_whitespace();
    current_token.line = current_line;

    if (source_pos >= source_len) {
        current_token.type = TK_EOF;
        return;
    }

    char c = source[source_pos];

    if (isdigit((unsigned char)c) || (c == '.' && source_pos + 1 < source_len && isdigit((unsigned char)source[source_pos + 1]))) {
        const char *start = source + source_pos;
        char *end = NULL;
        errno = 0;
        double value = strtod(start, &end);
        if (errno == ERANGE) {
            fail(current_line, "Numeric literal out of range");
        }
        size_t consumed = (size_t)(end - start);
        source_pos += consumed;
        current_token.type = TK_NUMBER;
        current_token.number = value;
        return;
    }

    if (isalpha((unsigned char)c)) {
        size_t start = source_pos;
        while (source_pos < source_len && isalpha((unsigned char)source[source_pos])) {
            source_pos++;
        }
        size_t len = source_pos - start;
        if (len >= 32) {
            fail(current_line, "Identifier too long");
        }
        char buf[32];
        for (size_t i = 0; i < len; ++i) {
            buf[i] = (char)tolower((unsigned char)source[start + i]);
        }
        buf[len] = '\0';

        if (strcmp(buf, "print") == 0) {
            current_token.type = TK_PRINT;
            return;
        }
        if (strcmp(buf, "input") == 0) {
            current_token.type = TK_INPUT;
            return;
        }
        if (strcmp(buf, "if") == 0) {
            current_token.type = TK_IF;
            return;
        }
        if (strcmp(buf, "while") == 0) {
            current_token.type = TK_WHILE;
            return;
        }

        if (len == 1) {
            char letter = (char)toupper((unsigned char)source[start]);
            if (letter >= 'A' && letter <= 'Z') {
                current_token.type = TK_IDENT;
                current_token.ident = letter;
                return;
            }
        }

        fail(current_line, "Invalid identifier. Use single letters A..Z.");
    }

    source_pos++;
    switch (c) {
    case '+':
        current_token.type = TK_PLUS;
        return;
    case '-':
        current_token.type = TK_MINUS;
        return;
    case '*':
        current_token.type = TK_STAR;
        return;
    case '/':
        current_token.type = TK_SLASH;
        return;
    case '=':
        if (source_pos < source_len && source[source_pos] == '=') {
            source_pos++;
            current_token.type = TK_EQEQ;
        } else {
            current_token.type = TK_EQUALS;
        }
        return;
    case '(':
        current_token.type = TK_LPAREN;
        return;
    case ')':
        current_token.type = TK_RPAREN;
        return;
    case '{':
        current_token.type = TK_LBRACE;
        return;
    case '}':
        current_token.type = TK_RBRACE;
        return;
    case ';':
        current_token.type = TK_SEMI;
        return;
    case '<':
        if (source_pos < source_len && source[source_pos] == '=') {
            source_pos++;
            current_token.type = TK_LE;
        } else {
            current_token.type = TK_LT;
        }
        return;
    case '>':
        if (source_pos < source_len && source[source_pos] == '=') {
            source_pos++;
            current_token.type = TK_GE;
        } else {
            current_token.type = TK_GT;
        }
        return;
    case '!':
        if (source_pos < source_len && source[source_pos] == '=') {
            source_pos++;
            current_token.type = TK_NEQ;
            return;
        }
        fail(current_line, "Unexpected '!'");
    default:
        fail(current_line, "Unexpected character '%c'", c);
    }
}

static void consume(TokenType type, const char *message)
{
    if (current_token.type != type) {
        fail(current_token.line, "%s", message);
    }
    next_token();
}

static AST *new_ast(ASTType type, int line)
{
    AST *node = calloc(1, sizeof(AST));
    if (!node) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    node->type = type;
    node->line = line;
    return node;
}

static void block_append(AST *block, AST *stmt)
{
    if (!block || block->type != AST_BLOCK) {
        return;
    }
    if (block->stmt_count == block->stmt_cap) {
        size_t new_cap = block->stmt_cap ? block->stmt_cap * 2 : 4;
        block->stmts = xrealloc(block->stmts, new_cap * sizeof(AST *));
        block->stmt_cap = new_cap;
    }
    block->stmts[block->stmt_count++] = stmt;
}

static AST *parse_expression(void);

static AST *parse_primary(void)
{
    if (current_token.type == TK_NUMBER) {
        AST *node = new_ast(AST_NUMBER, current_token.line);
        node->number = current_token.number;
        next_token();
        return node;
    }
    if (current_token.type == TK_IDENT) {
        AST *node = new_ast(AST_VAR, current_token.line);
        node->var_name = current_token.ident;
        next_token();
        return node;
    }
    if (current_token.type == TK_LPAREN) {
        next_token();
        AST *expr = parse_expression();
        consume(TK_RPAREN, "Expected ')' after expression");
        return expr;
    }
    fail(current_token.line, "Expected number, variable, or '('");
    return NULL;
}

static AST *make_unary(TokenType op, AST *operand, int line)
{
    AST *zero = new_ast(AST_NUMBER, line);
    zero->number = 0.0;
    AST *node = new_ast(AST_BINOP, line);
    node->op = op;
    node->left = zero;
    node->right = operand;
    return node;
}

static AST *parse_unary(void)
{
    if (current_token.type == TK_MINUS) {
        int line = current_token.line;
        next_token();
        return make_unary(TK_MINUS, parse_unary(), line);
    }
    if (current_token.type == TK_PLUS) {
        next_token();
        return parse_unary();
    }
    return parse_primary();
}

static AST *parse_factor(void)
{
    AST *node = parse_unary();
    while (current_token.type == TK_STAR || current_token.type == TK_SLASH) {
        TokenType op = current_token.type;
        int line = current_token.line;
        next_token();
        AST *rhs = parse_unary();
        AST *bin = new_ast(AST_BINOP, line);
        bin->op = op;
        bin->left = node;
        bin->right = rhs;
        node = bin;
    }
    return node;
}

static AST *parse_term(void)
{
    AST *node = parse_factor();
    while (current_token.type == TK_PLUS || current_token.type == TK_MINUS) {
        TokenType op = current_token.type;
        int line = current_token.line;
        next_token();
        AST *rhs = parse_factor();
        AST *bin = new_ast(AST_BINOP, line);
        bin->op = op;
        bin->left = node;
        bin->right = rhs;
        node = bin;
    }
    return node;
}

static AST *parse_comparison(void)
{
    AST *node = parse_term();
    while (current_token.type == TK_LT || current_token.type == TK_GT ||
           current_token.type == TK_LE || current_token.type == TK_GE) {
        TokenType op = current_token.type;
        int line = current_token.line;
        next_token();
        AST *rhs = parse_term();
        AST *bin = new_ast(AST_BINOP, line);
        bin->op = op;
        bin->left = node;
        bin->right = rhs;
        node = bin;
    }
    return node;
}

static AST *parse_equality(void)
{
    AST *node = parse_comparison();
    while (current_token.type == TK_EQEQ || current_token.type == TK_NEQ) {
        TokenType op = current_token.type;
        int line = current_token.line;
        next_token();
        AST *rhs = parse_comparison();
        AST *bin = new_ast(AST_BINOP, line);
        bin->op = op;
        bin->left = node;
        bin->right = rhs;
        node = bin;
    }
    return node;
}

static AST *parse_expression(void)
{
    return parse_equality();
}

static AST *parse_block(void);
static AST *parse_statement(void);

static AST *parse_print_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after print");
    AST *expr = parse_expression();
    consume(TK_RPAREN, "Expected ')' after print expression");
    consume(TK_SEMI, "Missing ';' after print");
    AST *node = new_ast(AST_PRINT, line);
    node->left = expr;
    return node;
}

static AST *parse_input_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after input");
    if (current_token.type != TK_IDENT) {
        fail(current_token.line, "input() expects a single-letter variable");
    }
    char var_name = current_token.ident;
    next_token();
    consume(TK_RPAREN, "Expected ')' after input variable");
    consume(TK_SEMI, "Missing ';' after input");
    AST *node = new_ast(AST_INPUT, line);
    node->var_name = var_name;
    return node;
}

static AST *parse_assignment(void)
{
    if (current_token.type != TK_IDENT) {
        fail(current_token.line, "Expected variable name");
    }
    char var_name = current_token.ident;
    int line = current_token.line;
    next_token();
    consume(TK_EQUALS, "Expected '=' in assignment");
    AST *expr = parse_expression();
    consume(TK_SEMI, "Missing ';' after assignment");
    AST *node = new_ast(AST_ASSIGN, line);
    node->var_name = var_name;
    node->left = expr;
    return node;
}

static AST *parse_if_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after if");
    AST *cond = parse_expression();
    consume(TK_RPAREN, "Expected ')' after if condition");
    if (current_token.type != TK_LBRACE) {
        fail(current_token.line, "if requires a block starting with '{'");
    }
    AST *body = parse_block();
    AST *node = new_ast(AST_IF, line);
    node->left = cond;
    node->right = body;
    return node;
}

static AST *parse_while_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after while");
    AST *cond = parse_expression();
    consume(TK_RPAREN, "Expected ')' after while condition");
    if (current_token.type != TK_LBRACE) {
        fail(current_token.line, "while requires a block starting with '{'");
    }
    AST *body = parse_block();
    AST *node = new_ast(AST_WHILE, line);
    node->left = cond;
    node->right = body;
    return node;
}

static AST *parse_block(void)
{
    int line = current_token.line;
    consume(TK_LBRACE, "Expected '{'");
    AST *block = new_ast(AST_BLOCK, line);
    while (current_token.type != TK_RBRACE) {
        if (current_token.type == TK_EOF) {
            fail(current_token.line, "Unterminated block. Missing '}'.");
        }
        AST *stmt = parse_statement();
        block_append(block, stmt);
    }
    consume(TK_RBRACE, "Expected '}' to close block");
    return block;
}

static AST *parse_statement(void)
{
    switch (current_token.type) {
    case TK_PRINT:
        return parse_print_statement();
    case TK_INPUT:
        return parse_input_statement();
    case TK_IF:
        return parse_if_statement();
    case TK_WHILE:
        return parse_while_statement();
    case TK_LBRACE:
        return parse_block();
    case TK_IDENT:
        return parse_assignment();
    default:
        fail(current_token.line, "Unexpected token in statement");
    }
}

static AST *parse_program(void)
{
    AST *root = new_ast(AST_BLOCK, 1);
    while (current_token.type != TK_EOF) {
        AST *stmt = parse_statement();
        block_append(root, stmt);
    }
    return root;
}

static int var_index(char name, int line)
{
    if (name < 'A' || name > 'Z') {
        fail(line, "Invalid variable '%c'", name);
    }
    return name - 'A';
}

static bool is_truthy(double value)
{
    if (isnan(value)) {
        return false;
    }
    return value != 0.0;
}

static double truncate_to_thousandths(double value)
{
    double scaled = trunc(value * 1000.0);
    return scaled / 1000.0;
}

static double read_number_from_stdin(int line)
{
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), stdin)) {
        fail(line, "input() failed to read data");
    }
    char *end = NULL;
    errno = 0;
    double value = strtod(buffer, &end);
    if (errno == ERANGE) {
        fail(line, "input() value out of range");
    }
    if (end == buffer) {
        fail(line, "input() expects numeric text");
    }
    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end != '\0') {
        fail(line, "input() expects numeric text");
    }
    return value;
}

static double eval_expr(AST *node);

static double eval_binop(AST *node, double lhs, double rhs)
{
    switch (node->op) {
    case TK_PLUS:
        return lhs + rhs;
    case TK_MINUS:
        return lhs - rhs;
    case TK_STAR:
        return lhs * rhs;
    case TK_SLASH:
        if (fabs(rhs) < ZERO_EPS) {
            fail(node->line, "Gravity called: divide-by-zero is forbidden");
        }
        return lhs / rhs;
    case TK_LT:
        return lhs < rhs ? 1.0 : 0.0;
    case TK_GT:
        return lhs > rhs ? 1.0 : 0.0;
    case TK_LE:
        return lhs <= rhs ? 1.0 : 0.0;
    case TK_GE:
        return lhs >= rhs ? 1.0 : 0.0;
    case TK_EQEQ:
        return lhs == rhs ? 1.0 : 0.0;
    case TK_NEQ:
        return lhs != rhs ? 1.0 : 0.0;
    default:
        fail(node->line, "Invalid binary operator");
    }
}

static double eval_expr(AST *node)
{
    if (!node) {
        fail(0, "Null expression node");
    }
    switch (node->type) {
    case AST_NUMBER:
        return node->number;
    case AST_VAR:
        return variables[var_index(node->var_name, node->line)];
    case AST_BINOP:
        return eval_binop(node, eval_expr(node->left), eval_expr(node->right));
    default:
        fail(node->line, "Unsupported expression");
    }
}

static void exec_stmt(AST *node)
{
    if (!node) {
        return;
    }
    switch (node->type) {
    case AST_PRINT: {
        double value = eval_expr(node->left);
        double truncated = truncate_to_thousandths(value);
        printf("%.3f\n", truncated);
        break;
    }
    case AST_INPUT: {
        fputs(INPUT_PROMPT, stdout);
        fflush(stdout);
        double value = read_number_from_stdin(node->line);
        variables[var_index(node->var_name, node->line)] = value;
        break;
    }
    case AST_ASSIGN: {
        double value = eval_expr(node->left);
        variables[var_index(node->var_name, node->line)] = value;
        break;
    }
    case AST_BLOCK:
        for (size_t i = 0; i < node->stmt_count; ++i) {
            exec_stmt(node->stmts[i]);
        }
        break;
    case AST_IF:
        if (is_truthy(eval_expr(node->left))) {
            exec_stmt(node->right);
        }
        break;
    case AST_WHILE:
        while (is_truthy(eval_expr(node->left))) {
            exec_stmt(node->right);
        }
        break;
    default:
        fail(node->line, "Unknown statement type");
    }
}

static void free_ast(AST *node)
{
    if (!node) {
        return;
    }
    switch (node->type) {
    case AST_PRINT:
    case AST_ASSIGN:
        free_ast(node->left);
        break;
    case AST_INPUT:
    case AST_VAR:
    case AST_NUMBER:
        break;
    case AST_BINOP:
        free_ast(node->left);
        free_ast(node->right);
        break;
    case AST_IF:
    case AST_WHILE:
        free_ast(node->left);
        free_ast(node->right);
        break;
    case AST_BLOCK:
        for (size_t i = 0; i < node->stmt_count; ++i) {
            free_ast(node->stmts[i]);
        }
        free(node->stmts);
        break;
    }
    free(node);
}

static char *read_file(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        exit(EXIT_FAILURE);
    }
    long length = ftell(fp);
    if (length < 0) {
        perror("ftell");
        exit(EXIT_FAILURE);
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("fseek");
        exit(EXIT_FAILURE);
    }
    char *buffer = malloc((size_t)length + 1);
    if (!buffer) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    size_t read_bytes = fread(buffer, 1, (size_t)length, fp);
    if (read_bytes != (size_t)length) {
        perror("fread");
        exit(EXIT_FAILURE);
    }
    buffer[length] = '\0';
    fclose(fp);
    if (out_size) {
        *out_size = (size_t)length;
    }
    return buffer;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <script.aln>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t file_size = 0;
    char *buffer = read_file(argv[1], &file_size);
    source = buffer;
    source_len = file_size;
    source_pos = 0;
    current_line = 1;
    memset(variables, 0, sizeof(variables));

    next_token();
    AST *program = parse_program();
    exec_stmt(program);

    free_ast(program);
    free(buffer);
    return EXIT_SUCCESS;
}
