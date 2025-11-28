#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_PROMPT "... "
#define ZERO_EPS 1e-12

typedef enum {
    TK_EOF = 0,
    TK_NUMBER,
    TK_IDENT,
    TK_STRING,
    TK_WRITE,
    TK_READ,
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
    TK_COMMA,
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
    char *lexeme; /* identifier or string literal */
    int line;
} Token;

typedef enum {
    AST_NUMBER,
    AST_VAR,
    AST_STRING,
    AST_BINOP,
    AST_ASSIGN,
    AST_WRITE,
    AST_READ,
    AST_IF,
    AST_WHILE,
    AST_BLOCK
} ASTType;

typedef struct AST AST;

struct AST {
    ASTType type;
    int line;
    double number;
    char *name;            /* identifiers for VAR/ASSIGN/READ */
    char *string_literal;  /* for AST_STRING */
    TokenType op;          /* for AST_BINOP */
    AST *left;
    AST *right;
    AST **stmts;
    size_t stmt_count;
    size_t stmt_cap;
    AST **args;
    size_t arg_count;
    size_t arg_cap;
};

typedef enum {
    VAL_NUMBER,
    VAL_STRING
} ValueType;

typedef struct {
    ValueType type;
    double number;
    char *string;
} Value;

typedef struct Var Var;

struct Var {
    char *name;
    Value value;
    Var *next;
};

static const char *source = NULL;
static size_t source_len = 0;
static size_t source_pos = 0;
static int current_line = 1;
static Token current_token = {0};
static Var *var_list = NULL;

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

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return ptr;
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

static char *xstrdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, src, len);
    return copy;
}

static void free_token(Token *tok)
{
    if (tok->lexeme) {
        free(tok->lexeme);
        tok->lexeme = NULL;
    }
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

static bool ident_equals(const char *text, const char *keyword)
{
    for (; *text && *keyword; ++text, ++keyword) {
        if ((char)tolower((unsigned char)*text) != *keyword) {
            return false;
        }
    }
    return *text == '\0' && *keyword == '\0';
}

static char decode_escape(char esc, int line)
{
    switch (esc) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    default:
        fail(line, "Unknown escape sequence \\%c", esc);
    }
}

static char *parse_string_literal(void)
{
    int line = current_line;
    size_t cap = 16;
    size_t len = 0;
    char *buffer = xmalloc(cap);
    source_pos++; /* skip opening quote */
    while (source_pos < source_len) {
        char c = source[source_pos++];
        if (c == '"') {
            buffer[len] = '\0';
            return buffer;
        }
        if (c == '\\') {
            if (source_pos >= source_len) {
                fail(line, "Unfinished escape in string literal");
            }
            c = decode_escape(source[source_pos++], line);
        } else if (c == '\n') {
            fail(line, "Newline in string literal");
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buffer = xrealloc(buffer, cap);
        }
        buffer[len++] = c;
    }
    fail(line, "Unterminated string literal");
}

static char *copy_range(size_t start, size_t len)
{
    char *text = xmalloc(len + 1);
    memcpy(text, source + start, len);
    text[len] = '\0';
    return text;
}

