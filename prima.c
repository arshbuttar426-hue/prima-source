/*
 * prima — self-contained C interpreter for the As/NotAs constraint language.
 *
 * Copyright (C) 2026 Arshbuttar.
 * Licensed under the GNU Affero General Public License v3.
 * See LICENSE for the full license text.
 *
 * Build:   cc -o prima prima.c
 * Usage:   ./prima test.prima
 *
 * Three primitives:
 *   as X        — positive constraint: value counts as X
 *   not_as Y    — negative constraint: value absolutely does NOT count as Y
 *   fn[n]       — categorical computation, selected by index from a function space
 *
 * v0.2.0 — Runtime expansion:
 *   I/O         — read_file, write_file, file_exists, capture, env
 *   Regex       — regex_match, regex_extract, regex_replace
 *   Pipe comp   — -> pipe other_pipe(args)
 *   Iteration   — for item in list { ... }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <setjmp.h>
#include <math.h>
#include <regex.h>
#include <errno.h>
#include <sys/stat.h>

/* ===== VERSION ===== */
#define PRIMA_VERSION "0.2.0"

/* ===== VALUE ===== */

typedef enum { VAL_NIL, VAL_INT, VAL_FLOAT, VAL_STRING, VAL_LIST } ValType;

typedef struct ValueList {
    struct Value *items;
    int count;
    int cap;
} ValueList;

typedef struct Value {
    ValType type;
    union {
        long ival;
        double fval;
        char *sval;
        ValueList *lval;
    } as;
} Value;

Value val_nil(void) { Value v = { .type = VAL_NIL }; return v; }
Value val_int(long i) { Value v = { VAL_INT, .as.ival = i }; return v; }
Value val_float(double f) { Value v = { VAL_FLOAT, .as.fval = f }; return v; }
Value val_str(const char *s) { Value v = { VAL_STRING, .as.sval = strdup(s) }; return v; }
Value val_str_nocopy(char *s) { Value v = { VAL_STRING, .as.sval = s }; return v; }
Value val_list_new(void) {
    Value v = { VAL_LIST };
    v.as.lval = malloc(sizeof(ValueList));
    v.as.lval->cap = 8;
    v.as.lval->count = 0;
    v.as.lval->items = malloc((size_t)v.as.lval->cap * sizeof(Value));
    return v;
}
void val_list_append(Value *list, Value item) {
    if (!list || list->type != VAL_LIST) return;
    ValueList *lv = list->as.lval;
    if (lv->count >= lv->cap) { lv->cap *= 2; lv->items = realloc(lv->items, (size_t)lv->cap * sizeof(Value)); }
    lv->items[lv->count++] = item;
}
Value val_list_get(Value list, int idx) {
    if (list.type != VAL_LIST || idx < 0 || idx >= list.as.lval->count) return val_nil();
    return list.as.lval->items[idx];
}
int val_list_len(Value list) {
    if (list.type != VAL_LIST) return 0;
    return list.as.lval->count;
}

void val_free(Value v) {
    if (v.type == VAL_LIST && v.as.lval) {
        for (int i = 0; i < v.as.lval->count; i++)
            val_free(v.as.lval->items[i]);
        free(v.as.lval->items);
        free(v.as.lval);
    }
    if (v.type == VAL_STRING && v.as.sval) free(v.as.sval);
}

Value val_clone(Value v) {
    if (v.type == VAL_STRING && v.as.sval) return val_str(v.as.sval);
    if (v.type == VAL_LIST && v.as.lval) {
        Value clone = val_list_new();
        for (int i = 0; i < v.as.lval->count; i++)
            val_list_append(&clone, val_clone(v.as.lval->items[i]));
        return clone;
    }
    return v;
}

int val_to_int(Value v) {
    switch (v.type) {
        case VAL_INT: return (int)v.as.ival;
        case VAL_FLOAT: return (int)v.as.fval;
        case VAL_STRING: return atoi(v.as.sval);
        case VAL_LIST: return v.as.lval->count;
        default: return 0;
    }
}

double val_to_float(Value v) {
    switch (v.type) {
        case VAL_INT: return (double)v.as.ival;
        case VAL_FLOAT: return v.as.fval;
        case VAL_STRING: return atof(v.as.sval);
        default: return 0.0;
    }
}

const char *val_to_str(Value v) {
    static char buf[1024];
    switch (v.type) {
        case VAL_NIL: return "nil";
        case VAL_INT: snprintf(buf, sizeof(buf), "%ld", v.as.ival); return buf;
        case VAL_FLOAT: snprintf(buf, sizeof(buf), "%.17g", v.as.fval); return buf;
        case VAL_STRING: return v.as.sval ? v.as.sval : "nil";
        case VAL_LIST: {
            buf[0] = '\0';
            int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "[");
            for (int i = 0; i < v.as.lval->count && off < (int)sizeof(buf) - 4; i++) {
                if (i > 0) off += snprintf(buf + off, sizeof(buf) - off, ", ");
                const char *s = val_to_str(v.as.lval->items[i]);
                off += snprintf(buf + off, sizeof(buf) - off, "%s", s);
            }
            if (off < (int)sizeof(buf)) snprintf(buf + off, sizeof(buf) - off, "]");
            return buf;
        }
        default: return "?";
    }
}

void val_print(Value v) {
    printf("%s", val_to_str(v));
}

void val_fprint(FILE *fp, Value v) {
    fprintf(fp, "%s", val_to_str(v));
}

/* ===== ERROR HANDLING ===== */

jmp_buf error_jmp;
char error_msg[256];

void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_msg, sizeof(error_msg), fmt, args);
    va_end(args);
    longjmp(error_jmp, 1);
}

/* ===== TOKENIZER ===== */

typedef enum {
    TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_ARROW,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACK, TOK_RBRACK, TOK_SEMI, TOK_COLON, TOK_COMMA, TOK_EQ,
    TOK_EOF, TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    int line, col;
} Token;

typedef struct {
    char *source;
    int pos;
    int line, col;
    Token cur;
} Lexer;

Lexer *lex_new(const char *source) {
    Lexer *L = calloc(1, sizeof(Lexer));
    L->source = strdup(source);
    L->pos = 0;
    L->line = 1;
    L->col = 1;
    return L;
}

void lex_free(Lexer *L) {
    if (L->cur.text) free(L->cur.text);
    free(L->source);
    free(L);
}

static void lex_skip_ws(Lexer *L) {
    while (L->source[L->pos]) {
        char c = L->source[L->pos];
        if (c == '\n') { L->line++; L->col = 1; L->pos++; }
        else if (c == ' ' || c == '\t' || c == '\r') { L->col++; L->pos++; }
        else break;
    }
}

static void lex_skip_comment(Lexer *L) {
    if (L->source[L->pos] == '-' && L->source[L->pos+1] == '-') {
        while (L->source[L->pos] && L->source[L->pos] != '\n') {
            L->pos++; L->col++;
        }
    }
}

Token lex_next(Lexer *L) {
    Token tok = { TOK_ERROR, NULL, L->line, L->col };

    while (L->source[L->pos]) {
        lex_skip_ws(L);
        if (!L->source[L->pos]) break;

        if (L->source[L->pos] == '-' && L->source[L->pos+1] == '-') {
            lex_skip_comment(L);
            continue;
        }

        tok.line = L->line;
        tok.col = L->col;

        /* -> arrow */
        if (L->source[L->pos] == '-' && L->source[L->pos+1] == '>') {
            tok.type = TOK_ARROW;
            tok.text = strdup("->");
            L->pos += 2; L->col += 2;
            return tok;
        }

        /* punctuation */
        switch (L->source[L->pos]) {
            case '(': tok.type = TOK_LPAREN; tok.text = strdup("("); L->pos++; L->col++; return tok;
            case ')': tok.type = TOK_RPAREN; tok.text = strdup(")"); L->pos++; L->col++; return tok;
            case '{': tok.type = TOK_LBRACE; tok.text = strdup("{"); L->pos++; L->col++; return tok;
            case '}': tok.type = TOK_RBRACE; tok.text = strdup("}"); L->pos++; L->col++; return tok;
            case '[': tok.type = TOK_LBRACK; tok.text = strdup("["); L->pos++; L->col++; return tok;
            case ']': tok.type = TOK_RBRACK; tok.text = strdup("]"); L->pos++; L->col++; return tok;
            case ';': tok.type = TOK_SEMI; tok.text = strdup(";"); L->pos++; L->col++; return tok;
            case ':': tok.type = TOK_COLON; tok.text = strdup(":"); L->pos++; L->col++; return tok;
            case ',': tok.type = TOK_COMMA; tok.text = strdup(","); L->pos++; L->col++; return tok;
            case '=': tok.type = TOK_EQ; tok.text = strdup("="); L->pos++; L->col++; return tok;
        }

        /* number */
        if (isdigit(L->source[L->pos]) ||
            (L->source[L->pos] == '-' && isdigit(L->source[L->pos+1]))) {
            int start = L->pos;
            if (L->source[L->pos] == '-') { L->pos++; L->col++; }
            while (isdigit(L->source[L->pos])) { L->pos++; L->col++; }
            if (L->source[L->pos] == '.' && isdigit(L->source[L->pos+1])) {
                L->pos++; L->col++;
                while (isdigit(L->source[L->pos])) { L->pos++; L->col++; }
            }
            int len = L->pos - start;
            tok.type = TOK_NUMBER;
            tok.text = malloc((size_t)len + 1);
            memcpy(tok.text, L->source + start, len);
            tok.text[len] = '\0';
            return tok;
        }

        /* string */
        if (L->source[L->pos] == '"' || L->source[L->pos] == '\'') {
            char quote = L->source[L->pos];
            L->pos++; L->col++;
            int start = L->pos;
            while (L->source[L->pos] && L->source[L->pos] != quote) {
                L->pos++; L->col++;
            }
            int len = L->pos - start;
            tok.type = TOK_STRING;
            tok.text = malloc((size_t)len + 1);
            memcpy(tok.text, L->source + start, len);
            tok.text[len] = '\0';
            if (L->source[L->pos] == quote) { L->pos++; L->col++; }
            return tok;
        }

        /* identifier */
        if (isalpha(L->source[L->pos]) || L->source[L->pos] == '_') {
            int start = L->pos;
            while (isalnum(L->source[L->pos]) || L->source[L->pos] == '_' || L->source[L->pos] == '<' || L->source[L->pos] == '>') {
                /* CHAR<10 includes <, > for constraint names */
                L->pos++; L->col++;
            }
            /* stop if we hit characters that don't belong */
            while (isalnum(L->source[L->pos]) || L->source[L->pos] == '_') {
                L->pos++; L->col++;
            }
            int len = L->pos - start;
            tok.type = TOK_IDENT;
            tok.text = malloc((size_t)len + 1);
            memcpy(tok.text, L->source + start, len);
            tok.text[len] = '\0';
            return tok;
        }

        /* unknown */
        {
            char bad[2] = { L->source[L->pos], '\0' };
            tok.text = strdup(bad);
            error("lex error at %d:%d: unexpected character '%c'", L->line, L->col, L->source[L->pos]);
            return tok;
        }
    }

    tok.type = TOK_EOF;
    tok.text = strdup("");
    return tok;
}