static void next_token(void)
{
    free_token(&current_token);
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

    if (c == '"') {
        current_token.type = TK_STRING;
        current_token.lexeme = parse_string_literal();
        return;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = source_pos;
        source_pos++;
        while (source_pos < source_len) {
            char ch = source[source_pos];
            if (isalnum((unsigned char)ch) || ch == '_') {
                source_pos++;
            } else {
                break;
            }
        }
        size_t len = source_pos - start;
        char *text = copy_range(start, len);

        if (ident_equals(text, "write")) {
            current_token.type = TK_WRITE;
            free(text);
            return;
        }
        if (ident_equals(text, "read")) {
            current_token.type = TK_READ;
            free(text);
            return;
        }
        if (ident_equals(text, "if")) {
            current_token.type = TK_IF;
            free(text);
            return;
        }
        if (ident_equals(text, "while")) {
            current_token.type = TK_WHILE;
            free(text);
            return;
        }

        current_token.type = TK_IDENT;
        current_token.lexeme = text;
        return;
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
    case ',':
        current_token.type = TK_COMMA;
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

static void write_append(AST *node, AST *arg)
{
    if (!node || node->type != AST_WRITE) {
        return;
    }
    if (node->arg_count == node->arg_cap) {
        size_t new_cap = node->arg_cap ? node->arg_cap * 2 : 4;
        node->args = xrealloc(node->args, new_cap * sizeof(AST *));
        node->arg_cap = new_cap;
    }
    node->args[node->arg_count++] = arg;
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
    if (current_token.type == TK_STRING) {
        AST *node = new_ast(AST_STRING, current_token.line);
        node->string_literal = xstrdup(current_token.lexeme);
        next_token();
        return node;
    }
    if (current_token.type == TK_IDENT) {
        AST *node = new_ast(AST_VAR, current_token.line);
        node->name = xstrdup(current_token.lexeme);
        next_token();
        return node;
    }
    if (current_token.type == TK_LPAREN) {
        next_token();
        AST *expr = parse_expression();
        consume(TK_RPAREN, "Expected ')' after expression");
        return expr;
    }
    fail(current_token.line, "Expected number, string, variable, or '('");
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

static AST *parse_write_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after write");
    AST *node = new_ast(AST_WRITE, line);
    if (current_token.type != TK_RPAREN) {
        while (1) {
            AST *expr = parse_expression();
            write_append(node, expr);
            if (current_token.type == TK_COMMA) {
                next_token();
                continue;
            }
            break;
        }
    }
    consume(TK_RPAREN, "Expected ')' after write arguments");
    consume(TK_SEMI, "Missing ';' after write");
    return node;
}

static AST *parse_read_statement(void)
{
    int line = current_token.line;
    next_token();
    consume(TK_LPAREN, "Expected '(' after read");
    if (current_token.type != TK_IDENT) {
        fail(current_token.line, "read() expects an identifier");
    }
    char *name = xstrdup(current_token.lexeme);
    next_token();
    consume(TK_RPAREN, "Expected ')' after read variable");
    consume(TK_SEMI, "Missing ';' after read");
    AST *node = new_ast(AST_READ, line);
    node->name = name;
    return node;
}

static AST *parse_assignment(void)
{
    if (current_token.type != TK_IDENT) {
        fail(current_token.line, "Expected variable name");
    }
    char *name = xstrdup(current_token.lexeme);
    int line = current_token.line;
    next_token();
    consume(TK_EQUALS, "Expected '=' in assignment");
    AST *expr = parse_expression();
    consume(TK_SEMI, "Missing ';' after assignment");
    AST *node = new_ast(AST_ASSIGN, line);
    node->name = name;
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
    case TK_WRITE:
        return parse_write_statement();
    case TK_READ:
        return parse_read_statement();
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

static Value value_number(double number)
{
    Value v;
    v.type = VAL_NUMBER;
    v.number = number;
    v.string = NULL;
    return v;
}

static Value value_string_copy(const char *text)
{
    Value v;
    v.type = VAL_STRING;
    v.number = 0.0;
    v.string = xstrdup(text ? text : "");
    return v;
}

static Value value_string_owned(char *text)
{
    Value v;
    v.type = VAL_STRING;
    v.number = 0.0;
    v.string = text ? text : xstrdup("");
    return v;
}

static void value_free(Value *v)
{
    if (!v) {
        return;
    }
    if (v->type == VAL_STRING && v->string) {
        free(v->string);
        v->string = NULL;
    }
    v->type = VAL_NUMBER;
    v->number = 0.0;
}

static Value value_clone(const Value *src)
{
    if (!src) {
        return value_number(0.0);
    }
    if (src->type == VAL_STRING) {
        return value_string_copy(src->string);
    }
    return value_number(src->number);
}

static double truncate_to_thousandths(double value)
{
    double scaled = trunc(value * 1000.0);
    return scaled / 1000.0;
}

static char *format_number(double value)
{
    double truncated = truncate_to_thousandths(value);
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "%.3f", truncated);
    if (written < 0) {
        fail(0, "Failed to format number");
    }
    char *out = xmalloc((size_t)written + 1);
    memcpy(out, buffer, (size_t)written + 1);
    return out;
}

static double value_expect_number(const Value *v, int line)
{
    if (!v || v->type != VAL_NUMBER) {
        fail(line, "Expected numeric value");
    }
    return v->number;
}

static bool value_is_truthy(const Value *v, int line)
{
    double number = value_expect_number(v, line);
    if (isnan(number)) {
        return false;
    }
    return number != 0.0;
}

static char *value_to_cstring(const Value *v)
{
    if (!v) {
        return xstrdup("");
    }
    if (v->type == VAL_STRING) {
        return xstrdup(v->string ? v->string : "");
    }
    return format_number(v->number);
}

static Var *find_variable(const char *name)
{
    for (Var *v = var_list; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return NULL;
}

static Var *ensure_variable(const char *name)
{
    Var *var = find_variable(name);
    if (var) {
        return var;
    }
    var = xmalloc(sizeof(Var));
    var->name = xstrdup(name);
    var->value = value_number(0.0);
    var->next = var_list;
    var_list = var;
    return var;
}

static Value variable_read(const char *name)
{
    Var *var = ensure_variable(name);
    return value_clone(&var->value);
}

static void variable_write(const char *name, const Value *value)
{
    Var *var = ensure_variable(name);
    value_free(&var->value);
    var->value = value_clone(value);
}

static double read_number_from_stdin(int line)
{
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), stdin)) {
        fail(line, "read() failed to read data");
    }
    char *end = NULL;
    errno = 0;
    double value = strtod(buffer, &end);
    if (errno == ERANGE) {
        fail(line, "read() value out of range");
    }
    if (end == buffer) {
        fail(line, "read() expects numeric text");
    }
    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end != '\0') {
        fail(line, "read() expects numeric text");
    }
    return value;
}