/* ===== AST ===== */

typedef enum {
    NODE_FRAME_DEF, NODE_SPACE_DEF, NODE_PIPE_DEF, NODE_EXEC
} NodeType;

typedef struct {
    NodeType type;
    union {
        struct { char *name, *pos, *neg; } frame;
        struct { char *name; int *indices; char **funcs; int count; } space;
        struct { char *name; char **params; int nparams; void *steps; int nsteps; } pipe;
        struct { char *name; void **args; int nargs; } exec;
    } as;
} ASTNode;

/* ===== ENVIRONMENT TYPES ===== */

typedef Value (*Predicate)(Value);
typedef Value (*BuiltinFn)(Value *args, int nargs);

typedef struct {
    char *name;
    Predicate positive;
    Predicate negative;
} Frame;

typedef struct {
    int idx;
    BuiltinFn fn;
} FnSlot;

typedef struct {
    char *name;
    FnSlot *slots;
    int count;
} Space;

typedef struct Step {
    char kind; /* 'c'=constraint, 'f'=fn_call, 'o'=output, 'p'=pipe_call, 'F'=for_each */
    char *target;
    char ctype; /* 'a'=as, 'n'=not_as */
    char *frame_name;
    int fn_idx;
    int fn_is_ref; /* 1=fn idx is a param ref */
    char *fn_ref;
    char *space_name;
    char **fn_args;
    int n_fn_args;
    char *pipe_name;
    char **pipe_args;
    int n_pipe_args;
    char *for_var;
    char *for_source;
    struct Step *for_body;
    int n_for_body;
} Step;

typedef struct {
    char *name;
    char **params;
    int nparams;
    Step *steps;
    int nsteps;
} Pipeline;

typedef struct Binding {
    char *name;
    Value val;
    struct Binding *next;
} Binding;

typedef struct {
    char *name;
    Frame **frames;
    int nframes, cap_frames;
    Space **spaces;
    int nspaces, cap_spaces;
    Pipeline **pipes;
    int npipes, cap_pipes;
} Env;

/* ===== BUILT-IN PREDICATES ===== */

Value pred_number(Value v) {
    return val_int(v.type == VAL_INT || v.type == VAL_FLOAT);
}
Value pred_int(Value v) {
    return val_int(v.type == VAL_INT);
}
Value pred_string(Value v) {
    return val_int(v.type == VAL_STRING);
}
Value pred_alpha(Value v) {
    if (v.type != VAL_STRING) return val_int(0);
    const char *s = v.as.sval;
    if (!s || !*s) return val_int(0);
    while (*s) { if (!isalpha((unsigned char)*s)) return val_int(0); s++; }
    return val_int(1);
}
Value pred_alnum(Value v) {
    if (v.type != VAL_STRING) return val_int(0);
    const char *s = v.as.sval;
    if (!s || !*s) return val_int(0);
    while (*s) { if (!isalnum((unsigned char)*s)) return val_int(0); s++; }
    return val_int(1);
}
Value pred_zero(Value v) {
    if (v.type == VAL_INT) return val_int(v.as.ival == 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval == 0.0);
    return val_int(0);
}
Value pred_positive(Value v) {
    if (v.type == VAL_INT) return val_int(v.as.ival > 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval > 0.0);
    return val_int(0);
}
Value pred_negative(Value v) {
    if (v.type == VAL_INT) return val_int(v.as.ival < 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval < 0.0);
    return val_int(0);
}
Value pred_nonzero(Value v) {
    if (v.type == VAL_INT) return val_int(v.as.ival != 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval != 0.0);
    return val_int(0);
}
Value pred_even(Value v) {
    if (v.type != VAL_INT) return val_int(0);
    return val_int(v.as.ival % 2 == 0);
}
Value pred_odd(Value v) {
    if (v.type != VAL_INT) return val_int(0);
    return val_int(v.as.ival % 2 != 0);
}
Value pred_empty(Value v) {
    if (v.type == VAL_NIL) return val_int(1);
    if (v.type == VAL_STRING) return val_int(!v.as.sval || !*v.as.sval);
    if (v.type == VAL_INT) return val_int(v.as.ival == 0);
    return val_int(0);
}
Value pred_nil(Value v) {
    return val_int(v.type == VAL_NIL);
}
Value pred_any(Value v) {
    (void)v;
    return val_int(1);
}
Value pred_non_numeric(Value v) {
    return val_int(v.type != VAL_INT && v.type != VAL_FLOAT);
}
Value pred_float(Value v) {
    return val_int(v.type == VAL_FLOAT);
}
Value pred_digit(Value v) {
    if (v.type != VAL_STRING) return val_int(0);
    const char *s = v.as.sval;
    if (!s || !*s) return val_int(0);
    while (*s) { if (!isdigit((unsigned char)*s)) return val_int(0); s++; }
    return val_int(1);
}
Value pred_lowercase(Value v) {
    if (v.type != VAL_STRING) return val_int(0);
    const char *s = v.as.sval;
    if (!s || !*s) return val_int(0);
    while (*s) { if (!islower((unsigned char)*s)) return val_int(0); s++; }
    return val_int(1);
}
Value pred_uppercase(Value v) {
    if (v.type != VAL_STRING) return val_int(0);
    const char *s = v.as.sval;
    if (!s || !*s) return val_int(0);
    while (*s) { if (!isupper((unsigned char)*s)) return val_int(0); s++; }
    return val_int(1);
}
Value pred_truthy(Value v) {
    if (v.type == VAL_NIL) return val_int(0);
    if (v.type == VAL_INT) return val_int(v.as.ival != 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval != 0.0);
    if (v.type == VAL_STRING) return val_int(v.as.sval && *v.as.sval);
    return val_int(0);
}
Value pred_falsy(Value v) {
    if (v.type == VAL_NIL) return val_int(1);
    if (v.type == VAL_INT) return val_int(v.as.ival == 0);
    if (v.type == VAL_FLOAT) return val_int(v.as.fval == 0.0);
    if (v.type == VAL_STRING) return val_int(!v.as.sval || !*v.as.sval);
    return val_int(1);
}
Value pred_finite(Value v) {
    if (v.type == VAL_INT) return val_int(1);
    if (v.type == VAL_FLOAT) return val_int(isfinite(v.as.fval));
    return val_int(0);
}
Value pred_prime(Value v) {
    if (v.type != VAL_INT || v.as.ival < 2) return val_int(0);
    long n = v.as.ival;
    for (long i = 2; i * i <= n; i++)
        if (n % i == 0) return val_int(0);
    return val_int(1);
}

/* ===== BUILT-IN FUNCTIONS ===== */