static Value eval_expr(AST *node);

static Value eval_binop(AST *node, Value left, Value right)
{
    switch (node->op) {
    case TK_PLUS:
        if (left.type == VAL_STRING || right.type == VAL_STRING) {
            char *ls = value_to_cstring(&left);
            char *rs = value_to_cstring(&right);
            size_t lhs_len = strlen(ls);
            size_t rhs_len = strlen(rs);
            char *joined = xmalloc(lhs_len + rhs_len + 1);
            memcpy(joined, ls, lhs_len);
            memcpy(joined + lhs_len, rs, rhs_len + 1);
            free(ls);
            free(rs);
            return value_string_owned(joined);
        }
        return value_number(value_expect_number(&left, node->line) +
                             value_expect_number(&right, node->line));
    case TK_MINUS:
        return value_number(value_expect_number(&left, node->line) -
                             value_expect_number(&right, node->line));
    case TK_STAR:
        return value_number(value_expect_number(&left, node->line) *
                             value_expect_number(&right, node->line));
    case TK_SLASH: {
        double divisor = value_expect_number(&right, node->line);
        if (fabs(divisor) < ZERO_EPS) {
            fail(node->line, "Gravity called: divide-by-zero is forbidden");
        }
        return value_number(value_expect_number(&left, node->line) / divisor);
    }
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE: {
        if (left.type == VAL_STRING && right.type == VAL_STRING) {
            int cmp = strcmp(left.string ? left.string : "", right.string ? right.string : "");
            bool result = false;
            switch (node->op) {
            case TK_LT:
                result = cmp < 0;
                break;
            case TK_LE:
                result = cmp <= 0;
                break;
            case TK_GT:
                result = cmp > 0;
                break;
            case TK_GE:
                result = cmp >= 0;
                break;
            default:
                break;
            }
            return value_number(result ? 1.0 : 0.0);
        }
        double a = value_expect_number(&left, node->line);
        double b = value_expect_number(&right, node->line);
        bool result = false;
        switch (node->op) {
        case TK_LT:
            result = a < b;
            break;
        case TK_LE:
            result = a <= b;
            break;
        case TK_GT:
            result = a > b;
            break;
        case TK_GE:
            result = a >= b;
            break;
        default:
            break;
        }
        return value_number(result ? 1.0 : 0.0);
    }
    case TK_EQEQ:
    case TK_NEQ: {
        bool equal = false;
        if (left.type == VAL_STRING && right.type == VAL_STRING) {
            const char *ls = left.string ? left.string : "";
            const char *rs = right.string ? right.string : "";
            equal = (strcmp(ls, rs) == 0);
        } else if (left.type == VAL_NUMBER && right.type == VAL_NUMBER) {
            double a = value_expect_number(&left, node->line);
            double b = value_expect_number(&right, node->line);
            equal = (a == b);
        } else {
            fail(node->line, "Cannot compare strings and numbers");
        }
        bool result = (node->op == TK_EQEQ) ? equal : !equal;
        return value_number(result ? 1.0 : 0.0);
    }
    default:
        fail(node->line, "Invalid binary operator");
    }
}