Value fn_add(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
        return val_int((long)a + (long)b);
    return val_float(a + b);
}
Value fn_sub(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
        return val_int((long)a - (long)b);
    return val_float(a - b);
}
Value fn_mul(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
        return val_int((long)(a * b));
    return val_float(a * b);
}
Value fn_div(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (b == 0.0) return val_float(INFINITY);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT && (long)a % (long)b == 0)
        return val_int((long)(a / b));
    return val_float(a / b);
}
Value fn_double(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    if (args[0].type == VAL_INT) return val_int((long)(a * 2));
    return val_float(a * 2);
}
Value fn_square(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    if (args[0].type == VAL_INT) return val_int((long)(a * a));
    return val_float(a * a);
}
Value fn_abs(Value *args, int n) {
    if (n < 1) return val_nil();
    if (args[0].type == VAL_INT) return val_int(labs(args[0].as.ival));
    return val_float(fabs(val_to_float(args[0])));
}
Value fn_negate(Value *args, int n) {
    if (n < 1) return val_nil();
    if (args[0].type == VAL_INT) return val_int(-args[0].as.ival);
    return val_float(-val_to_float(args[0]));
}
Value fn_upper(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    char *s = strdup(args[0].as.sval);
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return val_str_nocopy(s);
}
Value fn_lower(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    char *s = strdup(args[0].as.sval);
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    return val_str_nocopy(s);
}
Value fn_reverse(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    const char *src = args[0].as.sval;
    unsigned long len = strlen(src);
    char *s = malloc((size_t)len + 1);
    for (unsigned long i = 0; i < len; i++) s[i] = src[len - 1 - i];
    s[len] = '\0';
    return val_str_nocopy(s);
}
Value fn_concat(Value *args, int n) {
    if (n < 2) return val_nil();
    const char *a = val_to_str(args[0]);
    const char *b = val_to_str(args[1]);
    char *s = malloc(strlen(a) + strlen(b) + 1);
    snprintf(s, strlen(a) + strlen(b) + 1, "%s%s", a, b);
    return val_str_nocopy(s);
}
Value fn_length(Value *args, int n) {
    if (n < 1) return val_nil();
    if (args[0].type == VAL_STRING) return val_int((long)strlen(args[0].as.sval));
    return val_int(0);
}
Value fn_capitalize(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING || !args[0].as.sval || !*args[0].as.sval) return val_nil();
    char *s = strdup(args[0].as.sval);
    s[0] = (char)toupper((unsigned char)s[0]);
    return val_str_nocopy(s);
}
Value fn_trim(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    const char *s = args[0].as.sval;
    if (!s) return val_str("");
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return val_str("");
    const char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    size_t len = (size_t)(end - s) + 1;
    char *buf = malloc((size_t)len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    return val_str_nocopy(buf);
}
Value fn_repeat(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_INT) return val_nil();
    const char *s = args[0].as.sval;
    long count = args[1].as.ival;
    if (count <= 0) return val_str("");
    size_t slen = strlen(s);
    char *buf = malloc((size_t)slen * (size_t)count + 1);
    for (long i = 0; i < count; i++) memcpy(buf + (size_t)i * (size_t)slen, s, slen);
    buf[(size_t)slen * (size_t)count] = '\0';
    return val_str_nocopy(buf);
}
Value fn_pow(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    double r = pow(a, b);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT) return val_int((long)r);
    return val_float(r);
}
Value fn_min(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
        return val_int(args[0].as.ival < args[1].as.ival ? args[0].as.ival : args[1].as.ival);
    return val_float(a < b ? a : b);
}
Value fn_max(Value *args, int n) {
    if (n < 2) return val_nil();
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
        return val_int(args[0].as.ival > args[1].as.ival ? args[0].as.ival : args[1].as.ival);
    return val_float(a > b ? a : b);
}
Value fn_sqrt(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    if (a < 0) return val_nil();
    double r = sqrt(a);
    if (args[0].type == VAL_INT) {
        long ir = (long)r;
        if ((double)ir == r) return val_int(ir);
    }
    return val_float(r);
}
Value fn_floor(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    return val_int((long)floor(a));
}
Value fn_ceil(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    return val_int((long)ceil(a));
}
Value fn_round(Value *args, int n) {
    if (n < 1) return val_nil();
    double a = val_to_float(args[0]);
    return val_int((long)round(a));
}
Value fn_mod(Value *args, int n) {
    if (n < 2) return val_nil();
    if (args[0].type == VAL_INT && args[1].type == VAL_INT) {
        if (args[1].as.ival == 0) return val_nil();
        return val_int(args[0].as.ival % args[1].as.ival);
    }
    double a = val_to_float(args[0]), b = val_to_float(args[1]);
    if (b == 0.0) return val_nil();
    return val_float(fmod(a, b));
}
Value fn_title(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    const char *src = args[0].as.sval;
    if (!src || !*src) return val_str("");
    char *s = strdup(src);
    int inword = 0;
    for (char *p = s; *p; p++) {
        if (isalpha((unsigned char)*p)) {
            if (!inword) { *p = (char)toupper((unsigned char)*p); inword = 1; }
            else { *p = (char)tolower((unsigned char)*p); }
        } else {
            inword = 0;
        }
    }
    return val_str_nocopy(s);
}
Value fn_replace(Value *args, int n) {
    if (n < 3 || args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING) return val_nil();
    const char *hay = args[0].as.sval;
    const char *needle = args[1].as.sval;
    const char *repl = args[2].as.sval;
    if (!hay || !needle || !*needle) return val_str(hay ? hay : "");
    size_t hlen = strlen(hay), nlen = strlen(needle), rlen = strlen(repl);
    int count = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += nlen) count++;
    if (count == 0) return val_str(hay);
    size_t buflen = hlen + (size_t)count * (rlen - nlen) + 1;
    char *buf = malloc(buflen);
    char *dst = buf;
    const char *p = hay;
    while (*p) {
        const char *match = strstr(p, needle);
        if (!match) { strcpy(dst, p); break; }
        size_t pre = (size_t)(match - p);
        memcpy(dst, p, pre); dst += pre;
        memcpy(dst, repl, rlen); dst += rlen;
        p = match + nlen;
    }
    return val_str_nocopy(buf);
}
Value fn_substr(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_INT) return val_nil();
    const char *s = args[0].as.sval;
    if (!s) return val_str("");
    long start = args[1].as.ival;
    long slen = (long)strlen(s);
    if (start < 0) start = slen + start;
    if (start < 0) start = 0;
    if (start >= slen) return val_str("");
    long len = slen - start;
    if (n >= 3 && args[2].type == VAL_INT) {
        len = args[2].as.ival;
        if (len < 0) len = 0;
        if (start + len > slen) len = slen - start;
    }
    char *buf = malloc((size_t)len + 1);
    memcpy(buf, s + start, len);
    buf[len] = '\0';
    return val_str_nocopy(buf);
}
Value fn_split(Value *args, int n) {
    if (n < 3 || args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_INT) return val_nil();
    const char *s = args[0].as.sval;
    const char *delim = args[1].as.sval;
    long idx = args[2].as.ival;
    if (!s || !delim || idx < 0) return val_str("");
    if (!*delim) return val_str(s);
    const char *p = s;
    for (long i = 0; i < idx; i++) {
        p = strstr(p, delim);
        if (!p) return val_str("");
        p += strlen(delim);
    }
    const char *end = strstr(p, delim);
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char *buf = malloc((size_t)len + 1);
    memcpy(buf, p, len);
    buf[len] = '\0';
    return val_str_nocopy(buf);
}
Value fn_type(Value *args, int n) {
    if (n < 1) return val_nil();
    const char *t = "";
    switch (args[0].type) {
        case VAL_NIL: t = "nil"; break;
        case VAL_INT: t = "int"; break;
        case VAL_FLOAT: t = "float"; break;
        case VAL_STRING: t = "string"; break;
        case VAL_LIST: t = "list"; break;
    }
    return val_str(t);
}

Value fn_read_file(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    const char *path = args[0].as.sval;
    FILE *fp = fopen(path, "rb");
    if (!fp) return val_nil();
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size && ferror(fp)) { free(buf); fclose(fp); return val_nil(); }
    buf[size] = '\0';
    fclose(fp);
    return val_str_nocopy(buf);
}

Value fn_write_file(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_int(0);
    FILE *fp = fopen(args[0].as.sval, "w");
    if (!fp) return val_int(0);
    fprintf(fp, "%s", args[1].as.sval);
    fclose(fp);
    return val_int(1);
}

Value fn_file_exists(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_int(0);
    struct stat st;
    return val_int(stat(args[0].as.sval, &st) == 0);
}

Value fn_env(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    const char *val = getenv(args[0].as.sval);
    return val ? val_str(val) : val_nil();
}

Value fn_capture(Value *args, int n) {
    if (n < 1 || args[0].type != VAL_STRING) return val_nil();
    FILE *fp = popen(args[0].as.sval, "r");
    if (!fp) return val_nil();
    char *buf = NULL;
    size_t cap = 0;
    size_t total = 0;
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), fp)) {
        size_t clen = strlen(chunk);
        if (total + clen + 1 > cap) { cap = total + clen + 4096; buf = realloc(buf, cap); }
        memcpy(buf + total, chunk, clen);
        total += clen;
    }
    int rc = pclose(fp);
    if (!buf) return val_str("");
    buf[total] = '\0';
    if (total > 0 && buf[total-1] == '\n') buf[--total] = '\0';
    buf[total] = '\0';
    Value v = val_str_nocopy(buf);
    if (n >= 2 && args[1].type == VAL_INT) {
        v.as.sval[total] = '\0';
        (void)rc;
    }
    return v;
}

Value fn_split_list(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_nil();
    const char *s = args[0].as.sval;
    const char *delim = args[1].as.sval;
    if (!s || !delim || !*delim) { Value v = val_list_new(); val_list_append(&v, val_str(s ? s : "")); return v; }
    Value list = val_list_new();
    size_t dlen = strlen(delim);
    const char *p = s;
    while (*p) {
        const char *end = strstr(p, delim);
        if (!end) {
            val_list_append(&list, val_str(p));
            break;
        }
        if (end > p) {
            size_t len = (size_t)(end - p);
            char *chunk = malloc(len + 1);
            memcpy(chunk, p, len);
            chunk[len] = '\0';
            val_list_append(&list, val_str_nocopy(chunk));
        } else {
            val_list_append(&list, val_str(""));
        }
        p = end + dlen;
    }
    return list;
}

Value fn_join_list(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_LIST || args[1].type != VAL_STRING) return val_nil();
    ValueList *lv = args[0].as.lval;
    const char *delim = args[1].as.sval;
    if (!lv->count) return val_str("");
    size_t total = 0;
    char **strs = malloc((size_t)lv->count * sizeof(char*));
    for (int i = 0; i < lv->count; i++) {
        strs[i] = strdup(val_to_str(lv->items[i]));
        total += strlen(strs[i]);
        if (i < lv->count - 1) total += strlen(delim);
    }
    char *buf = malloc((size_t)total + 1);
    char *dst = buf;
    for (int i = 0; i < lv->count; i++) {
        size_t slen = strlen(strs[i]);
        memcpy(dst, strs[i], slen); dst += slen;
        if (i < lv->count - 1) { memcpy(dst, delim, strlen(delim)); dst += strlen(delim); }
        free(strs[i]);
    }
    *dst = '\0';
    free(strs);
    return val_str_nocopy(buf);
}

Value fn_regex_match(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_int(0);
    regex_t regex;
    if (regcomp(&regex, args[1].as.sval, REG_EXTENDED | REG_NOSUB)) return val_int(0);
    int rc = regexec(&regex, args[0].as.sval, 0, NULL, 0);
    regfree(&regex);
    return val_int(rc == 0);
}

Value fn_regex_extract(Value *args, int n) {
    if (n < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_nil();
    regex_t regex;
    if (regcomp(&regex, args[1].as.sval, REG_EXTENDED)) return val_nil();
    regmatch_t matches[2];
    if (regexec(&regex, args[0].as.sval, 2, matches, 0) == 0 && matches[1].rm_so >= 0) {
        int len = matches[1].rm_eo - matches[1].rm_so;
        char *buf = malloc((size_t)len + 1);
        memcpy(buf, args[0].as.sval + matches[1].rm_so, len);
        buf[len] = '\0';
        regfree(&regex);
        return val_str_nocopy(buf);
    }
    regfree(&regex);
    return val_nil();
}

Value fn_regex_replace(Value *args, int n) {
    if (n < 3 || args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING) return val_nil();
    const char *hay = args[0].as.sval;
    const char *pattern = args[1].as.sval;
    const char *repl = args[2].as.sval;
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED)) return val_str(hay);
    size_t hlen = strlen(hay);
    regoff_t rm_so = 0, rm_eo = 0;
    if (regexec(&regex, hay, 0, NULL, 0) == 0) {
        regmatch_t m;
        if (regexec(&regex, hay, 1, &m, 0) == 0) { rm_so = m.rm_so; rm_eo = m.rm_eo; }
    }
    regfree(&regex);
    if (rm_so == rm_eo && rm_so == 0) return val_str(hay);
    size_t rlen = strlen(repl);
    size_t buflen = hlen - (size_t)(rm_eo - rm_so) + rlen + 1;
    char *buf = malloc(buflen);
    memcpy(buf, hay, rm_so);
    memcpy(buf + rm_so, repl, rlen);
    memcpy(buf + rm_so + rlen, hay + rm_eo, hlen - (size_t)rm_eo + 1);
    regfree(&regex);
    return val_str_nocopy(buf);
}

/* ===== ENVIRONMENT ===== */

Env *env_new(void) {
    Env *env = calloc(1, sizeof(Env));
    env->cap_frames = 32;
    env->frames = malloc((size_t)env->cap_frames * sizeof(Frame*));
    env->cap_spaces = 32;
    env->spaces = malloc((size_t)env->cap_spaces * sizeof(Space*));
    env->cap_pipes = 32;
    env->pipes = malloc((size_t)env->cap_pipes * sizeof(Pipeline*));

    /* register built-in frames */
    struct { char *name; Predicate pos, neg; } builtins[] = {
        {"nil",      pred_nil,      pred_empty},
        {"number",   pred_number,   pred_nil},
        {"int",      pred_int,      pred_nil},
        {"string",   pred_string,   pred_nil},
        {"alpha",    pred_alpha,    pred_nil},
        {"alnum",    pred_alnum,    pred_nil},
        {"zero",     pred_zero,     pred_nonzero},
        {"positive", pred_positive, pred_negative},
        {"negative", pred_negative, pred_positive},
        {"nonzero",  pred_nonzero,  pred_zero},
        {"even",     pred_even,     pred_nil},
        {"odd",      pred_odd,      pred_nil},
        {"empty",    pred_empty,    pred_nil},
        {"any",      pred_any,      pred_nil},
        {"non_numeric", pred_non_numeric, pred_nil},
        {"float",    pred_float,    pred_nil},
        {"digit",    pred_digit,    pred_nil},
        {"lowercase", pred_lowercase, pred_nil},
        {"uppercase", pred_uppercase, pred_nil},
        {"truthy",   pred_truthy,   pred_falsy},
        {"falsy",    pred_falsy,    pred_truthy},
        {"finite",   pred_finite,   pred_nil},
        {"prime",    pred_prime,    pred_nil},
        {NULL, NULL, NULL}
    };
    for (int i = 0; builtins[i].name; i++) {
        Frame *f = malloc(sizeof(Frame));
        f->name = strdup(builtins[i].name);
        f->positive = builtins[i].pos;
        f->negative = builtins[i].neg;
        env->frames[env->nframes++] = f;
    }

    return env;
}

Frame *env_get_frame(Env *env, const char *name) {
    for (int i = 0; i < env->nframes; i++)
        if (!strcmp(env->frames[i]->name, name)) return env->frames[i];
    return NULL;
}

void env_add_frame(Env *env, const char *name, const char *pos_name, const char *neg_name) {
    Frame *pos = env_get_frame(env, pos_name);
    Frame *neg = env_get_frame(env, neg_name);
    if (!pos) error("unknown positive frame '%s'", pos_name);
    if (!neg) error("unknown negative frame '%s'", neg_name);

    Frame *f = malloc(sizeof(Frame));
    f->name = strdup(name);
    /* compose: positive uses pos's positive, negative uses neg's positive (what to avoid) */
    f->positive = pos->positive;
    f->negative = neg->positive;

    if (env->nframes >= env->cap_frames) {
        env->cap_frames *= 2;
        env->frames = realloc(env->frames, (size_t)env->cap_frames * sizeof(Frame*));
    }
    env->frames[env->nframes++] = f;
}

Space *env_get_space(Env *env, const char *name) {
    for (int i = 0; i < env->nspaces; i++)
        if (!strcmp(env->spaces[i]->name, name)) return env->spaces[i];
    return NULL;
}

void env_add_space(Env *env, const char *name) {
    Space *s = calloc(1, sizeof(Space));
    s->name = strdup(name);
    s->slots = NULL;
    s->count = 0;
    if (env->nspaces >= env->cap_spaces) {
        env->cap_spaces *= 2;
        env->spaces = realloc(env->spaces, (size_t)env->cap_spaces * sizeof(Space*));
    }
    env->spaces[env->nspaces++] = s;
}

void space_add_fn(Space *s, int idx, BuiltinFn fn) {
    s->count++;
    s->slots = realloc(s->slots, (size_t)s->count * sizeof(FnSlot));
    s->slots[s->count-1].idx = idx;
    s->slots[s->count-1].fn = fn;
}

Pipeline *env_get_pipe(Env *env, const char *name) {
    for (int i = 0; i < env->npipes; i++)
        if (!strcmp(env->pipes[i]->name, name)) return env->pipes[i];
    return NULL;
}

void env_add_pipe(Env *env, Pipeline *p) {
    if (env->npipes >= env->cap_pipes) {
        env->cap_pipes *= 2;
        env->pipes = realloc(env->pipes, (size_t)env->cap_pipes * sizeof(Pipeline*));
    }
    env->pipes[env->npipes++] = p;
}

/* ===== PARSER ===== */

typedef struct {
    Lexer *lex;
    Env *env;
    Token tok;
} Parser;

Parser *parser_new(Lexer *lex, Env *env) {
    Parser *P = calloc(1, sizeof(Parser));
    P->lex = lex;
    P->env = env;
    P->tok = lex_next(lex);
    return P;
}

void parser_advance(Parser *P) {
    if (P->tok.text) free(P->tok.text);
    P->tok = lex_next(P->lex);
}

void parser_expect(Parser *P, TokenType type, const char *msg) {
    if (P->tok.type != type)
        error("parse error at %d:%d: expected %s, got '%s'", 
              P->tok.line, P->tok.col, msg, P->tok.text);
    parser_advance(P);
}

int parser_match(Parser *P, TokenType type) {
    if (P->tok.type == type) { parser_advance(P); return 1; }
    return 0;
}

int parser_match_ident(Parser *P, const char *val) {
    if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, val)) {
        parser_advance(P); return 1;
    }
    return 0;
}

/* forward declarations */
ASTNode parse_def(Parser *P);
ASTNode parse_exec(Parser *P);