static Value eval_expr(AST *node)
{
    if (!node) {
        fail(0, "Null expression node");
    }
    switch (node->type) {
    case AST_NUMBER:
        return value_number(node->number);
    case AST_STRING:
        return value_string_copy(node->string_literal);
    case AST_VAR:
        return variable_read(node->name);
    case AST_BINOP: {
        Value left = eval_expr(node->left);
        Value right = eval_expr(node->right);
        Value result = eval_binop(node, left, right);
        value_free(&left);
        value_free(&right);
        return result;
    }
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
    case AST_WRITE:
        for (size_t i = 0; i < node->arg_count; ++i) {
            Value value = eval_expr(node->args[i]);
            if (value.type == VAL_STRING) {
                fputs(value.string ? value.string : "", stdout);
            } else {
                double truncated = truncate_to_thousandths(value.number);
                printf("%.3f", truncated);
            }
            value_free(&value);
        }
        printf("\n");
        break;
    case AST_READ: {
        fputs(READ_PROMPT, stdout);
        fflush(stdout);
        double number = read_number_from_stdin(node->line);
        Value v = value_number(number);
        variable_write(node->name, &v);
        break;
    }
    case AST_ASSIGN: {
        Value value = eval_expr(node->left);
        variable_write(node->name, &value);
        value_free(&value);
        break;
    }
    case AST_BLOCK:
        for (size_t i = 0; i < node->stmt_count; ++i) {
            exec_stmt(node->stmts[i]);
        }
        break;
    case AST_IF: {
        Value cond = eval_expr(node->left);
        if (value_is_truthy(&cond, node->line)) {
            exec_stmt(node->right);
        }
        value_free(&cond);
        break;
    }
    case AST_WHILE:
        while (1) {
            Value cond = eval_expr(node->left);
            bool truthy = value_is_truthy(&cond, node->line);
            value_free(&cond);
            if (!truthy) {
                break;
            }
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
    case AST_WRITE:
        for (size_t i = 0; i < node->arg_count; ++i) {
            free_ast(node->args[i]);
        }
        free(node->args);
        break;
    case AST_ASSIGN:
        free(node->name);
        free_ast(node->left);
        break;
    case AST_READ:
        free(node->name);
        break;
    case AST_VAR:
        free(node->name);
        break;
    case AST_STRING:
        free(node->string_literal);
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
    case AST_NUMBER:
        break;
    }
    free(node);
}

static void free_variables(void)
{
    Var *var = var_list;
    while (var) {
        Var *next = var->next;
        free(var->name);
        value_free(&var->value);
        free(var);
        var = next;
    }
    var_list = NULL;
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
    char *buffer = xmalloc((size_t)length + 1);
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
    memset(&current_token, 0, sizeof(current_token));

    next_token();
    AST *program = parse_program();
    exec_stmt(program);

    free_ast(program);
    free_token(&current_token);
    free(buffer);
    free_variables();
    return EXIT_SUCCESS;
}