ASTNode parse_frame_def(Parser *P) {
    ASTNode node = { .type = NODE_FRAME_DEF };
    node.as.frame.name = strdup(P->tok.text);
    parser_advance(P); /* name */
    /* check 'as' */
    if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "as") != 0)
        error("expected 'as', got '%s' at %d:%d", P->tok.text, P->tok.line, P->tok.col);
    parser_advance(P); /* 'as' */
    node.as.frame.pos = strdup(P->tok.text);
    parser_advance(P); /* positive frame */
    /* check 'not_as' */
    if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "not_as") != 0)
        error("expected 'not_as', got '%s' at %d:%d", P->tok.text, P->tok.line, P->tok.col);
    parser_advance(P); /* 'not_as' */
    node.as.frame.neg = strdup(P->tok.text);
    parser_advance(P); /* negative frame */
    parser_expect(P, TOK_SEMI, "';'");
    return node;
}

ASTNode parse_space_def(Parser *P) {
    ASTNode node = { .type = NODE_SPACE_DEF };
    node.as.space.name = strdup(P->tok.text);
    parser_advance(P); /* name */
    parser_expect(P, TOK_LBRACE, "'{'");

    int cap = 8;
    node.as.space.indices = malloc((size_t)cap * sizeof(int));
    node.as.space.funcs = malloc((size_t)cap * sizeof(char*));
    node.as.space.count = 0;

    while (P->tok.type != TOK_RBRACE && P->tok.type != TOK_EOF) {
        if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "fn") != 0)
            error("expected 'fn', got '%s' at %d:%d", P->tok.text, P->tok.line, P->tok.col);
        parser_advance(P); /* fn */
        parser_expect(P, TOK_LBRACK, "'['");
        if (node.as.space.count >= cap) { cap *= 2; node.as.space.indices = realloc(node.as.space.indices, (size_t)cap * sizeof(int)); node.as.space.funcs = realloc(node.as.space.funcs, (size_t)cap * sizeof(char*)); }
        node.as.space.indices[node.as.space.count] = atoi(P->tok.text);
        parser_advance(P); /* index */
        parser_expect(P, TOK_RBRACK, "']'");
        parser_expect(P, TOK_EQ, "'='");
        node.as.space.funcs[node.as.space.count] = strdup(P->tok.text);
        node.as.space.count++;
        parser_advance(P); /* function name */
        parser_match(P, TOK_COMMA); /* optional comma */
        parser_match(P, TOK_SEMI); /* optional semicolon */
    }
    parser_expect(P, TOK_RBRACE, "'}'");
    parser_expect(P, TOK_SEMI, "';'");
    return node;
}

Step *parse_body(Parser *P, int *nsteps);

Step parse_step(Parser *P) {
    Step s = { .kind = 0 };

    if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, "for")) {
        s.kind = 'F';
        parser_advance(P); /* for */
        s.for_var = strdup(P->tok.text);
        parser_advance(P); /* var */
        if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "in") != 0)
            error("expected 'in', got '%s'", P->tok.text);
        parser_advance(P); /* in */
        s.for_source = strdup(P->tok.text);
        parser_advance(P); /* source */
        parser_expect(P, TOK_LBRACE, "'{'");
        s.for_body = parse_body(P, &s.n_for_body);
        parser_expect(P, TOK_RBRACE, "'}'");
        return s;
    }

    if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, "output")) {
        s.kind = 'o';
        parser_advance(P);
        s.target = strdup(P->tok.text);
        parser_advance(P);
        return s;
    }

    if (P->tok.type == TOK_ARROW) {
        parser_advance(P);
        if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, "pipe")) {
            s.kind = 'p';
            parser_advance(P); /* pipe */
            s.pipe_name = strdup(P->tok.text);
            parser_advance(P); /* pipe name */
            if (P->tok.type == TOK_LPAREN) {
                parser_advance(P);
                int cap = 4;
                s.pipe_args = malloc((size_t)cap * sizeof(char*));
                s.n_pipe_args = 0;
                while (P->tok.type != TOK_RPAREN && P->tok.type != TOK_EOF) {
                    if (s.n_pipe_args >= cap) { cap *= 2; s.pipe_args = realloc(s.pipe_args, (size_t)cap * sizeof(char*)); }
                    s.pipe_args[s.n_pipe_args++] = strdup(P->tok.text);
                    parser_advance(P);
                    parser_match(P, TOK_COMMA);
                }
                parser_expect(P, TOK_RPAREN, "')'");
            }
            return s;
        }
        if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "fn") != 0)
            error("expected 'fn' or 'pipe', got '%s'", P->tok.text);
        parser_advance(P);
        parser_expect(P, TOK_LBRACK, "'['");
        s.kind = 'f';
        if (P->tok.type == TOK_NUMBER) {
            s.fn_idx = atoi(P->tok.text);
            s.fn_is_ref = 0;
        } else {
            s.fn_ref = strdup(P->tok.text);
            s.fn_is_ref = 1;
        }
        parser_advance(P);
        parser_expect(P, TOK_RBRACK, "']'");
        if (P->tok.type != TOK_IDENT || strcmp(P->tok.text, "from") != 0)
            error("expected 'from', got '%s'", P->tok.text);
        parser_advance(P);
        s.space_name = strdup(P->tok.text);
        parser_advance(P);

        /* optional explicit args */
        if (P->tok.type == TOK_LPAREN) {
            parser_advance(P);
            int cap = 4;
            s.fn_args = malloc((size_t)cap * sizeof(char*));
            s.n_fn_args = 0;
            while (P->tok.type != TOK_RPAREN && P->tok.type != TOK_EOF) {
                if (s.n_fn_args >= cap) { cap *= 2; s.fn_args = realloc(s.fn_args, (size_t)cap * sizeof(char*)); }
                s.fn_args[s.n_fn_args++] = strdup(P->tok.text);
                parser_advance(P);
                parser_match(P, TOK_COMMA);
            }
            parser_expect(P, TOK_RPAREN, "')'");
        }
        return s;
    }

    /* constraint step */
    s.kind = 'c';
    s.target = strdup(P->tok.text);
    parser_advance(P);
    if (!strcmp(P->tok.text, "as")) s.ctype = 'a';
    else if (!strcmp(P->tok.text, "not_as")) s.ctype = 'n';
    else error("expected 'as' or 'not_as', got '%s'", P->tok.text);
    parser_advance(P);
    s.frame_name = strdup(P->tok.text);
    parser_advance(P);
    return s;
}

Step *parse_body(Parser *P, int *nsteps) {
    int cap = 8;
    Step *steps = malloc((size_t)cap * sizeof(Step));
    *nsteps = 0;

    while (P->tok.type != TOK_RBRACE && P->tok.type != TOK_EOF) {
        if (*nsteps >= cap) { cap *= 2; steps = realloc(steps, (size_t)cap * sizeof(Step)); }
        steps[(*nsteps)++] = parse_step(P);
        parser_match(P, TOK_SEMI); /* optional semi */
    }
    return steps;
}

ASTNode parse_pipe_def(Parser *P) {
    ASTNode node = { .type = NODE_PIPE_DEF };

    node.as.pipe.name = strdup(P->tok.text);
    parser_advance(P); /* name */
    parser_expect(P, TOK_LPAREN, "'('");
    int cap = 4;
    node.as.pipe.params = malloc((size_t)cap * sizeof(char*));
    node.as.pipe.nparams = 0;
    while (P->tok.type != TOK_RPAREN && P->tok.type != TOK_EOF) {
        if (node.as.pipe.nparams >= cap) { cap *= 2; node.as.pipe.params = realloc(node.as.pipe.params, (size_t)cap * sizeof(char*)); }
        node.as.pipe.params[node.as.pipe.nparams++] = strdup(P->tok.text);
        parser_advance(P);
        parser_match(P, TOK_COMMA);
    }
    parser_expect(P, TOK_RPAREN, "')'");
    parser_expect(P, TOK_LBRACE, "'{'");
    node.as.pipe.steps = parse_body(P, &node.as.pipe.nsteps);
    parser_expect(P, TOK_RBRACE, "'}'");
    parser_expect(P, TOK_SEMI, "';'");
    return node;
}

ASTNode parse_exec(Parser *P) {
    ASTNode node = { .type = NODE_EXEC };
    node.as.exec.name = strdup(P->tok.text);
    parser_advance(P); /* name */
    parser_expect(P, TOK_LPAREN, "'('");
    int cap = 8;
    node.as.exec.args = malloc((size_t)cap * sizeof(void*));
    node.as.exec.nargs = 0;
    while (P->tok.type != TOK_RPAREN && P->tok.type != TOK_EOF) {
        if (node.as.exec.nargs >= cap) { cap *= 2; node.as.exec.args = realloc(node.as.exec.args, (size_t)cap * sizeof(void*)); }
        Value *v = malloc(sizeof(Value));
        if (P->tok.type == TOK_STRING) {
            *v = val_str(P->tok.text);
        } else if (P->tok.type == TOK_NUMBER) {
            if (strchr(P->tok.text, '.'))
                *v = val_float(atof(P->tok.text));
            else
                *v = val_int(atol(P->tok.text));
        } else if (P->tok.type == TOK_IDENT) {
            *v = val_str(P->tok.text);
        }
        node.as.exec.args[node.as.exec.nargs++] = v;
        parser_advance(P);
        parser_match(P, TOK_COMMA);
    }
    parser_expect(P, TOK_RPAREN, "')'");
    parser_expect(P, TOK_SEMI, "';'");
    return node;
}

ASTNode parse_def(Parser *P) {
    if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, "space")) {
        parser_advance(P);
        return parse_space_def(P);
    }
    if (P->tok.type == TOK_IDENT && !strcmp(P->tok.text, "pipe")) {
        parser_advance(P);
        return parse_pipe_def(P);
    }
    /* check if it's a frame_def: IDENT 'as' IDENT 'not_as' IDENT */
    /* need to look ahead: current is name, next should be 'as' */
    {
        Lexer *L = P->lex;
        int saved_pos = L->pos;
        int saved_line = L->line, saved_col = L->col;
        Token saved = P->tok;
        /* peek ahead to see if this looks like a frame_def */
        Token t = lex_next(L);
        int is_frame = (t.type == TOK_IDENT && !strcmp(t.text, "as"));
        free(t.text);
        /* restore lexer state */
        L->pos = saved_pos;
        L->line = saved_line;
        L->col = saved_col;
        P->tok = saved;

        if (is_frame) {
            return parse_frame_def(P);
        }
    }
    /* lookahead for pipe name: IDENT '(' ... ')' '{' */
    {
        Lexer *L = P->lex;
        int saved_pos = L->pos;
        int saved_line = L->line, saved_col = L->col;
        Token saved = P->tok;
        int is_pipe = 0;
        Token t = lex_next(L);
        if (t.type == TOK_LPAREN) {
            free(t.text);
            int depth = 1;
            while (depth > 0) {
                Token inner = lex_next(L);
                if (inner.type == TOK_LPAREN) depth++;
                if (inner.type == TOK_RPAREN) depth--;
                if (inner.type == TOK_EOF) { free(inner.text); break; }
                if (depth == 0) {
                    Token after = lex_next(L);
                    is_pipe = (after.type == TOK_LBRACE);
                    free(after.text);
                }
                free(inner.text);
            }
        } else {
            free(t.text);
        }
        L->pos = saved_pos;
        L->line = saved_line;
        L->col = saved_col;
        P->tok = saved;

        if (is_pipe) {
            return parse_pipe_def(P);
        }
    }
    return parse_exec(P);
}

ASTNode *parse_all(Parser *P, int *n) {
    int cap = 16;
    ASTNode *nodes = malloc((size_t)cap * sizeof(ASTNode));
    *n = 0;

    while (P->tok.type != TOK_EOF) {
        if (P->tok.type == TOK_SEMI) { parser_advance(P); continue; }
        if (*n >= cap) { cap *= 2; nodes = realloc(nodes, (size_t)cap * sizeof(ASTNode)); }
        nodes[(*n)++] = parse_def(P);
    }
    return nodes;
}

/* ===== INTERPRETER ===== */

Binding *binding_add(Binding **list, const char *name, Value val) {
    Binding *b = malloc(sizeof(Binding));
    b->name = strdup(name);
    b->val = val;
    b->next = *list;
    *list = b;
    return b;
}

Binding *binding_find(Binding *list, const char *name) {
    for (Binding *b = list; b; b = b->next)
        if (!strcmp(b->name, name)) return b;
    return NULL;
}

void binding_free(Binding *list) {
    while (list) {
        Binding *next = list->next;
        val_free(list->val);
        free(list->name);
        free(list);
        list = next;
    }
}

static void free_ast(ASTNode *nodes, int n) {
    for (int i = 0; i < n; i++) {
        ASTNode *node = &nodes[i];
        switch (node->type) {
            case NODE_FRAME_DEF:
                free(node->as.frame.name);
                free(node->as.frame.pos);
                free(node->as.frame.neg);
                break;
            case NODE_SPACE_DEF:
                free(node->as.space.name);
                for (int j = 0; j < node->as.space.count; j++)
                    free(node->as.space.funcs[j]);
                free(node->as.space.funcs);
                free(node->as.space.indices);
                break;
            case NODE_PIPE_DEF:
                if (node->as.pipe.params) {
                    for (int j = 0; j < node->as.pipe.nparams; j++)
                        free(node->as.pipe.params[j]);
                    free(node->as.pipe.params);
                }
                if (node->as.pipe.steps) {
                    Step *steps = (Step *)node->as.pipe.steps;
                    for (int j = 0; j < node->as.pipe.nsteps; j++) {
                        free(steps[j].target);
                        free(steps[j].frame_name);
                        free(steps[j].fn_ref);
                        free(steps[j].space_name);
                        for (int k = 0; k < steps[j].n_fn_args; k++)
                            free(steps[j].fn_args[k]);
                        free(steps[j].fn_args);
                        free(steps[j].pipe_name);
                        for (int k = 0; k < steps[j].n_pipe_args; k++)
                            free(steps[j].pipe_args[k]);
                        free(steps[j].pipe_args);
                        free(steps[j].for_var);
                        free(steps[j].for_source);
                        if (steps[j].for_body) {
                            for (int k = 0; k < steps[j].n_for_body; k++) {
                                free(steps[j].for_body[k].target);
                                free(steps[j].for_body[k].frame_name);
                                free(steps[j].for_body[k].fn_ref);
                                free(steps[j].for_body[k].space_name);
                                for (int m = 0; m < steps[j].for_body[k].n_fn_args; m++)
                                    free(steps[j].for_body[k].fn_args[m]);
                                free(steps[j].for_body[k].fn_args);
                            }
                            free(steps[j].for_body);
                        }
                    }
                    free(steps);
                }
                break;
            case NODE_EXEC:
                free(node->as.exec.name);
                free(node->as.exec.args);
                break;
        }
    }
    free(nodes);
}

Value interpret(ASTNode *nodes, int n, Env *env) {
    Value last_result = val_nil();

    for (int i = 0; i < n; i++) {
        ASTNode *node = &nodes[i];

        switch (node->type) {
            case NODE_FRAME_DEF:
                env_add_frame(env, node->as.frame.name, node->as.frame.pos, node->as.frame.neg);
                break;

            case NODE_SPACE_DEF: {
                env_add_space(env, node->as.space.name);
                Space *s = env_get_space(env, node->as.space.name);

                struct { char *name; BuiltinFn fn; } builtin_fns[] = {
                    {"add", fn_add}, {"sub", fn_sub}, {"mul", fn_mul}, {"div", fn_div},
                    {"double", fn_double}, {"square", fn_square}, {"abs", fn_abs}, {"negate", fn_negate},
                    {"upper", fn_upper}, {"lower", fn_lower}, {"reverse", fn_reverse},
                    {"concat", fn_concat}, {"length", fn_length},
                    {"capitalize", fn_capitalize}, {"trim", fn_trim}, {"repeat", fn_repeat},
                    {"pow", fn_pow}, {"min", fn_min}, {"max", fn_max},
                    {"sqrt", fn_sqrt}, {"floor", fn_floor}, {"ceil", fn_ceil},
                    {"round", fn_round}, {"mod", fn_mod},
                    {"title", fn_title}, {"replace", fn_replace},
                    {"substr", fn_substr}, {"split", fn_split},
                    {"type", fn_type},
                    {"read_file", fn_read_file},
                    {"write_file", fn_write_file},
                    {"file_exists", fn_file_exists},
                    {"env", fn_env},
                    {"capture", fn_capture},
                    {"split_list", fn_split_list},
                    {"join_list", fn_join_list},
                    {"regex_match", fn_regex_match},
                    {"regex_extract", fn_regex_extract},
                    {"regex_replace", fn_regex_replace},
                    {NULL, NULL}
                };

                for (int j = 0; j < node->as.space.count; j++) {
                    const char *fname = node->as.space.funcs[j];
                    int idx = node->as.space.indices[j];
                    BuiltinFn fn = NULL;
                    for (int k = 0; builtin_fns[k].name; k++) {
                        if (!strcmp(builtin_fns[k].name, fname)) {
                            fn = builtin_fns[k].fn;
                            break;
                        }
                    }
                    if (!fn) error("unknown function '%s' in space '%s'", fname, s->name);
                    space_add_fn(s, idx, fn);
                }
                break;
            }

            case NODE_PIPE_DEF: {
                Pipeline *pipe = calloc(1, sizeof(Pipeline));
                pipe->name = node->as.pipe.name;
                pipe->nparams = node->as.pipe.nparams;
                pipe->params = node->as.pipe.params;
                pipe->nsteps = node->as.pipe.nsteps;
                pipe->steps = node->as.pipe.steps;
                /* take ownership */
                node->as.pipe.params = NULL;
                node->as.pipe.steps = NULL;
                env_add_pipe(env, pipe);
                break;
            }

            case NODE_EXEC: {
                Pipeline *pipe = env_get_pipe(env, node->as.exec.name);
                if (!pipe) error("unknown pipeline '%s'", node->as.exec.name);
                if (node->as.exec.nargs != pipe->nparams)
                    error("pipeline '%s' expects %d args, got %d", 
                          pipe->name, pipe->nparams, node->as.exec.nargs);

                Binding *bindings = NULL;
                for (int j = 0; j < pipe->nparams; j++) {
                    Value *v = (Value*)node->as.exec.args[j];
                    binding_add(&bindings, pipe->params[j], *v);
                    free(v);
                    node->as.exec.args[j] = NULL;
                }

                Value result = val_nil();
                for (int j = 0; j < pipe->nsteps; j++) {
                    Step *s = &pipe->steps[j];

                    if (s->kind == 'c') {
                        Frame *f = env_get_frame(env, s->frame_name);
                        if (!f) error("unknown frame '%s'", s->frame_name);
                        Binding *b = binding_find(bindings, s->target);
                        if (!b) error("unknown binding '%s'", s->target);

                        Value pos_result = f->positive(b->val);
                        Value neg_result = f->negative(b->val);

                        if (s->ctype == 'a') {
                            if (pos_result.type != VAL_INT || pos_result.as.ival == 0)
                                error("constraint failed: '%s' as '%s' (positive violated)", s->target, s->frame_name);
                            if (neg_result.type == VAL_INT && neg_result.as.ival != 0)
                                error("constraint failed: '%s' as '%s' (negative violated)", s->target, s->frame_name);
                        } else if (s->ctype == 'n') {
                            if (pos_result.type == VAL_INT && pos_result.as.ival != 0)
                                error("constraint failed: '%s' not_as '%s' (counts as it)", s->target, s->frame_name);
                        }
                    } else if (s->kind == 'f') {
                        Space *sp = env_get_space(env, s->space_name);
                        if (!sp) error("unknown space '%s'", s->space_name);

                        int idx;
                        if (s->fn_is_ref) {
                            Binding *b = binding_find(bindings, s->fn_ref);
                            if (!b || b->val.type != VAL_INT)
                                error("unknown or non-integer fn ref '%s'", s->fn_ref);
                            idx = (int)b->val.as.ival;
                        } else {
                            idx = s->fn_idx;
                        }

                        BuiltinFn fn = NULL;
                        for (int k = 0; k < sp->count; k++) {
                            if (sp->slots[k].idx == idx) { fn = sp->slots[k].fn; break; }
                        }
                        if (!fn) error("fn[%d] not defined in space '%s'", idx, s->space_name);

                        Value args[16];
                        int nargs = 0;
                        if (s->n_fn_args > 0) {
                            for (int k = 0; k < s->n_fn_args && k < 16; k++) {
                                Binding *b = binding_find(bindings, s->fn_args[k]);
                                if (b) {
                                    args[nargs++] = b->val;
                                } else {
                                    /* try as literal */
                                    char *end;
                                    long iv = strtol(s->fn_args[k], &end, 10);
                                    if (*end == '\0' && end != s->fn_args[k]) {
                                        args[nargs++] = val_int(iv);
                                    } else if (*end == '.' && *(end+1)) {
                                        double fv = strtod(s->fn_args[k], &end);
                                        if (*end == '\0') { args[nargs++] = val_float(fv); }
                                        else { args[nargs++] = val_str(s->fn_args[k]); }
                                    } else {
                                        args[nargs++] = val_str(s->fn_args[k]);
                                    }
                                }
                            }
                        } else {
                            Binding *b = binding_find(bindings, "result");
                            if (b) {
                                args[nargs++] = b->val;
                            } else {
                                for (Binding *bb = bindings; bb && nargs < 16; bb = bb->next) {
                                    if (s->fn_is_ref && !strcmp(bb->name, s->fn_ref)) continue;
                                    args[nargs++] = bb->val;
                                }
                            }
                        }

                        result = fn(args, nargs);
                        binding_add(&bindings, "result", result);
                    } else if (s->kind == 'o') {
                        Binding *b = binding_find(bindings, s->target);
                        if (!b) error("unknown binding '%s' for output", s->target);
                        result = b->val;
                    } else if (s->kind == 'p') {
                        Pipeline *called = env_get_pipe(env, s->pipe_name);
                        if (!called) error("unknown pipeline '%s'", s->pipe_name);
                        Binding *child = NULL;
                        for (int k = 0; k < s->n_pipe_args && k < called->nparams; k++) {
                            Binding *b = binding_find(bindings, s->pipe_args[k]);
                            if (b) {
                                binding_add(&child, called->params[k], val_clone(b->val));
                            } else {
                                char *end;
                                long iv = strtol(s->pipe_args[k], &end, 10);
                                if (*end == '\0') binding_add(&child, called->params[k], val_int(iv));
                                else binding_add(&child, called->params[k], val_str(s->pipe_args[k]));
                            }
                        }
                        Value child_result = val_nil();
                        for (int k = 0; k < called->nsteps; k++) {
                            Step *cs = &called->steps[k];
                            if (cs->kind == 'c') {
                                Frame *f = env_get_frame(env, cs->frame_name);
                                if (!f) error("unknown frame '%s'", cs->frame_name);
                                Binding *b = binding_find(child, cs->target);
                                if (!b) error("unknown binding '%s'", cs->target);
                                Value pos_result = f->positive(b->val);
                                Value neg_result = f->negative(b->val);
                                if (cs->ctype == 'a') {
                                    if (pos_result.type != VAL_INT || pos_result.as.ival == 0)
                                        error("constraint failed: '%s' as '%s'", cs->target, cs->frame_name);
                                    if (neg_result.type == VAL_INT && neg_result.as.ival != 0)
                                        error("constraint failed: '%s' as '%s'", cs->target, cs->frame_name);
                                } else {
                                    if (pos_result.type == VAL_INT && pos_result.as.ival != 0)
                                        error("constraint failed: '%s' not_as '%s'", cs->target, cs->frame_name);
                                }
                            } else if (cs->kind == 'f') {
                                Space *sp = env_get_space(env, cs->space_name);
                                if (!sp) error("unknown space '%s'", cs->space_name);
                                int idx;
                                if (cs->fn_is_ref) {
                                    Binding *b = binding_find(child, cs->fn_ref);
                                    if (!b || b->val.type != VAL_INT) error("non-int fn ref '%s'", cs->fn_ref);
                                    idx = (int)b->val.as.ival;
                                } else idx = cs->fn_idx;
                                BuiltinFn fn = NULL;
                                for (int m = 0; m < sp->count; m++)
                                    if (sp->slots[m].idx == idx) { fn = sp->slots[m].fn; break; }
                                if (!fn) error("fn[%d] not in space '%s'", idx, cs->space_name);
                                Value fargs[16]; int nfargs = 0;
                                if (cs->n_fn_args > 0) {
                                    for (int m = 0; m < cs->n_fn_args && m < 16; m++) {
                                        Binding *b = binding_find(child, cs->fn_args[m]);
                                        if (b) { fargs[nfargs++] = b->val; }
                                        else {
                                            char *end; long iv = strtol(cs->fn_args[m], &end, 10);
                                            if (*end == '\0' && end != cs->fn_args[m]) fargs[nfargs++] = val_int(iv);
                                            else fargs[nfargs++] = val_str(cs->fn_args[m]);
                                        }
                                    }
                                } else {
                                    for (Binding *bb = child; bb && nfargs < 16; bb = bb->next)
                                        if (!cs->fn_is_ref || strcmp(bb->name, cs->fn_ref))
                                            fargs[nfargs++] = bb->val;
                                }
                                child_result = fn(fargs, nfargs);
                                binding_add(&child, "result", child_result);
                            } else if (cs->kind == 'o') {
                                Binding *b = binding_find(child, cs->target);
                                if (!b) error("unknown binding '%s'", cs->target);
                                child_result = b->val;
                            } else if (cs->kind == 'p') {
                                Pipeline *nested = env_get_pipe(env, cs->pipe_name);
                                if (!nested) error("unknown pipeline '%s'", cs->pipe_name);
                                Binding *nchild = NULL;
                                for (int m = 0; m < cs->n_pipe_args && m < nested->nparams; m++) {
                                    Binding *b = binding_find(child, cs->pipe_args[m]);
                                    if (b) binding_add(&nchild, nested->params[m], val_clone(b->val));
                                    else binding_add(&nchild, nested->params[m], val_str(cs->pipe_args[m]));
                                }
                                Value nresult = val_nil();
                                for (int m = 0; m < nested->nsteps; m++) {
                                    Step *ns = &nested->steps[m];
                                    if (ns->kind == 'c') {
                                        Frame *f = env_get_frame(env, ns->frame_name);
                                        if (!f) error("unknown frame '%s'", ns->frame_name);
                                        Binding *b = binding_find(nchild, ns->target);
                                        if (!b) error("unknown binding '%s'", ns->target);
                                        Value pr = f->positive(b->val);
                                        Value nr = f->negative(b->val);
                                        if (ns->ctype == 'a') {
                                            if (pr.type != VAL_INT || pr.as.ival == 0) error("constraint failed: '%s' as '%s'", ns->target, ns->frame_name);
                                            if (nr.type == VAL_INT && nr.as.ival != 0) error("constraint failed: '%s' as '%s'", ns->target, ns->frame_name);
                                        } else {
                                            if (pr.type == VAL_INT && pr.as.ival != 0) error("constraint failed: '%s' not_as '%s'", ns->target, ns->frame_name);
                                        }
                                    } else if (ns->kind == 'f') {
                                        Space *sp = env_get_space(env, ns->space_name);
                                        if (!sp) error("unknown space '%s'", ns->space_name);
                                        int idx = ns->fn_is_ref ? ((Binding*)binding_find(nchild, ns->fn_ref)) ? (int)binding_find(nchild, ns->fn_ref)->val.as.ival : ns->fn_idx : ns->fn_idx;
                                        BuiltinFn fn = NULL;
                                        for (int x = 0; x < sp->count; x++) if (sp->slots[x].idx == idx) { fn = sp->slots[x].fn; break; }
                                        if (!fn) error("fn[%d] not in space '%s'", idx, ns->space_name);
                                        Value fargs[16]; int nf = 0;
                                        if (ns->n_fn_args > 0) {
                                            for (int x = 0; x < ns->n_fn_args && x < 16; x++) {
                                                Binding *b = binding_find(nchild, ns->fn_args[x]);
                                                if (b) fargs[nf++] = b->val;
                                                else { char *end; long iv = strtol(ns->fn_args[x], &end, 10);
                                                       if (*end == '\0' && end != ns->fn_args[x]) fargs[nf++] = val_int(iv);
                                                       else fargs[nf++] = val_str(ns->fn_args[x]); }
                                            }
                                        } else { for (Binding *bb = nchild; bb && nf < 16; bb = bb->next) fargs[nf++] = bb->val; }
                                        nresult = fn(fargs, nf);
                                        binding_add(&nchild, "result", nresult);
                                    } else if (ns->kind == 'o') {
                                        Binding *b = binding_find(nchild, ns->target);
                                        if (!b) error("unknown binding '%s'", ns->target);
                                        nresult = b->val;
                                    }
                                }
                                child_result = nresult;
                                { Binding *rb = binding_find(nchild, "result"); if (rb) rb->val = val_nil(); }
                                binding_add(&child, "result", child_result);
                                binding_free(nchild);
                            }
                        }
                        result = child_result;
                        { Binding *rb = binding_find(child, "result");
                          if (rb) rb->val = val_nil(); }
                        binding_add(&bindings, "result", result);
                        binding_free(child);
                    } else if (s->kind == 'F') {
                        Binding *src = binding_find(bindings, s->for_source);
                        if (!src || src->val.type != VAL_LIST)
                            error("for_each requires a list binding '%s'", s->for_source);
                        Value collect = val_list_new();
                        for (int k = 0; k < src->val.as.lval->count; k++) {
                            Value elem = val_list_get(src->val, k);
                            binding_add(&bindings, s->for_var, val_clone(elem));
                            Value body_result = val_nil();
                            for (int m = 0; m < s->n_for_body; m++) {
                                Step *bs = &s->for_body[m];
                                if (bs->kind == 'c') {
                                    Frame *f = env_get_frame(env, bs->frame_name);
                                    if (!f) error("unknown frame '%s'", bs->frame_name);
                                    Binding *b = binding_find(bindings, bs->target);
                                    if (!b) error("unknown binding '%s'", bs->target);
                                    Value pos_result = f->positive(b->val);
                                    Value neg_result = f->negative(b->val);
                                    if (bs->ctype == 'a') {
                                        if (pos_result.type != VAL_INT || pos_result.as.ival == 0)
                                            error("constraint failed: '%s' as '%s'", bs->target, bs->frame_name);
                                        if (neg_result.type == VAL_INT && neg_result.as.ival != 0)
                                            error("constraint failed: '%s' as '%s'", bs->target, bs->frame_name);
                                    } else {
                                        if (pos_result.type == VAL_INT && pos_result.as.ival != 0)
                                            error("constraint failed: '%s' not_as '%s'", bs->target, bs->frame_name);
                                    }
                                } else if (bs->kind == 'f') {
                                    Space *sp = env_get_space(env, bs->space_name);
                                    if (!sp) error("unknown space '%s'", bs->space_name);
                                    int idx;
                                    if (bs->fn_is_ref) {
                                        Binding *b = binding_find(bindings, bs->fn_ref);
                                        if (!b || b->val.type != VAL_INT) error("non-int fn ref '%s'", bs->fn_ref);
                                        idx = (int)b->val.as.ival;
                                    } else idx = bs->fn_idx;
                                    BuiltinFn fn = NULL;
                                    for (int n = 0; n < sp->count; n++)
                                        if (sp->slots[n].idx == idx) { fn = sp->slots[n].fn; break; }
                                    if (!fn) error("fn[%d] not in space '%s'", idx, bs->space_name);
                                    Value fargs[16]; int nfargs = 0;
                                    if (bs->n_fn_args > 0) {
                                        for (int n = 0; n < bs->n_fn_args && n < 16; n++) {
                                            Binding *b = binding_find(bindings, bs->fn_args[n]);
                                            if (b) { fargs[nfargs++] = b->val; }
                                            else {
                                                char *end; long iv = strtol(bs->fn_args[n], &end, 10);
                                                if (*end == '\0' && end != bs->fn_args[n]) fargs[nfargs++] = val_int(iv);
                                                else fargs[nfargs++] = val_str(bs->fn_args[n]);
                                            }
                                        }
                                    } else {
                                        for (Binding *bb = bindings; bb && nfargs < 16; bb = bb->next)
                                            if (!bs->fn_is_ref || strcmp(bb->name, bs->fn_ref))
                                                fargs[nfargs++] = bb->val;
                                    }
                                    body_result = fn(fargs, nfargs);
                                    binding_add(&bindings, "result", body_result);
                                } else if (bs->kind == 'o') {
                                    Binding *b = binding_find(bindings, bs->target);
                                    if (!b) error("unknown binding '%s'", bs->target);
                                    body_result = b->val;
                                }
                            }
                            val_list_append(&collect, val_clone(body_result));
                        }
                        result = collect;
                        binding_add(&bindings, "result", result);
                    }
                }

                last_result = result;
                val_print(result);
                printf("\n");
                binding_free(bindings);
                break;
            }
        }
    }

    return last_result;
}

/* ===== REPL ===== */

static void repl(Env *env) {
    printf("prima v0.2\n");
    char *line = NULL;
    size_t cap = 0;
    for (;;) {
        printf("» ");
        fflush(stdout);
        if (getline(&line, &cap, stdin) < 0) {
            printf("\n");
            break;
        }
        /* strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        /* skip blank lines */
        if (len == 0) continue;
        /* skip comment-only lines */
        {
            const char *p = line;
            while (*p && (*p == ' ' || *p == '\t')) p++;
            if (*p == '-' && *(p+1) == '-') continue;
        }

        if (setjmp(error_jmp)) {
            fprintf(stderr, "error: %s\n", error_msg);
            continue;
        }

        Lexer *lex = lex_new(line);
        Parser *parser = parser_new(lex, env);
        if (parser->tok.type != TOK_EOF) {
            int nnodes;
            ASTNode *nodes = parse_all(parser, &nnodes);
            interpret(nodes, nnodes, env);
            free_ast(nodes, nnodes);
        }
        lex_free(lex);
    }
    free(line);
}

static void free_env(Env *env) {
    for (int i = 0; i < env->nframes; i++) {
        free(env->frames[i]->name);
        free(env->frames[i]);
    }
    free(env->frames);
    for (int i = 0; i < env->nspaces; i++) {
        free(env->spaces[i]->name);
        free(env->spaces[i]->slots);
        free(env->spaces[i]);
    }
    free(env->spaces);
    for (int i = 0; i < env->npipes; i++) {
        Pipeline *p = env->pipes[i];
        free(p->name);
        for (int j = 0; j < p->nparams; j++) free(p->params[j]);
        free(p->params);
        for (int j = 0; j < p->nsteps; j++) {
            free(p->steps[j].target);
            free(p->steps[j].frame_name);
            free(p->steps[j].fn_ref);
            free(p->steps[j].space_name);
            for (int k = 0; k < p->steps[j].n_fn_args; k++)
                free(p->steps[j].fn_args[k]);
            free(p->steps[j].fn_args);
            free(p->steps[j].pipe_name);
            for (int k = 0; k < p->steps[j].n_pipe_args; k++)
                free(p->steps[j].pipe_args[k]);
            free(p->steps[j].pipe_args);
            free(p->steps[j].for_var);
            free(p->steps[j].for_source);
            if (p->steps[j].for_body) {
                for (int k = 0; k < p->steps[j].n_for_body; k++) {
                    free(p->steps[j].for_body[k].target);
                    free(p->steps[j].for_body[k].frame_name);
                    free(p->steps[j].for_body[k].fn_ref);
                    free(p->steps[j].for_body[k].space_name);
                    for (int m = 0; m < p->steps[j].for_body[k].n_fn_args; m++)
                        free(p->steps[j].for_body[k].fn_args[m]);
                    free(p->steps[j].for_body[k].fn_args);
                }
                free(p->steps[j].for_body);
            }
        }
        free(p->steps);
        free(p);
    }
    free(env->pipes);
    free(env);
}

/* ===== MAIN ===== */

int main(int argc, char **argv) {
    Env *env = env_new();

    if (argc < 2 || !strcmp(argv[1], "-")) {
        repl(env);
        return 0;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *source = malloc((size_t)len + 1);
    fread(source, 1, (size_t)len, fp);
    source[len] = '\0';
    fclose(fp);

    if (setjmp(error_jmp)) {
        fprintf(stderr, "error: %s\n", error_msg);
        free(source);
        return 1;
    }

    Lexer *lex = lex_new(source);
    Parser *parser = parser_new(lex, env);
    int nnodes;
    ASTNode *nodes = parse_all(parser, &nnodes);

    interpret(nodes, nnodes, env);
    free_ast(nodes, nnodes);
    free(source);
    lex_free(lex);
    free_env(env);

    return 0;
}
