#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RETROCC_MAX_PARAMETERS 6u
#define RETROCC_MAX_FRAME_WORDS 0x2000u
#define RETROCC_STACK_LIMIT 0x8000u
#define RETROCC_TEMPORARY_RESERVE 0x400u
#define RETROCC_STACK_GUARD (RETROCC_STACK_LIMIT + RETROCC_TEMPORARY_RESERVE)
#define RETROCC_MAX_PARSE_DEPTH 256u

typedef enum {
    RC_TOKEN_EOF,
    RC_TOKEN_IDENTIFIER,
    RC_TOKEN_NUMBER,
    RC_TOKEN_U16,
    RC_TOKEN_VOID,
    RC_TOKEN_IF,
    RC_TOKEN_ELSE,
    RC_TOKEN_WHILE,
    RC_TOKEN_FOR,
    RC_TOKEN_BREAK,
    RC_TOKEN_CONTINUE,
    RC_TOKEN_RETURN,
    RC_TOKEN_LBRACE,
    RC_TOKEN_RBRACE,
    RC_TOKEN_LPAREN,
    RC_TOKEN_RPAREN,
    RC_TOKEN_LBRACKET,
    RC_TOKEN_RBRACKET,
    RC_TOKEN_SEMICOLON,
    RC_TOKEN_COMMA,
    RC_TOKEN_PLUS,
    RC_TOKEN_MINUS,
    RC_TOKEN_SLASH,
    RC_TOKEN_STAR,
    RC_TOKEN_PERCENT,
    RC_TOKEN_AMPERSAND,
    RC_TOKEN_PIPE,
    RC_TOKEN_CARET,
    RC_TOKEN_TILDE,
    RC_TOKEN_BANG,
    RC_TOKEN_LESS,
    RC_TOKEN_GREATER,
    RC_TOKEN_ASSIGN,
    RC_TOKEN_PLUS_ASSIGN,
    RC_TOKEN_MINUS_ASSIGN,
    RC_TOKEN_SLASH_ASSIGN,
    RC_TOKEN_AMPERSAND_ASSIGN,
    RC_TOKEN_PIPE_ASSIGN,
    RC_TOKEN_CARET_ASSIGN,
    RC_TOKEN_LEFT_SHIFT,
    RC_TOKEN_RIGHT_SHIFT,
    RC_TOKEN_LEFT_SHIFT_ASSIGN,
    RC_TOKEN_RIGHT_SHIFT_ASSIGN,
    RC_TOKEN_LOGICAL_AND,
    RC_TOKEN_LOGICAL_OR,
    RC_TOKEN_EQUAL,
    RC_TOKEN_NOT_EQUAL,
    RC_TOKEN_LESS_EQUAL,
    RC_TOKEN_GREATER_EQUAL,
    RC_TOKEN_UNKNOWN
} RcTokenKind;

typedef enum {
    RC_TYPE_U16,
    RC_TYPE_VOID
} RcType;

typedef enum {
    RC_ENTITY_GLOBAL,
    RC_ENTITY_FUNCTION,
    RC_ENTITY_LOCAL
} RcEntityKind;

typedef enum {
    RC_EXPR_NUMBER,
    RC_EXPR_VARIABLE,
    RC_EXPR_INDEX,
    RC_EXPR_CALL,
    RC_EXPR_UNARY,
    RC_EXPR_BINARY,
    RC_EXPR_ASSIGNMENT
} RcExprKind;

typedef struct RcArenaBlock RcArenaBlock;

typedef struct {
    RcArenaBlock *head;
} RcArena;

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} RcBuffer;

typedef struct {
    RcTokenKind kind;
    char *text;
    size_t line;
    size_t column;
} RcToken;

typedef struct RcEntity RcEntity;

typedef struct RcLoop {
    size_t break_label;
    size_t continue_label;
    const struct RcLoop *previous;
} RcLoop;

typedef struct RcExpr RcExpr;

struct RcEntity {
    RcEntityKind kind;
    char *name;
    RcEntity *next;
    RcType type;
    bool array;
    size_t parameter_count;
    size_t element_count;
    size_t offset;
    size_t scope_depth;
    uint16_t *initializers;
    size_t initializer_count;
};

struct RcExpr {
    RcExprKind kind;
    RcTokenKind op;
    uint16_t value;
    RcEntity *entity;
    RcExpr *left;
    RcExpr *right;
    RcExpr **arguments;
    size_t argument_count;
    size_t depth;
    size_t line;
    size_t column;
};

typedef struct {
    RcArena arena;
    RcBuffer source;
    RcBuffer output;
    const char *path;
    RcToken *tokens;
    size_t token_count;
    size_t token_capacity;
    size_t position;
    RcEntity *globals;
    RcEntity *functions;
    RcEntity *locals;
    RcEntity *current_function;
    const RcLoop *loop;
    size_t scope_depth;
    size_t frame_words;
    size_t temporary_depth;
    size_t parse_depth;
    size_t next_label;
    bool uses_array_index;
    bool failed;
} RcCompiler;

struct RcArenaBlock {
    RcArenaBlock *next;
    size_t used;
    size_t capacity;
    max_align_t data[1];
};

static void rc_arena_clear(RcArena *arena)
{
    RcArenaBlock *block = arena->head;

    while (block != NULL) {
        RcArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
}

static void *rc_arena_allocate(RcArena *arena, size_t size)
{
    RcArenaBlock *block;
    size_t capacity;
    size_t alignment = _Alignof(max_align_t);
    size_t aligned_used;
    void *result;

    if (size == 0u) {
        size = 1u;
    }
    block = arena->head;
    if (block != NULL && block->used <= SIZE_MAX - (alignment - 1u)) {
        aligned_used = (block->used + alignment - 1u) & ~(alignment - 1u);
        if (aligned_used <= block->capacity && size <= block->capacity - aligned_used) {
            result = (unsigned char *)block->data + aligned_used;
            block->used = aligned_used + size;
            return result;
        }
    }
    capacity = size > 4096u ? size : 4096u;
    if (capacity < size || capacity > SIZE_MAX - sizeof(RcArenaBlock)) {
        return NULL;
    }
    block = malloc(sizeof(RcArenaBlock) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = arena->head;
    block->used = size;
    block->capacity = capacity;
    arena->head = block;
    return block->data;
}

static char *rc_arena_copy(RcArena *arena, const char *text)
{
    size_t length = strlen(text);
    char *copy;

    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = rc_arena_allocate(arena, length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

static char *rc_arena_copy_n(RcArena *arena, const char *text, size_t length)
{
    char *copy;

    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = rc_arena_allocate(arena, length + 1u);
    if (copy != NULL) {
        if (length != 0u) {
            memcpy(copy, text, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

static void rc_buffer_clear(RcBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool rc_buffer_reserve(RcBuffer *buffer, size_t additional)
{
    size_t needed;
    size_t capacity;
    unsigned char *data;

    if (additional > SIZE_MAX - buffer->length) {
        return false;
    }
    needed = buffer->length + additional;
    if (needed == SIZE_MAX) {
        return false;
    }
    if (needed + 1u <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    while (capacity < needed + 1u) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed + 1u;
            break;
        }
        capacity *= 2u;
    }
    data = realloc(buffer->data, capacity);
    if (data == NULL) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool rc_buffer_append(RcBuffer *buffer, const void *data, size_t length)
{
    if (!rc_buffer_reserve(buffer, length)) {
        return false;
    }
    if (length != 0u) {
        memcpy(buffer->data + buffer->length, data, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = 0u;
    return true;
}

static bool rc_read_source(const char *path, RcBuffer *source, const char **message)
{
    FILE *file = fopen(path, "rb");
    unsigned char chunk[4096];
    size_t count;
    size_t index;
    bool read_error;
    bool close_error;

    if (file == NULL) {
        *message = strerror(errno);
        return false;
    }
    while ((count = fread(chunk, 1u, sizeof(chunk), file)) != 0u) {
        if (!rc_buffer_append(source, chunk, count)) {
            fclose(file);
            *message = "out of memory";
            return false;
        }
    }
    read_error = ferror(file) != 0;
    close_error = fclose(file) != 0;
    if (read_error || close_error) {
        *message = strerror(errno);
        return false;
    }
    for (index = 0u; index < source->length; ++index) {
        if (source->data[index] == 0u) {
            *message = "source contains NUL";
            return false;
        }
    }
    return true;
}

static bool rc_is_identifier_start(unsigned char value)
{
    return value == '_' || (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static bool rc_is_identifier_char(unsigned char value)
{
    return rc_is_identifier_start(value) || (value >= '0' && value <= '9');
}

static bool rc_is_digit(unsigned char value)
{
    return value >= '0' && value <= '9';
}

static bool rc_text_is(const char *text, const char *expected)
{
    return strcmp(text, expected) == 0;
}

static char rc_ascii_upper(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }
    return value;
}

static bool rc_label_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (rc_ascii_upper(*left) != rc_ascii_upper(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool rc_keyword_kind(const char *text, RcTokenKind *kind)
{
    static const struct {
        const char *text;
        RcTokenKind kind;
    } keywords[] = {
        {"u16", RC_TOKEN_U16}, {"void", RC_TOKEN_VOID}, {"if", RC_TOKEN_IF},
        {"else", RC_TOKEN_ELSE}, {"while", RC_TOKEN_WHILE}, {"for", RC_TOKEN_FOR},
        {"break", RC_TOKEN_BREAK}, {"continue", RC_TOKEN_CONTINUE},
        {"return", RC_TOKEN_RETURN}
    };
    size_t index;

    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if (rc_text_is(text, keywords[index].text)) {
            *kind = keywords[index].kind;
            return true;
        }
    }
    return false;
}

static bool rc_token_append(RcCompiler *compiler, RcTokenKind kind, char *text,
                            size_t line, size_t column)
{
    RcToken *tokens;
    size_t capacity;

    if (compiler->token_count == compiler->token_capacity) {
        capacity = compiler->token_capacity == 0u ? 128u : compiler->token_capacity * 2u;
        if (capacity < compiler->token_capacity || capacity > SIZE_MAX / sizeof(RcToken)) {
            return false;
        }
        tokens = realloc(compiler->tokens, capacity * sizeof(RcToken));
        if (tokens == NULL) {
            return false;
        }
        compiler->tokens = tokens;
        compiler->token_capacity = capacity;
    }
    compiler->tokens[compiler->token_count].kind = kind;
    compiler->tokens[compiler->token_count].text = text;
    compiler->tokens[compiler->token_count].line = line;
    compiler->tokens[compiler->token_count].column = column;
    ++compiler->token_count;
    return true;
}

static RcTokenKind rc_punctuation_kind(const unsigned char *data, size_t length,
                                       size_t *consumed)
{
    if (length >= 3u && data[0] == '<' && data[1] == '<' && data[2] == '=') {
        *consumed = 3u;
        return RC_TOKEN_LEFT_SHIFT_ASSIGN;
    }
    if (length >= 3u && data[0] == '>' && data[1] == '>' && data[2] == '=') {
        *consumed = 3u;
        return RC_TOKEN_RIGHT_SHIFT_ASSIGN;
    }
    if (length >= 2u && data[0] == '<' && data[1] == '<') {
        *consumed = 2u;
        return RC_TOKEN_LEFT_SHIFT;
    }
    if (length >= 2u && data[0] == '>' && data[1] == '>') {
        *consumed = 2u;
        return RC_TOKEN_RIGHT_SHIFT;
    }
    if (length >= 2u && data[0] == '/' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_SLASH_ASSIGN;
    }
    if (length >= 2u && data[0] == '+' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_PLUS_ASSIGN;
    }
    if (length >= 2u && data[0] == '-' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_MINUS_ASSIGN;
    }
    if (length >= 2u && data[0] == '&' && data[1] == '&') {
        *consumed = 2u;
        return RC_TOKEN_LOGICAL_AND;
    }
    if (length >= 2u && data[0] == '&' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_AMPERSAND_ASSIGN;
    }
    if (length >= 2u && data[0] == '|' && data[1] == '|') {
        *consumed = 2u;
        return RC_TOKEN_LOGICAL_OR;
    }
    if (length >= 2u && data[0] == '|' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_PIPE_ASSIGN;
    }
    if (length >= 2u && data[0] == '^' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_CARET_ASSIGN;
    }
    if (length >= 2u && data[0] == '=' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_EQUAL;
    }
    if (length >= 2u && data[0] == '!' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_NOT_EQUAL;
    }
    if (length >= 2u && data[0] == '<' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_LESS_EQUAL;
    }
    if (length >= 2u && data[0] == '>' && data[1] == '=') {
        *consumed = 2u;
        return RC_TOKEN_GREATER_EQUAL;
    }
    *consumed = 1u;
    switch (data[0]) {
    case '{': return RC_TOKEN_LBRACE;
    case '}': return RC_TOKEN_RBRACE;
    case '(': return RC_TOKEN_LPAREN;
    case ')': return RC_TOKEN_RPAREN;
    case '[': return RC_TOKEN_LBRACKET;
    case ']': return RC_TOKEN_RBRACKET;
    case ';': return RC_TOKEN_SEMICOLON;
    case ',': return RC_TOKEN_COMMA;
    case '+': return RC_TOKEN_PLUS;
    case '-': return RC_TOKEN_MINUS;
    case '/': return RC_TOKEN_SLASH;
    case '*': return RC_TOKEN_STAR;
    case '%': return RC_TOKEN_PERCENT;
    case '&': return RC_TOKEN_AMPERSAND;
    case '|': return RC_TOKEN_PIPE;
    case '^': return RC_TOKEN_CARET;
    case '~': return RC_TOKEN_TILDE;
    case '!': return RC_TOKEN_BANG;
    case '<': return RC_TOKEN_LESS;
    case '>': return RC_TOKEN_GREATER;
    case '=': return RC_TOKEN_ASSIGN;
    default: return RC_TOKEN_UNKNOWN;
    }
}

static void rc_lexer_error(RcCompiler *compiler, size_t line, size_t column,
                           const char *message)
{
    if (!compiler->failed) {
        fprintf(stderr, "%s:%zu:%zu: error: %s\n", compiler->path, line, column, message);
    }
    compiler->failed = true;
}

static bool rc_lex_source(RcCompiler *compiler, size_t *line, size_t *column)
{
    char *text;

    while (compiler->position < compiler->source.length) {
        size_t start;
        size_t token_line;
        size_t token_column;
        unsigned char value;
        char *text;
        RcTokenKind kind;

        for (;;) {
            if (compiler->position >= compiler->source.length) {
                goto done;
            }
            value = compiler->source.data[compiler->position];
            if (value == ' ' || value == '\t' || value == '\v' || value == '\f') {
                ++compiler->position;
                ++*column;
                continue;
            }
            if (value == '\r' || value == '\n') {
                ++compiler->position;
                if (value == '\r' && compiler->position < compiler->source.length &&
                    compiler->source.data[compiler->position] == '\n') {
                    ++compiler->position;
                }
                ++*line;
                *column = 1u;
                continue;
            }
            if (value == '/' && compiler->position + 1u < compiler->source.length &&
                compiler->source.data[compiler->position + 1u] == '/') {
                compiler->position += 2u;
                *column += 2u;
                while (compiler->position < compiler->source.length &&
                       compiler->source.data[compiler->position] != '\r' &&
                       compiler->source.data[compiler->position] != '\n') {
                    ++compiler->position;
                    ++*column;
                }
                continue;
            }
            if (value == '/' && compiler->position + 1u < compiler->source.length &&
                compiler->source.data[compiler->position + 1u] == '*') {
                size_t comment_line = *line;
                size_t comment_column = *column;
                bool closed = false;
                compiler->position += 2u;
                *column += 2u;
                while (compiler->position < compiler->source.length) {
                    if (compiler->source.data[compiler->position] == '*' &&
                        compiler->position + 1u < compiler->source.length &&
                        compiler->source.data[compiler->position + 1u] == '/') {
                        compiler->position += 2u;
                        *column += 2u;
                        closed = true;
                        break;
                    }
                    if (compiler->source.data[compiler->position] == '\r' ||
                        compiler->source.data[compiler->position] == '\n') {
                        unsigned char newline = compiler->source.data[compiler->position++];
                        if (newline == '\r' && compiler->position < compiler->source.length &&
                            compiler->source.data[compiler->position] == '\n') {
                            ++compiler->position;
                        }
                        ++*line;
                        *column = 1u;
                    } else {
                        ++compiler->position;
                        ++*column;
                    }
                }
                if (!closed) {
                    rc_lexer_error(compiler, comment_line, comment_column,
                                   "unterminated block comment");
                    return false;
                }
                continue;
            }
            break;
        }

        start = compiler->position;
        token_line = *line;
        token_column = *column;
        if (rc_is_identifier_start(value)) {
            while (compiler->position < compiler->source.length &&
                   rc_is_identifier_char(compiler->source.data[compiler->position])) {
                ++compiler->position;
                ++*column;
            }
            text = rc_arena_copy_n(&compiler->arena,
                                    (const char *)compiler->source.data + start,
                                    compiler->position - start);
            if (text == NULL || !rc_keyword_kind(text, &kind)) {
                kind = RC_TOKEN_IDENTIFIER;
            }
        } else if (rc_is_digit(value)) {
            while (compiler->position < compiler->source.length &&
                   rc_is_identifier_char(compiler->source.data[compiler->position])) {
                ++compiler->position;
                ++*column;
            }
            text = rc_arena_copy_n(&compiler->arena,
                                    (const char *)compiler->source.data + start,
                                    compiler->position - start);
            kind = RC_TOKEN_NUMBER;
        } else {
            size_t consumed;
            kind = rc_punctuation_kind(compiler->source.data + compiler->position,
                                       compiler->source.length - compiler->position,
                                       &consumed);
            while (consumed-- != 0u) {
                ++compiler->position;
                ++*column;
            }
            text = rc_arena_copy_n(&compiler->arena,
                                    (const char *)compiler->source.data + start,
                                    compiler->position - start);
        }
        if (text == NULL || !rc_token_append(compiler, kind, text, token_line, token_column)) {
            rc_lexer_error(compiler, token_line, token_column, "out of memory");
            return false;
        }
    }

done:
    text = rc_arena_copy(&compiler->arena, "");
    if (text == NULL || !rc_token_append(compiler, RC_TOKEN_EOF, text, *line, *column)) {
        return false;
    }
    compiler->position = 0u;
    return true;
}

static RcToken *rc_peek(RcCompiler *compiler, size_t offset)
{
    size_t index = compiler->position + offset;
    if (index >= compiler->token_count) {
        index = compiler->token_count - 1u;
    }
    return &compiler->tokens[index];
}

static RcToken *rc_take(RcCompiler *compiler)
{
    RcToken *token = rc_peek(compiler, 0u);
    if (compiler->position + 1u < compiler->token_count) {
        ++compiler->position;
    }
    return token;
}

static bool rc_accept(RcCompiler *compiler, RcTokenKind kind)
{
    if (rc_peek(compiler, 0u)->kind == kind) {
        rc_take(compiler);
        return true;
    }
    return false;
}

static void rc_error(RcCompiler *compiler, RcToken *token, const char *format, ...)
{
    va_list arguments;

    if (compiler->failed) {
        return;
    }
    fprintf(stderr, "%s:%zu:%zu: error: ", compiler->path,
            token != NULL ? token->line : 1u, token != NULL ? token->column : 1u);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    compiler->failed = true;
}

static bool rc_expect(RcCompiler *compiler, RcTokenKind kind, const char *description)
{
    RcToken *token = rc_peek(compiler, 0u);
    if (rc_accept(compiler, kind)) {
        return true;
    }
    rc_error(compiler, token, "expected %s, found '%s'", description, token->text);
    return false;
}

static bool rc_token_is_type(RcToken *token)
{
    return token->kind == RC_TOKEN_U16 || token->kind == RC_TOKEN_VOID;
}

static bool rc_number_value(const char *text, uint16_t *value)
{
    size_t position = 0u;
    size_t length = strlen(text);
    unsigned int base = 10u;
    uint32_t result = 0u;
    bool digit_seen = false;
    bool previous_underscore = false;

    if (length >= 2u && text[0] == '0' && text[1] == 'x') {
        base = 16u;
        position = 2u;
    } else if (length >= 2u && text[0] == '0' && text[1] == 'b') {
        base = 2u;
        position = 2u;
    } else if (length >= 2u && text[0] == '0' && text[1] == 'o') {
        base = 8u;
        position = 2u;
    }
    for (; position < length; ++position) {
        char character = text[position];
        unsigned int digit;

        if (character == '_') {
            if (previous_underscore || !digit_seen || position + 1u == length) {
                return false;
            }
            previous_underscore = true;
            continue;
        }
        previous_underscore = false;
        if (character >= '0' && character <= '9') {
            digit = (unsigned int)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (unsigned int)(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = (unsigned int)(character - 'A' + 10);
        } else {
            return false;
        }
        if (digit >= base || result > (0xffffu - digit) / base) {
            return false;
        }
        result = result * base + digit;
        digit_seen = true;
    }
    if (!digit_seen) {
        return false;
    }
    *value = (uint16_t)result;
    return true;
}

static RcEntity *rc_find_entity(RcEntity *list, const char *name)
{
    for (; list != NULL; list = list->next) {
        if (strcmp(list->name, name) == 0) {
            return list;
        }
    }
    return NULL;
}

static RcEntity *rc_find_label_entity(RcEntity *list, const char *name)
{
    for (; list != NULL; list = list->next) {
        if (rc_label_name_equal(list->name, name)) {
            return list;
        }
    }
    return NULL;
}

static void rc_append_entity(RcEntity **list, RcEntity *entity)
{
    RcEntity *current;

    if (*list == NULL) {
        *list = entity;
        return;
    }
    current = *list;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = entity;
}

static RcEntity *rc_find_local(RcCompiler *compiler, const char *name)
{
    RcEntity *local;
    for (local = compiler->locals; local != NULL; local = local->next) {
        if (local->scope_depth <= compiler->scope_depth && strcmp(local->name, name) == 0) {
            return local;
        }
    }
    return NULL;
}

static RcEntity *rc_lookup(RcCompiler *compiler, const char *name)
{
    RcEntity *entity = rc_find_local(compiler, name);
    if (entity != NULL) return entity;
    entity = rc_find_entity(compiler->globals, name);
    if (entity != NULL) return entity;
    return rc_find_entity(compiler->functions, name);
}

static bool rc_reserved_name(const char *name)
{
    return strncmp(name, "__", 2u) == 0;
}

static RcExpr *rc_expr_new(RcCompiler *compiler, RcExprKind kind, RcToken *token)
{
    RcExpr *expression = rc_arena_allocate(&compiler->arena, sizeof(RcExpr));
    if (expression != NULL) {
        memset(expression, 0, sizeof(*expression));
        expression->kind = kind;
        expression->depth = 1u;
        expression->line = token->line;
        expression->column = token->column;
    }
    return expression;
}

static bool rc_set_expression_depth(RcCompiler *compiler, RcExpr *expression,
                                    const RcExpr *left, const RcExpr *right)
{
    size_t left_depth = left != NULL ? left->depth : 0u;
    size_t right_depth = right != NULL ? right->depth : 0u;
    size_t depth = left_depth > right_depth ? left_depth : right_depth;

    if (depth == SIZE_MAX || depth + 1u > RETROCC_MAX_PARSE_DEPTH) {
        rc_error(compiler, NULL, "expression nesting exceeds %u levels",
                 RETROCC_MAX_PARSE_DEPTH);
        return false;
    }
    expression->depth = depth + 1u;
    return true;
}

static bool rc_expr_is_void(const RcExpr *expression)
{
    return expression->kind == RC_EXPR_CALL && expression->entity->type == RC_TYPE_VOID;
}

static bool rc_expr_is_lvalue(const RcExpr *expression)
{
    if (expression->kind == RC_EXPR_INDEX) {
        return true;
    }
    return expression->kind == RC_EXPR_VARIABLE &&
           expression->entity->kind != RC_ENTITY_FUNCTION &&
           !expression->entity->array;
}

static void rc_emit(RcCompiler *compiler, const char *format, ...)
{
    va_list arguments;
    va_list measuring;
    int needed;

    if (compiler->failed) {
        return;
    }
    va_start(arguments, format);
    va_copy(measuring, arguments);
    needed = vsnprintf(NULL, 0, format, measuring);
    va_end(measuring);
    va_end(arguments);
    if (needed < 0 || !rc_buffer_reserve(&compiler->output, (size_t)needed)) {
        rc_error(compiler, NULL, "unable to allocate output buffer");
        return;
    }
    va_start(arguments, format);
    needed = vsnprintf((char *)compiler->output.data + compiler->output.length,
                      compiler->output.capacity - compiler->output.length,
                      format, arguments);
    va_end(arguments);
    if (needed < 0) {
        rc_error(compiler, NULL, "unable to format output");
        return;
    }
    compiler->output.length += (size_t)needed;
}

static size_t rc_new_label(RcCompiler *compiler)
{
    return compiler->next_label++;
}

static void rc_label(RcCompiler *compiler, size_t label)
{
    rc_emit(compiler, "__cc_%06zu:\n", label);
}

static void rc_ldi(RcCompiler *compiler, unsigned int value, const char *reg)
{
    rc_emit(compiler, "LDI %04Xh, %s\n", value, reg);
}

static void rc_mov(RcCompiler *compiler, const char *source, const char *destination)
{
    rc_emit(compiler, "MOV %s, %s\n", source, destination);
}

static void rc_push_temporary(RcCompiler *compiler)
{
    if (compiler->temporary_depth >= RETROCC_TEMPORARY_RESERVE) {
        rc_error(compiler, NULL, "expression temporary stack exceeds %u words",
                 RETROCC_TEMPORARY_RESERVE);
        return;
    }
    rc_emit(compiler, "PUSH RS\n");
    ++compiler->temporary_depth;
}

static void rc_pop_temporary(RcCompiler *compiler, const char *register_name)
{
    if (compiler->temporary_depth == 0u) {
        rc_error(compiler, NULL, "internal temporary stack underflow");
        return;
    }
    rc_emit(compiler, "POP %s\n", register_name);
    --compiler->temporary_depth;
}

static void rc_gen_expression(RcCompiler *compiler, RcExpr *expression);
static bool rc_parse_expression(RcCompiler *compiler, RcExpr **expression);
static bool rc_parse_statement(RcCompiler *compiler);

static bool rc_constant(RcCompiler *compiler, uint16_t *value)
{
    RcToken *token = rc_peek(compiler, 0u);

    if (!rc_expect(compiler, RC_TOKEN_NUMBER, "integer constant")) {
        return false;
    }
    if (!rc_number_value(token->text, value)) {
        rc_error(compiler, token, "integer constant '%s' is outside 0..65535", token->text);
        return false;
    }
    return true;
}

static bool rc_parse_array_length(RcCompiler *compiler, size_t *length)
{
    RcToken *token;
    uint16_t value;

    if (!rc_expect(compiler, RC_TOKEN_LBRACKET, "'['")) {
        return false;
    }
    token = rc_peek(compiler, 0u);
    if (!rc_constant(compiler, &value)) {
        return false;
    }
    if (value == 0u) {
        rc_error(compiler, token, "array length must be greater than zero");
        return false;
    }
    if (!rc_expect(compiler, RC_TOKEN_RBRACKET, "']'")) {
        return false;
    }
    *length = value;
    return true;
}

static bool rc_collect_global_initializer(RcCompiler *compiler, RcEntity *entity)
{
    size_t index;

    if (!rc_accept(compiler, RC_TOKEN_ASSIGN)) {
        return true;
    }
    if (!entity->array) {
        uint16_t value;
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACE) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "scalar initializer must be one integer constant");
            return false;
        }
        if (!rc_constant(compiler, &value)) {
            return false;
        }
        entity->initializers = rc_arena_allocate(&compiler->arena, sizeof(uint16_t));
        if (entity->initializers == NULL) {
            rc_error(compiler, rc_peek(compiler, 0u), "out of memory");
            return false;
        }
        entity->initializers[0] = value;
        entity->initializer_count = 1u;
        return true;
    }
    if (!rc_expect(compiler, RC_TOKEN_LBRACE, "'{'")) {
        return false;
    }
    entity->initializers = rc_arena_allocate(&compiler->arena,
                                             entity->element_count * sizeof(uint16_t));
    if (entity->initializers == NULL) {
        rc_error(compiler, rc_peek(compiler, 0u), "out of memory");
        return false;
    }
    if (rc_accept(compiler, RC_TOKEN_RBRACE)) {
        return true;
    }
    for (index = 0u;; ++index) {
        uint16_t value;
        if (!rc_constant(compiler, &value)) {
            return false;
        }
        if (index >= entity->element_count) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "array '%s' has more than %zu initializers",
                     entity->name, entity->element_count);
            return false;
        }
        entity->initializers[index] = value;
        entity->initializer_count = index + 1u;
        if (rc_accept(compiler, RC_TOKEN_COMMA)) {
            if (rc_accept(compiler, RC_TOKEN_RBRACE)) {
                return true;
            }
            continue;
        }
        break;
    }
    return rc_expect(compiler, RC_TOKEN_RBRACE, "'}'");
}

static bool rc_collect_parameters(RcCompiler *compiler, size_t *count)
{
    if (!rc_expect(compiler, RC_TOKEN_LPAREN, "'('")) {
        return false;
    }
    *count = 0u;
    if (rc_accept(compiler, RC_TOKEN_RPAREN)) {
        return true;
    }
    if (rc_peek(compiler, 0u)->kind == RC_TOKEN_VOID &&
        rc_peek(compiler, 1u)->kind == RC_TOKEN_RPAREN) {
        rc_take(compiler);
        rc_take(compiler);
        return true;
    }
    for (;;) {
        if (!rc_expect(compiler, RC_TOKEN_U16, "parameter type 'u16'")) {
            return false;
        }
        if (!rc_expect(compiler, RC_TOKEN_IDENTIFIER, "parameter name")) {
            return false;
        }
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
            rc_error(compiler, rc_peek(compiler, 0u), "array parameters are not supported");
            return false;
        }
        if (*count >= RETROCC_MAX_PARAMETERS) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "a function may have at most %u parameters", RETROCC_MAX_PARAMETERS);
            return false;
        }
        ++*count;
        if (rc_accept(compiler, RC_TOKEN_COMMA)) {
            continue;
        }
        break;
    }
    return rc_expect(compiler, RC_TOKEN_RPAREN, "')'");
}

static bool rc_skip_function_body(RcCompiler *compiler)
{
    size_t depth = 1u;

    if (!rc_expect(compiler, RC_TOKEN_LBRACE, "'{'")) {
        return false;
    }
    while (depth != 0u) {
        RcToken *token = rc_take(compiler);
        if (token->kind == RC_TOKEN_EOF) {
            rc_error(compiler, token, "unterminated function body");
            return false;
        }
        if (token->kind == RC_TOKEN_LBRACE) {
            ++depth;
        } else if (token->kind == RC_TOKEN_RBRACE) {
            --depth;
        }
    }
    return true;
}

static bool rc_collect_entities(RcCompiler *compiler)
{
    compiler->position = 0u;
    while (rc_peek(compiler, 0u)->kind != RC_TOKEN_EOF) {
        RcToken *type_token = rc_peek(compiler, 0u);
        RcToken *name_token;
        RcEntity *entity;

        if (!rc_token_is_type(type_token)) {
            rc_error(compiler, type_token, "expected top-level type 'u16' or 'void'");
            return false;
        }
        rc_take(compiler);
        name_token = rc_peek(compiler, 0u);
        if (!rc_expect(compiler, RC_TOKEN_IDENTIFIER, "declaration name")) {
            return false;
        }
        if (rc_reserved_name(name_token->text)) {
            rc_error(compiler, name_token, "identifier '%s' is reserved", name_token->text);
            return false;
        }
        entity = rc_arena_allocate(&compiler->arena, sizeof(RcEntity));
        if (entity == NULL) {
            rc_error(compiler, name_token, "out of memory");
            return false;
        }
        memset(entity, 0, sizeof(*entity));
        entity->name = rc_arena_copy(&compiler->arena, name_token->text);
        if (entity->name == NULL) {
            rc_error(compiler, name_token, "out of memory");
            return false;
        }
        entity->type = type_token->kind == RC_TOKEN_U16 ? RC_TYPE_U16 : RC_TYPE_VOID;
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LPAREN) {
            size_t count;
            if (!rc_collect_parameters(compiler, &count)) {
                return false;
            }
            if (rc_find_label_entity(compiler->globals, entity->name) != NULL ||
                rc_find_label_entity(compiler->functions, entity->name) != NULL) {
                rc_error(compiler, name_token, "duplicate symbol '%s'", entity->name);
                return false;
            }
            entity->kind = RC_ENTITY_FUNCTION;
            entity->parameter_count = count;
            entity->next = compiler->functions;
            compiler->functions = entity;
            if (!rc_skip_function_body(compiler)) {
                return false;
            }
            continue;
        }
        if (entity->type != RC_TYPE_U16) {
            rc_error(compiler, name_token, "global variables must have type u16");
            return false;
        }
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
            if (!rc_parse_array_length(compiler, &entity->element_count)) {
                return false;
            }
            entity->array = true;
            if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
                rc_error(compiler, rc_peek(compiler, 0u),
                         "only one-dimensional u16 arrays are supported");
                return false;
            }
        } else {
            entity->element_count = 1u;
        }
        if (!rc_collect_global_initializer(compiler, entity)) {
            return false;
        }
        if (!rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'")) {
            return false;
        }
        if (rc_find_label_entity(compiler->globals, entity->name) != NULL ||
            rc_find_label_entity(compiler->functions, entity->name) != NULL) {
            rc_error(compiler, name_token, "duplicate symbol '%s'", entity->name);
            return false;
        }
        entity->kind = RC_ENTITY_GLOBAL;
        rc_append_entity(&compiler->globals, entity);
    }
    {
        RcEntity *main_function = rc_find_entity(compiler->functions, "main");
        if (main_function == NULL) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "program must define u16 main(void)");
            return false;
        }
        if (main_function->type != RC_TYPE_U16 || main_function->parameter_count != 0u) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "main must have signature u16 main(void)");
            return false;
        }
    }
    return true;
}

static bool rc_add_local(RcCompiler *compiler, RcEntity *local, RcToken *token)
{
    RcEntity *current;
    size_t words = local->array ? local->element_count : 1u;

    for (current = compiler->locals; current != NULL; current = current->next) {
        if (current->scope_depth <= compiler->scope_depth &&
            strcmp(current->name, local->name) == 0) {
            rc_error(compiler, token, "duplicate local '%s'", local->name);
            return false;
        }
    }
    if (words == 0u || words > RETROCC_MAX_FRAME_WORDS - compiler->frame_words) {
        rc_error(compiler, token, "function stack frame exceeds %u words",
                 RETROCC_MAX_FRAME_WORDS);
        return false;
    }
    local->kind = RC_ENTITY_LOCAL;
    local->offset = compiler->frame_words;
    compiler->frame_words += words;
    local->scope_depth = compiler->scope_depth;
    local->next = compiler->locals;
    compiler->locals = local;
    return true;
}

static RcExpr *rc_binary_expression(RcCompiler *compiler, RcToken *token, RcExpr *left,
                                     RcExpr *right)
{
    RcExpr *expression;

    if (rc_expr_is_void(left) || rc_expr_is_void(right)) {
        rc_error(compiler, token, "operator requires u16 operands");
        return NULL;
    }
    expression = rc_expr_new(compiler, RC_EXPR_BINARY, token);
    if (expression == NULL) {
        rc_error(compiler, token, "out of memory");
        return NULL;
    }
    expression->op = token->kind;
    expression->left = left;
    expression->right = right;
    if (!rc_set_expression_depth(compiler, expression, left, right)) {
        return NULL;
    }
    return expression;
}

static bool rc_parse_arguments(RcCompiler *compiler, RcExpr *call)
{
    if (rc_accept(compiler, RC_TOKEN_RPAREN)) {
        return true;
    }
    for (;;) {
        RcExpr *argument;
        RcExpr **arguments;

        if (!rc_parse_expression(compiler, &argument)) {
            return false;
        }
        if (rc_expr_is_void(argument)) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "void function result cannot be used as an argument");
            return false;
        }
        if (call->argument_count >= RETROCC_MAX_PARAMETERS) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "a call may pass at most %u arguments", RETROCC_MAX_PARAMETERS);
            return false;
        }
        arguments = rc_arena_allocate(&compiler->arena,
                                      (call->argument_count + 1u) * sizeof(RcExpr *));
        if (arguments == NULL) {
            rc_error(compiler, rc_peek(compiler, 0u), "out of memory");
            return false;
        }
        if (call->argument_count != 0u) {
            memcpy(arguments, call->arguments, call->argument_count * sizeof(RcExpr *));
        }
        arguments[call->argument_count++] = argument;
        call->arguments = arguments;
        if (rc_accept(compiler, RC_TOKEN_COMMA)) {
            continue;
        }
        break;
    }
    return rc_expect(compiler, RC_TOKEN_RPAREN, "')'");
}

static bool rc_parse_primary(RcCompiler *compiler, RcExpr **result)
{
    RcToken *token = rc_peek(compiler, 0u);

    if (rc_accept(compiler, RC_TOKEN_NUMBER)) {
        RcExpr *expression;
        uint16_t value;
        if (!rc_number_value(token->text, &value)) {
            rc_error(compiler, token, "integer constant '%s' is outside 0..65535", token->text);
            return false;
        }
        expression = rc_expr_new(compiler, RC_EXPR_NUMBER, token);
        if (expression == NULL) {
            rc_error(compiler, token, "out of memory");
            return false;
        }
        expression->value = value;
        *result = expression;
        return true;
    }
    if (rc_accept(compiler, RC_TOKEN_LPAREN)) {
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_IDENTIFIER &&
            rc_peek(compiler, 1u)->kind == RC_TOKEN_RPAREN &&
            rc_peek(compiler, 2u)->kind == RC_TOKEN_LBRACKET) {
            RcToken *name_token = rc_take(compiler);
            RcEntity *entity = rc_lookup(compiler, name_token->text);
            RcExpr *expression;

            if (entity == NULL) {
                rc_error(compiler, name_token, "unknown identifier '%s'", name_token->text);
                return false;
            }
            if (entity->kind != RC_ENTITY_GLOBAL && entity->kind != RC_ENTITY_LOCAL) {
                rc_error(compiler, name_token, "parenthesized value is not an array");
                return false;
            }
            if (!entity->array) {
                rc_error(compiler, name_token, "parenthesized value is not an array");
                return false;
            }
            expression = rc_expr_new(compiler, RC_EXPR_VARIABLE, name_token);
            if (expression == NULL) {
                rc_error(compiler, name_token, "out of memory");
                return false;
            }
            expression->entity = entity;
            if (!rc_expect(compiler, RC_TOKEN_RPAREN, "')'")) {
                return false;
            }
            *result = expression;
            return true;
        }
        if (!rc_parse_expression(compiler, result)) {
            return false;
        }
        return rc_expect(compiler, RC_TOKEN_RPAREN, "')'");
    }
    if (rc_accept(compiler, RC_TOKEN_IDENTIFIER)) {
        RcEntity *entity = rc_lookup(compiler, token->text);
        RcExpr *expression;

        if (entity == NULL) {
            rc_error(compiler, token, "unknown identifier '%s'", token->text);
            return false;
        }
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LPAREN) {
            if (entity->kind != RC_ENTITY_FUNCTION) {
                rc_error(compiler, token, "called value is not a function");
                return false;
            }
            rc_take(compiler);
            expression = rc_expr_new(compiler, RC_EXPR_CALL, token);
            if (expression == NULL) {
                rc_error(compiler, token, "out of memory");
                return false;
            }
            expression->entity = entity;
            if (!rc_parse_arguments(compiler, expression)) {
                return false;
            }
            {
                size_t argument_index;
                RcExpr *deepest = NULL;
                for (argument_index = 0u; argument_index < expression->argument_count;
                     ++argument_index) {
                    if (deepest == NULL ||
                        expression->arguments[argument_index]->depth > deepest->depth) {
                        deepest = expression->arguments[argument_index];
                    }
                }
                if (!rc_set_expression_depth(compiler, expression, deepest, NULL)) {
                    return false;
                }
            }
            if (expression->argument_count != entity->parameter_count) {
                rc_error(compiler, token, "function '%s' expects %zu arguments, got %zu",
                         entity->name, entity->parameter_count, expression->argument_count);
                return false;
            }
            *result = expression;
            return true;
        }
        if (entity->kind == RC_ENTITY_FUNCTION) {
            rc_error(compiler, token, "function '%s' requires a call", entity->name);
            return false;
        }
        if (entity->type != RC_TYPE_U16) {
            rc_error(compiler, token, "'%s' does not have a u16 value", entity->name);
            return false;
        }
        expression = rc_expr_new(compiler, RC_EXPR_VARIABLE, token);
        if (expression == NULL) {
            rc_error(compiler, token, "out of memory");
            return false;
        }
        expression->entity = entity;
        *result = expression;
        return true;
    }
    rc_error(compiler, token, "expected expression, found '%s'", token->text);
    return false;
}

static bool rc_parse_postfix(RcCompiler *compiler, RcExpr **result)
{
    RcExpr *base;

    if (!rc_parse_primary(compiler, &base)) {
        return false;
    }
    while (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
        RcToken *token = rc_take(compiler);
        RcExpr *index;
        RcExpr *expression;

        if (base->kind != RC_EXPR_VARIABLE || !base->entity->array) {
            rc_error(compiler, token, "indexing requires a one-dimensional u16 array");
            return false;
        }
        compiler->uses_array_index = true;
        if (!rc_parse_expression(compiler, &index)) {
            return false;
        }
        if (rc_expr_is_void(index)) {
            rc_error(compiler, rc_peek(compiler, 0u), "array index requires a u16 value");
            return false;
        }
        if (index->kind == RC_EXPR_NUMBER && index->value >= base->entity->element_count) {
            rc_error(compiler, token, "array index %u is outside bounds 0..%zu",
                     (unsigned int)index->value, base->entity->element_count - 1u);
            return false;
        }
        if (!rc_expect(compiler, RC_TOKEN_RBRACKET, "']'")) {
            return false;
        }
        expression = rc_expr_new(compiler, RC_EXPR_INDEX, token);
        if (expression == NULL) {
            rc_error(compiler, token, "out of memory");
            return false;
        }
        expression->entity = base->entity;
        expression->left = base;
        expression->right = index;
        if (!rc_set_expression_depth(compiler, expression, base, index)) {
            return false;
        }
        base = expression;
    }
    if (base->kind == RC_EXPR_VARIABLE && base->entity->array) {
        rc_error(compiler, rc_peek(compiler, 0u),
                 "array '%s' cannot be used as a u16 value; use an index",
                 base->entity->name);
        return false;
    }
    *result = base;
    return true;
}

static bool rc_parse_unary(RcCompiler *compiler, RcExpr **result)
{
    RcToken *token = rc_peek(compiler, 0u);
    RcTokenKind kind = token->kind;
    RcExpr *child;
    RcExpr *expression;

    if (kind != RC_TOKEN_BANG && kind != RC_TOKEN_TILDE && kind != RC_TOKEN_MINUS) {
        return rc_parse_postfix(compiler, result);
    }
    if (compiler->parse_depth >= RETROCC_MAX_PARSE_DEPTH) {
        rc_error(compiler, token, "expression nesting exceeds %u levels",
                 RETROCC_MAX_PARSE_DEPTH);
        return false;
    }
    rc_take(compiler);
    ++compiler->parse_depth;
    if (!rc_parse_unary(compiler, &child)) {
        --compiler->parse_depth;
        return false;
    }
    --compiler->parse_depth;
    if (rc_expr_is_void(child)) {
        rc_error(compiler, token, "void value has no unary operator");
        return false;
    }
    expression = rc_expr_new(compiler, RC_EXPR_UNARY, token);
    if (expression == NULL) {
        rc_error(compiler, token, "out of memory");
        return false;
    }
    expression->op = kind;
    expression->left = child;
    if (!rc_set_expression_depth(compiler, expression, child, NULL)) {
        return false;
    }
    *result = expression;
    return true;
}

static bool rc_parse_multiplicative(RcCompiler *compiler, RcExpr **result)
{
    RcExpr *left;

    if (!rc_parse_unary(compiler, &left)) {
        return false;
    }
    for (;;) {
        RcToken *token = rc_peek(compiler, 0u);
        RcExpr *right;
        RcExpr *expression;

        if (token->kind == RC_TOKEN_STAR || token->kind == RC_TOKEN_PERCENT) {
            rc_error(compiler, token, "operator '%s' is not supported", token->text);
            return false;
        }
        if (!rc_accept(compiler, RC_TOKEN_SLASH)) {
            break;
        }
        if (!rc_parse_unary(compiler, &right)) {
            return false;
        }
        expression = rc_binary_expression(compiler, token, left, right);
        if (expression == NULL) {
            return false;
        }
        left = expression;
    }
    *result = left;
    return true;
}

#define RC_DEFINE_BINARY_PARSER(name, child_parser, condition)                    \
    static bool name(RcCompiler *compiler, RcExpr **result)                       \
    {                                                                              \
        RcExpr *left;                                                              \
        if (!child_parser(compiler, &left)) return false;                         \
        while (condition) {                                                        \
            RcToken *token = rc_take(compiler);                                   \
            RcExpr *right;                                                         \
            RcExpr *expression;                                                    \
            if (!child_parser(compiler, &right)) return false;                     \
            expression = rc_binary_expression(compiler, token, left, right);       \
            if (expression == NULL) return false;                                  \
            left = expression;                                                     \
        }                                                                          \
        *result = left;                                                            \
        return true;                                                               \
    }

RC_DEFINE_BINARY_PARSER(rc_parse_additive, rc_parse_multiplicative,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_PLUS ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_MINUS)
RC_DEFINE_BINARY_PARSER(rc_parse_shift, rc_parse_additive,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_LEFT_SHIFT ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_RIGHT_SHIFT)
RC_DEFINE_BINARY_PARSER(rc_parse_relational, rc_parse_shift,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_LESS ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_GREATER ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_LESS_EQUAL ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_GREATER_EQUAL)
RC_DEFINE_BINARY_PARSER(rc_parse_equality, rc_parse_relational,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_EQUAL ||
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_NOT_EQUAL)
RC_DEFINE_BINARY_PARSER(rc_parse_bitwise_and, rc_parse_equality,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_AMPERSAND)
RC_DEFINE_BINARY_PARSER(rc_parse_bitwise_xor, rc_parse_bitwise_and,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_CARET)
RC_DEFINE_BINARY_PARSER(rc_parse_bitwise_or, rc_parse_bitwise_xor,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_PIPE)
RC_DEFINE_BINARY_PARSER(rc_parse_logical_and, rc_parse_bitwise_or,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_LOGICAL_AND)
RC_DEFINE_BINARY_PARSER(rc_parse_logical_or, rc_parse_logical_and,
                        rc_peek(compiler, 0u)->kind == RC_TOKEN_LOGICAL_OR)

static bool rc_assignment_operator(RcTokenKind kind)
{
    return kind == RC_TOKEN_ASSIGN || kind == RC_TOKEN_PLUS_ASSIGN ||
           kind == RC_TOKEN_MINUS_ASSIGN || kind == RC_TOKEN_SLASH_ASSIGN ||
           kind == RC_TOKEN_AMPERSAND_ASSIGN || kind == RC_TOKEN_PIPE_ASSIGN ||
           kind == RC_TOKEN_CARET_ASSIGN || kind == RC_TOKEN_LEFT_SHIFT_ASSIGN ||
           kind == RC_TOKEN_RIGHT_SHIFT_ASSIGN;
}

static bool rc_parse_assignment(RcCompiler *compiler, RcExpr **result)
{
    RcExpr *left;

    if (!rc_parse_logical_or(compiler, &left)) {
        return false;
    }
    if (rc_assignment_operator(rc_peek(compiler, 0u)->kind)) {
        RcToken *token = rc_take(compiler);
        RcExpr *right;
        RcExpr *expression;

        if (!rc_expr_is_lvalue(left)) {
            if (left->kind == RC_EXPR_VARIABLE && left->entity->array) {
                rc_error(compiler, token, "array '%s' is not assignable; assign to an element",
                         left->entity->name);
            } else {
                rc_error(compiler, token,
                         "assignment target is not a modifiable u16 lvalue");
            }
            return false;
        }
        if (!rc_parse_expression(compiler, &right)) {
            return false;
        }
        if (rc_expr_is_void(right)) {
            rc_error(compiler, token, "void value cannot be assigned");
            return false;
        }
        expression = rc_expr_new(compiler, RC_EXPR_ASSIGNMENT, token);
        if (expression == NULL) {
            rc_error(compiler, token, "out of memory");
            return false;
        }
        expression->op = token->kind;
        expression->left = left;
        expression->right = right;
        if (!rc_set_expression_depth(compiler, expression, left, right)) {
            return false;
        }
        *result = expression;
        return true;
    }
    *result = left;
    return true;
}

static bool rc_parse_expression(RcCompiler *compiler, RcExpr **expression)
{
    bool success;
    RcToken *token = rc_peek(compiler, 0u);

    if (compiler->parse_depth >= RETROCC_MAX_PARSE_DEPTH) {
        rc_error(compiler, token, "expression nesting exceeds %u levels",
                 RETROCC_MAX_PARSE_DEPTH);
        return false;
    }
    ++compiler->parse_depth;
    success = rc_parse_assignment(compiler, expression);
    --compiler->parse_depth;
    return success;
}

static void rc_emit_entity_address(RcCompiler *compiler, RcEntity *entity)
{
    if (entity->kind == RC_ENTITY_GLOBAL) {
        rc_emit(compiler, "LDI %s, R1\nMOV R1, RS\n", entity->name);
        return;
    }
    rc_ldi(compiler, (unsigned int)(entity->offset + compiler->temporary_depth), "R1");
    rc_mov(compiler, "R1", "ORD0");
    rc_mov(compiler, "SP", "ORD1");
    rc_emit(compiler, "ADD\n");
}

static void rc_gen_lvalue_address(RcCompiler *compiler, RcExpr *expression)
{
    size_t in_bounds_label;

    if (expression->kind == RC_EXPR_VARIABLE) {
        rc_emit_entity_address(compiler, expression->entity);
        return;
    }
    if (expression->kind != RC_EXPR_INDEX) {
        rc_error(compiler, NULL, "internal lvalue error");
        return;
    }
    in_bounds_label = rc_new_label(compiler);
    rc_emit_entity_address(compiler, expression->entity);
    rc_push_temporary(compiler);
    rc_gen_expression(compiler, expression->right);
    rc_mov(compiler, "RS", "ORD0");
    rc_ldi(compiler, (unsigned int)expression->entity->element_count, "R1");
    rc_mov(compiler, "R1", "ORD1");
    rc_emit(compiler, "CMP\n");
    rc_emit(compiler, "JC __cc_%06zu\n", in_bounds_label);
    rc_emit(compiler, "JMP __array_out_of_bounds\n");
    rc_label(compiler, in_bounds_label);
    rc_pop_temporary(compiler, "R2");
    rc_mov(compiler, "R2", "ORD0");
    rc_mov(compiler, "RS", "ORD1");
    rc_emit(compiler, "ADD\n");
}

static void rc_emit_local_store(RcCompiler *compiler, RcEntity *local, size_t index,
                                const char *source_register)
{
    size_t address_offset = local->offset + index + compiler->temporary_depth;

    if (address_offset == 0u) {
        rc_mov(compiler, "SP", "MAR");
    } else {
        rc_ldi(compiler, (unsigned int)address_offset, "R1");
        rc_mov(compiler, "R1", "ORD0");
        rc_mov(compiler, "SP", "ORD1");
        rc_emit(compiler, "ADD\nMOV RS, MAR\n");
    }
    rc_emit(compiler, "WRT %s\n", source_register);
}

static void rc_emit_local_element_store(RcCompiler *compiler, RcEntity *local,
                                        size_t index, uint16_t value)
{
    rc_ldi(compiler, value, "R0");
    rc_emit_local_store(compiler, local, index, "R0");
}

static void rc_emit_nonzero_test(RcCompiler *compiler)
{
    rc_mov(compiler, "RS", "ORD0");
    rc_ldi(compiler, 0u, "R1");
    rc_mov(compiler, "R1", "ORD1");
    rc_emit(compiler, "CMP\n");
}

static void rc_emit_comparison_result(RcCompiler *compiler, RcTokenKind operation)
{
    size_t set_label = rc_new_label(compiler);
    size_t end_label = rc_new_label(compiler);

    switch (operation) {
    case RC_TOKEN_EQUAL:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JZ __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    case RC_TOKEN_NOT_EQUAL:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JNZ __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    case RC_TOKEN_GREATER:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JC __cc_%06zu\n", end_label);
        rc_emit(compiler, "JNZ __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    case RC_TOKEN_GREATER_EQUAL:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JNC __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    case RC_TOKEN_LESS:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JC __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    case RC_TOKEN_LESS_EQUAL:
        rc_ldi(compiler, 0u, "R0");
        rc_emit(compiler, "JC __cc_%06zu\n", set_label);
        rc_emit(compiler, "JZ __cc_%06zu\n", set_label);
        rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
        break;
    default:
        rc_error(compiler, NULL, "internal comparison error");
        return;
    }
    rc_label(compiler, set_label);
    rc_ldi(compiler, 1u, "R0");
    rc_label(compiler, end_label);
    rc_mov(compiler, "R0", "RS");
}

static void rc_emit_alu_operation(RcCompiler *compiler, RcTokenKind operation)
{
    switch (operation) {
    case RC_TOKEN_PLUS: rc_emit(compiler, "ADD\n"); break;
    case RC_TOKEN_MINUS: rc_emit(compiler, "SUB\n"); break;
    case RC_TOKEN_SLASH: rc_emit(compiler, "DIV\n"); break;
    case RC_TOKEN_AMPERSAND: rc_emit(compiler, "AND\n"); break;
    case RC_TOKEN_PIPE: rc_emit(compiler, "OR\n"); break;
    case RC_TOKEN_CARET: rc_emit(compiler, "XOR\n"); break;
    case RC_TOKEN_LEFT_SHIFT: rc_emit(compiler, "SHL\n"); break;
    case RC_TOKEN_RIGHT_SHIFT: rc_emit(compiler, "SHR\n"); break;
    default: rc_error(compiler, NULL, "internal ALU error"); break;
    }
}

static void rc_gen_logical_and(RcCompiler *compiler, RcExpr *expression)
{
    size_t false_label = rc_new_label(compiler);
    size_t end_label = rc_new_label(compiler);

    rc_gen_expression(compiler, expression->left);
    rc_emit_nonzero_test(compiler);
    rc_emit(compiler, "JZ __cc_%06zu\n", false_label);
    rc_gen_expression(compiler, expression->right);
    rc_emit_nonzero_test(compiler);
    rc_emit(compiler, "JZ __cc_%06zu\n", false_label);
    rc_ldi(compiler, 1u, "R0");
    rc_mov(compiler, "R0", "RS");
    rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
    rc_label(compiler, false_label);
    rc_ldi(compiler, 0u, "R0");
    rc_mov(compiler, "R0", "RS");
    rc_label(compiler, end_label);
}

static void rc_gen_logical_or(RcCompiler *compiler, RcExpr *expression)
{
    size_t true_label = rc_new_label(compiler);
    size_t end_label = rc_new_label(compiler);

    rc_gen_expression(compiler, expression->left);
    rc_emit_nonzero_test(compiler);
    rc_emit(compiler, "JNZ __cc_%06zu\n", true_label);
    rc_gen_expression(compiler, expression->right);
    rc_emit_nonzero_test(compiler);
    rc_emit(compiler, "JNZ __cc_%06zu\n", true_label);
    rc_ldi(compiler, 0u, "R0");
    rc_mov(compiler, "R0", "RS");
    rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
    rc_label(compiler, true_label);
    rc_ldi(compiler, 1u, "R0");
    rc_mov(compiler, "R0", "RS");
    rc_label(compiler, end_label);
}

static void rc_gen_call(RcCompiler *compiler, RcExpr *expression)
{
    size_t index;

    for (index = 0u; index < expression->argument_count; ++index) {
        rc_gen_expression(compiler, expression->arguments[index]);
        rc_push_temporary(compiler);
    }
    if (expression->argument_count != 0u) {
        static const char *const argument_registers[RETROCC_MAX_PARAMETERS] = {
            "R0", "R1", "R2", "R3", "R4", "R5"
        };
        for (index = expression->argument_count; index > 0u; --index) {
            rc_pop_temporary(compiler, argument_registers[index - 1u]);
        }
    }
    rc_emit(compiler, "LDI %s, R7\nCALL R7\n", expression->entity->name);
}

static void rc_gen_assignment(RcCompiler *compiler, RcExpr *expression)
{
    RcTokenKind operation = expression->op;

    rc_gen_lvalue_address(compiler, expression->left);
    rc_push_temporary(compiler);
    rc_gen_expression(compiler, expression->right);
    rc_push_temporary(compiler);
    rc_pop_temporary(compiler, "R0");
    rc_pop_temporary(compiler, "R1");
    if (operation == RC_TOKEN_ASSIGN) {
        rc_emit(compiler, "MOV R1, MAR\nWRT R0\nMOV R0, RS\n");
        return;
    }
    rc_emit(compiler, "MOV R1, MAR\nRED R2\n");
    rc_mov(compiler, "R2", "ORD0");
    rc_mov(compiler, "R0", "ORD1");
    switch (operation) {
    case RC_TOKEN_PLUS_ASSIGN: rc_emit(compiler, "ADD\n"); break;
    case RC_TOKEN_MINUS_ASSIGN: rc_emit(compiler, "SUB\n"); break;
    case RC_TOKEN_SLASH_ASSIGN: rc_emit(compiler, "DIV\n"); break;
    case RC_TOKEN_AMPERSAND_ASSIGN: rc_emit(compiler, "AND\n"); break;
    case RC_TOKEN_PIPE_ASSIGN: rc_emit(compiler, "OR\n"); break;
    case RC_TOKEN_CARET_ASSIGN: rc_emit(compiler, "XOR\n"); break;
    case RC_TOKEN_LEFT_SHIFT_ASSIGN: rc_emit(compiler, "SHL\n"); break;
    case RC_TOKEN_RIGHT_SHIFT_ASSIGN: rc_emit(compiler, "SHR\n"); break;
    default: rc_error(compiler, NULL, "internal assignment error"); return;
    }
    rc_emit(compiler, "MOV RS, R0\nMOV R1, MAR\nWRT R0\nMOV R0, RS\n");
}

static void rc_gen_expression(RcCompiler *compiler, RcExpr *expression)
{
    if (compiler->failed) {
        return;
    }
    switch (expression->kind) {
    case RC_EXPR_NUMBER:
        rc_ldi(compiler, expression->value, "R0");
        rc_mov(compiler, "R0", "RS");
        break;
    case RC_EXPR_VARIABLE:
    case RC_EXPR_INDEX:
        rc_gen_lvalue_address(compiler, expression);
        rc_mov(compiler, "RS", "MAR");
        rc_emit(compiler, "RED RS\n");
        break;
    case RC_EXPR_CALL:
        rc_gen_call(compiler, expression);
        break;
    case RC_EXPR_UNARY:
        rc_gen_expression(compiler, expression->left);
        if (expression->op == RC_TOKEN_TILDE) {
            rc_mov(compiler, "RS", "ORD0");
            rc_emit(compiler, "NOT\n");
        } else if (expression->op == RC_TOKEN_MINUS) {
            rc_mov(compiler, "RS", "ORD1");
            rc_ldi(compiler, 0u, "R1");
            rc_mov(compiler, "R1", "ORD0");
            rc_emit(compiler, "SUB\n");
        } else {
            rc_mov(compiler, "RS", "ORD0");
            rc_ldi(compiler, 0u, "R1");
            rc_mov(compiler, "R1", "ORD1");
            rc_emit(compiler, "CMP\n");
            rc_emit_comparison_result(compiler, RC_TOKEN_EQUAL);
        }
        break;
    case RC_EXPR_BINARY:
        if (expression->op == RC_TOKEN_LOGICAL_AND) {
            rc_gen_logical_and(compiler, expression);
        } else if (expression->op == RC_TOKEN_LOGICAL_OR) {
            rc_gen_logical_or(compiler, expression);
        } else {
            rc_gen_expression(compiler, expression->left);
            rc_push_temporary(compiler);
            rc_gen_expression(compiler, expression->right);
            rc_pop_temporary(compiler, "R2");
            rc_mov(compiler, "R2", "ORD0");
            rc_mov(compiler, "RS", "ORD1");
            if (expression->op == RC_TOKEN_EQUAL || expression->op == RC_TOKEN_NOT_EQUAL ||
                expression->op == RC_TOKEN_LESS || expression->op == RC_TOKEN_GREATER ||
                expression->op == RC_TOKEN_LESS_EQUAL ||
                expression->op == RC_TOKEN_GREATER_EQUAL) {
                rc_emit(compiler, "CMP\n");
                rc_emit_comparison_result(compiler, expression->op);
            } else {
                rc_emit_alu_operation(compiler, expression->op);
            }
        }
        break;
    case RC_EXPR_ASSIGNMENT:
        rc_gen_assignment(compiler, expression);
        break;
    }
}

static void rc_emit_prologue(RcCompiler *compiler, RcEntity **parameters,
                             size_t parameter_count)
{
    static const char *const parameter_registers[RETROCC_MAX_PARAMETERS] = {
        "R0", "R1", "R2", "R3", "R4", "R5"
    };
    size_t index;

    rc_ldi(compiler, RETROCC_STACK_GUARD + (unsigned int)compiler->frame_words, "R6");
    rc_mov(compiler, "R6", "ORD1");
    rc_mov(compiler, "SP", "ORD0");
    rc_emit(compiler, "CMP\n");
    rc_emit(compiler, "JC __stack_overflow\nJZ __stack_overflow\n");
    if (compiler->frame_words != 0u) {
        rc_ldi(compiler, (unsigned int)compiler->frame_words, "R6");
        rc_mov(compiler, "R6", "ORD1");
        rc_mov(compiler, "SP", "ORD0");
        rc_emit(compiler, "SUB\nMOV RS, SP\n");
    }
    for (index = 0u; index < parameter_count; ++index) {
        rc_mov(compiler, parameter_registers[index], "R6");
        rc_emit_local_store(compiler, parameters[index], 0u, "R6");
    }
}

static void rc_emit_return_epilogue(RcCompiler *compiler, RcType type)
{
    if (type == RC_TYPE_U16) {
        rc_pop_temporary(compiler, "R0");
        if (compiler->frame_words != 0u) {
            rc_ldi(compiler, (unsigned int)compiler->frame_words, "R1");
            rc_mov(compiler, "R1", "ORD1");
            rc_mov(compiler, "SP", "ORD0");
            rc_emit(compiler, "ADD\nMOV RS, SP\n");
        }
        rc_mov(compiler, "R0", "RS");
        rc_emit(compiler, "RET\n");
        return;
    }
    if (compiler->frame_words != 0u) {
        rc_ldi(compiler, (unsigned int)compiler->frame_words, "R0");
        rc_mov(compiler, "R0", "ORD1");
        rc_mov(compiler, "SP", "ORD0");
        rc_emit(compiler, "ADD\nMOV RS, SP\n");
    }
    rc_emit(compiler, "RET\n");
}

static bool rc_parse_local_array_initializer(RcCompiler *compiler, RcEntity *local)
{
    size_t index = 0u;

    if (!rc_expect(compiler, RC_TOKEN_LBRACE, "'{'")) {
        return false;
    }
    if (!rc_accept(compiler, RC_TOKEN_RBRACE)) {
        for (;;) {
            RcExpr *initializer;
            if (index >= local->element_count) {
                rc_error(compiler, rc_peek(compiler, 0u),
                         "array '%s' has more than %zu initializers",
                         local->name, local->element_count);
                return false;
            }
            if (!rc_parse_expression(compiler, &initializer)) {
                return false;
            }
            if (rc_expr_is_void(initializer)) {
                rc_error(compiler, rc_peek(compiler, 0u),
                         "array initializer requires u16 values");
                return false;
            }
            rc_gen_expression(compiler, initializer);
            rc_mov(compiler, "RS", "R0");
            rc_emit_local_store(compiler, local, index++, "R0");
            if (rc_accept(compiler, RC_TOKEN_COMMA)) {
                if (rc_accept(compiler, RC_TOKEN_RBRACE)) {
                    return true;
                }
                continue;
            }
            break;
        }
        if (!rc_expect(compiler, RC_TOKEN_RBRACE, "'}'")) {
            return false;
        }
    }
    while (index < local->element_count) {
        rc_emit_local_element_store(compiler, local, index++, 0u);
    }
    return true;
}

static bool rc_parse_local_declaration(RcCompiler *compiler)
{
    RcToken *name_token;
    RcEntity *local;
    RcExpr *expression;

    if (!rc_expect(compiler, RC_TOKEN_U16, "'u16'")) {
        return false;
    }
    name_token = rc_peek(compiler, 0u);
    if (!rc_expect(compiler, RC_TOKEN_IDENTIFIER, "local name")) {
        return false;
    }
    if (rc_reserved_name(name_token->text)) {
        rc_error(compiler, name_token, "identifier '%s' is reserved", name_token->text);
        return false;
    }
    local = rc_arena_allocate(&compiler->arena, sizeof(RcEntity));
    if (local == NULL) {
        rc_error(compiler, name_token, "out of memory");
        return false;
    }
    memset(local, 0, sizeof(*local));
    local->name = rc_arena_copy(&compiler->arena, name_token->text);
    if (local->name == NULL) {
        rc_error(compiler, name_token, "out of memory");
        return false;
    }
    if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
        if (!rc_parse_array_length(compiler, &local->element_count)) {
            return false;
        }
        local->array = true;
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "only one-dimensional u16 arrays are supported");
            return false;
        }
    } else {
        local->element_count = 1u;
    }
    if (!rc_add_local(compiler, local, name_token)) {
        return false;
    }
    if (rc_accept(compiler, RC_TOKEN_ASSIGN)) {
        if (local->array) {
            if (!rc_parse_local_array_initializer(compiler, local)) {
                return false;
            }
        } else {
            if (!rc_parse_expression(compiler, &expression)) {
                return false;
            }
            if (rc_expr_is_void(expression)) {
                rc_error(compiler, name_token, "local initializer requires a u16 value");
                return false;
            }
            rc_gen_expression(compiler, expression);
            rc_mov(compiler, "RS", "R0");
            rc_emit_local_store(compiler, local, 0u, "R0");
        }
    }
    return rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'");
}

static bool rc_gen_condition(RcCompiler *compiler, size_t false_label)
{
    RcExpr *expression;

    if (!rc_parse_expression(compiler, &expression)) {
        return false;
    }
    if (rc_expr_is_void(expression)) {
        rc_error(compiler, rc_peek(compiler, 0u), "condition requires a u16 value");
        return false;
    }
    rc_gen_expression(compiler, expression);
    rc_emit_nonzero_test(compiler);
    rc_emit(compiler, "JZ __cc_%06zu\n", false_label);
    return true;
}

static bool rc_parse_if(RcCompiler *compiler)
{
    size_t else_label;
    size_t end_label;

    if (!rc_expect(compiler, RC_TOKEN_IF, "'if'") ||
        !rc_expect(compiler, RC_TOKEN_LPAREN, "'('")) {
        return false;
    }
    else_label = rc_new_label(compiler);
    end_label = rc_new_label(compiler);
    if (!rc_gen_condition(compiler, else_label) ||
        !rc_expect(compiler, RC_TOKEN_RPAREN, "')'")) {
        return false;
    }
    if (!rc_parse_statement(compiler)) {
        return false;
    }
    rc_emit(compiler, "JMP __cc_%06zu\n", end_label);
    rc_label(compiler, else_label);
    if (rc_accept(compiler, RC_TOKEN_ELSE) && !rc_parse_statement(compiler)) {
        return false;
    }
    rc_label(compiler, end_label);
    return !compiler->failed;
}

static bool rc_parse_while(RcCompiler *compiler)
{
    size_t condition_label = rc_new_label(compiler);
    size_t end_label = rc_new_label(compiler);
    RcLoop loop;

    if (!rc_expect(compiler, RC_TOKEN_WHILE, "'while'") ||
        !rc_expect(compiler, RC_TOKEN_LPAREN, "'('")) {
        return false;
    }
    rc_label(compiler, condition_label);
    if (!rc_gen_condition(compiler, end_label) ||
        !rc_expect(compiler, RC_TOKEN_RPAREN, "')'")) {
        return false;
    }
    loop.break_label = end_label;
    loop.continue_label = condition_label;
    loop.previous = compiler->loop;
    compiler->loop = &loop;
    if (!rc_parse_statement(compiler)) {
        compiler->loop = loop.previous;
        return false;
    }
    compiler->loop = loop.previous;
    rc_emit(compiler, "JMP __cc_%06zu\n", condition_label);
    rc_label(compiler, end_label);
    return !compiler->failed;
}

static bool rc_parse_for(RcCompiler *compiler)
{
    size_t condition_label;
    size_t step_label;
    size_t end_label;
    RcLoop loop;
    const RcLoop *previous = compiler->loop;
    RcEntity *outer_locals = compiler->locals;
    RcExpr *step_expression = NULL;
    bool loop_installed = false;
    bool success = false;

    if (!rc_expect(compiler, RC_TOKEN_FOR, "'for'") ||
        !rc_expect(compiler, RC_TOKEN_LPAREN, "'('")) {
        return false;
    }
    ++compiler->scope_depth;
    if (rc_accept(compiler, RC_TOKEN_SEMICOLON)) {
    } else if (rc_peek(compiler, 0u)->kind == RC_TOKEN_U16) {
        if (!rc_parse_local_declaration(compiler)) {
            goto cleanup;
        }
    } else {
        RcExpr *initializer;
        if (!rc_parse_expression(compiler, &initializer)) {
            goto cleanup;
        }
        if (rc_expr_is_void(initializer)) {
            rc_error(compiler, rc_peek(compiler, 0u),
                     "for initializer cannot produce void");
            goto cleanup;
        }
        rc_gen_expression(compiler, initializer);
        if (!rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'")) {
            goto cleanup;
        }
    }
    condition_label = rc_new_label(compiler);
    step_label = rc_new_label(compiler);
    end_label = rc_new_label(compiler);
    rc_label(compiler, condition_label);
    if (rc_peek(compiler, 0u)->kind != RC_TOKEN_SEMICOLON &&
        !rc_gen_condition(compiler, end_label)) {
        goto cleanup;
    }
    if (!rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'")) {
        goto cleanup;
    }
    if (rc_peek(compiler, 0u)->kind != RC_TOKEN_RPAREN) {
        if (!rc_parse_expression(compiler, &step_expression)) {
            goto cleanup;
        }
        if (rc_expr_is_void(step_expression)) {
            rc_error(compiler, rc_peek(compiler, 0u), "for step cannot produce void");
            goto cleanup;
        }
    }
    if (!rc_expect(compiler, RC_TOKEN_RPAREN, "')'")) {
        goto cleanup;
    }
    loop.break_label = end_label;
    loop.continue_label = step_label;
    loop.previous = previous;
    compiler->loop = &loop;
    loop_installed = true;
    if (!rc_parse_statement(compiler)) {
        goto cleanup;
    }
    compiler->loop = previous;
    loop_installed = false;
    rc_label(compiler, step_label);
    if (step_expression != NULL) {
        rc_gen_expression(compiler, step_expression);
    }
    rc_emit(compiler, "JMP __cc_%06zu\n", condition_label);
    rc_label(compiler, end_label);
    success = !compiler->failed;
cleanup:
    if (loop_installed) {
        compiler->loop = previous;
    }
    --compiler->scope_depth;
    compiler->locals = outer_locals;
    return success;
}

static bool rc_parse_return(RcCompiler *compiler)
{
    RcToken *token = rc_peek(compiler, 0u);
    RcType type;

    if (!rc_expect(compiler, RC_TOKEN_RETURN, "'return'")) {
        return false;
    }
    if (compiler->current_function == NULL) {
        rc_error(compiler, token, "return outside a function");
        return false;
    }
    type = compiler->current_function->type;
    if (type == RC_TYPE_VOID) {
        if (!rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'")) {
            return false;
        }
        rc_emit_return_epilogue(compiler, type);
        return !compiler->failed;
    }
    {
        RcExpr *expression;
        if (!rc_parse_expression(compiler, &expression)) {
            return false;
        }
        if (rc_expr_is_void(expression)) {
            rc_error(compiler, token, "u16 function cannot return void");
            return false;
        }
        rc_gen_expression(compiler, expression);
        rc_push_temporary(compiler);
    }
    if (!rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'")) {
        return false;
    }
    rc_emit_return_epilogue(compiler, type);
    return !compiler->failed;
}

static bool rc_parse_statement(RcCompiler *compiler)
{
    RcToken *token = rc_peek(compiler, 0u);

    if (token->kind == RC_TOKEN_LBRACE) {
        RcEntity *outer_locals = compiler->locals;
        rc_take(compiler);
        ++compiler->scope_depth;
        while (rc_peek(compiler, 0u)->kind != RC_TOKEN_RBRACE) {
            if (rc_peek(compiler, 0u)->kind == RC_TOKEN_EOF) {
                rc_error(compiler, rc_peek(compiler, 0u), "unterminated block");
                goto block_error;
            }
            if (rc_peek(compiler, 0u)->kind == RC_TOKEN_U16) {
                if (!rc_parse_local_declaration(compiler)) {
                    goto block_error;
                }
            } else if (!rc_parse_statement(compiler)) {
                goto block_error;
            }
        }
        rc_take(compiler);
        --compiler->scope_depth;
        compiler->locals = outer_locals;
        return true;
block_error:
        --compiler->scope_depth;
        compiler->locals = outer_locals;
        return false;
    }
    if (token->kind == RC_TOKEN_U16) {
        return rc_parse_local_declaration(compiler);
    }
    if (token->kind == RC_TOKEN_IF) return rc_parse_if(compiler);
    if (token->kind == RC_TOKEN_WHILE) return rc_parse_while(compiler);
    if (token->kind == RC_TOKEN_FOR) return rc_parse_for(compiler);
    if (token->kind == RC_TOKEN_RETURN) return rc_parse_return(compiler);
    if (rc_accept(compiler, RC_TOKEN_SEMICOLON)) return true;
    if (rc_accept(compiler, RC_TOKEN_BREAK)) {
        if (compiler->loop == NULL) {
            rc_error(compiler, token, "break is only valid inside a loop");
            return false;
        }
        rc_emit(compiler, "JMP __cc_%06zu\n", compiler->loop->break_label);
        return rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'");
    }
    if (rc_accept(compiler, RC_TOKEN_CONTINUE)) {
        if (compiler->loop == NULL) {
            rc_error(compiler, token, "continue is only valid inside a loop");
            return false;
        }
        rc_emit(compiler, "JMP __cc_%06zu\n", compiler->loop->continue_label);
        return rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'");
    }
    {
        RcExpr *expression;
        if (!rc_parse_expression(compiler, &expression)) {
            return false;
        }
        rc_gen_expression(compiler, expression);
    }
    return rc_expect(compiler, RC_TOKEN_SEMICOLON, "';'");
}

static bool rc_parse_body_parameters(RcCompiler *compiler, RcEntity **parameters,
                                     size_t *count)
{
    if (!rc_expect(compiler, RC_TOKEN_LPAREN, "'('")) {
        return false;
    }
    *count = 0u;
    if (rc_accept(compiler, RC_TOKEN_RPAREN)) {
        return true;
    }
    if (rc_peek(compiler, 0u)->kind == RC_TOKEN_VOID &&
        rc_peek(compiler, 1u)->kind == RC_TOKEN_RPAREN) {
        rc_take(compiler);
        rc_take(compiler);
        return true;
    }
    for (;;) {
        RcToken *name_token;
        RcEntity *local;

        if (!rc_expect(compiler, RC_TOKEN_U16, "parameter type 'u16'")) {
            return false;
        }
        name_token = rc_peek(compiler, 0u);
        if (!rc_expect(compiler, RC_TOKEN_IDENTIFIER, "parameter name")) {
            return false;
        }
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LBRACKET) {
            rc_error(compiler, rc_peek(compiler, 0u), "array parameters are not supported");
            return false;
        }
        if (rc_reserved_name(name_token->text)) {
            rc_error(compiler, name_token, "identifier '%s' is reserved", name_token->text);
            return false;
        }
        if (*count >= RETROCC_MAX_PARAMETERS) {
            rc_error(compiler, name_token, "a function may have at most %u parameters",
                     RETROCC_MAX_PARAMETERS);
            return false;
        }
        local = rc_arena_allocate(&compiler->arena, sizeof(RcEntity));
        if (local == NULL) {
            rc_error(compiler, name_token, "out of memory");
            return false;
        }
        memset(local, 0, sizeof(*local));
        local->element_count = 1u;
        local->name = rc_arena_copy(&compiler->arena, name_token->text);
        if (local->name == NULL) {
            rc_error(compiler, name_token, "out of memory");
            return false;
        }
        if (!rc_add_local(compiler, local, name_token)) {
            return false;
        }
        parameters[(*count)++] = local;
        if (rc_accept(compiler, RC_TOKEN_COMMA)) {
            continue;
        }
        break;
    }
    return rc_expect(compiler, RC_TOKEN_RPAREN, "')'");
}

static bool rc_parse_function(RcCompiler *compiler, RcEntity *function)
{
    RcEntity *parameters[RETROCC_MAX_PARAMETERS];
    size_t parameter_count = 0u;
    bool success = false;
    RcBuffer outer_output = compiler->output;
    RcBuffer body_output = {0};
    RcBuffer prefix_output = {0};
    RcCompiler prefix_compiler;
    bool output_attached = false;

    compiler->output = body_output;
    compiler->current_function = function;
    compiler->locals = NULL;
    compiler->scope_depth = 0u;
    compiler->frame_words = 0u;
    compiler->temporary_depth = 0u;
    compiler->loop = NULL;
    if (!rc_parse_body_parameters(compiler, parameters, &parameter_count)) {
        goto cleanup;
    }
    if (parameter_count != function->parameter_count) {
        rc_error(compiler, rc_peek(compiler, 0u), "function '%s' has inconsistent signature",
                 function->name);
        goto cleanup;
    }
    if (!rc_parse_statement(compiler)) {
        goto cleanup;
    }
    if (function->type == RC_TYPE_U16) {
        rc_ldi(compiler, 0u, "R0");
        rc_mov(compiler, "R0", "RS");
        rc_push_temporary(compiler);
    }
    rc_emit_return_epilogue(compiler, function->type);
    if (compiler->failed) {
        goto cleanup;
    }
    prefix_compiler = *compiler;
    prefix_compiler.output = prefix_output;
    rc_emit(&prefix_compiler, "%s:\n", function->name);
    rc_emit_prologue(&prefix_compiler, parameters, parameter_count);
    prefix_output = prefix_compiler.output;
    if (prefix_compiler.failed) {
        rc_error(compiler, NULL, "unable to generate function prologue");
        goto cleanup;
    }
    body_output = compiler->output;
    compiler->output = outer_output;
    output_attached = true;
    if (!rc_buffer_append(&compiler->output, prefix_output.data, prefix_output.length) ||
        !rc_buffer_append(&compiler->output, body_output.data, body_output.length)) {
        rc_error(compiler, NULL, "unable to append generated function");
        goto cleanup;
    }
    success = true;
cleanup:
    if (!output_attached) {
        body_output = compiler->output;
        compiler->output = outer_output;
    }
    rc_buffer_clear(&body_output);
    rc_buffer_clear(&prefix_output);
    compiler->locals = NULL;
    compiler->current_function = NULL;
    compiler->scope_depth = 0u;
    compiler->frame_words = 0u;
    compiler->temporary_depth = 0u;
    compiler->loop = NULL;
    return success;
}

static bool rc_skip_global(RcCompiler *compiler)
{
    for (;;) {
        RcToken *token = rc_take(compiler);
        if (token->kind == RC_TOKEN_EOF) {
            rc_error(compiler, token, "unterminated global declaration");
            return false;
        }
        if (token->kind == RC_TOKEN_SEMICOLON) {
            return true;
        }
    }
}

static bool rc_generate(RcCompiler *compiler)
{
    compiler->position = 0u;
    rc_emit(compiler, ".entry __retro_entry\n__retro_entry:\nLDI FFFFh, R0\n"
                      "MOV R0, SP\nLDI main, R7\nCALL R7\nHLT\n");
    while (rc_peek(compiler, 0u)->kind != RC_TOKEN_EOF && !compiler->failed) {
        RcToken *name_token;
        rc_take(compiler);
        name_token = rc_peek(compiler, 0u);
        if (!rc_expect(compiler, RC_TOKEN_IDENTIFIER, "declaration name")) {
            return false;
        }
        if (rc_peek(compiler, 0u)->kind == RC_TOKEN_LPAREN) {
            RcEntity *function = rc_find_entity(compiler->functions, name_token->text);
            if (function == NULL) {
                rc_error(compiler, name_token, "internal function collection failure");
                return false;
            }
            if (!rc_parse_function(compiler, function)) {
                return false;
            }
        } else if (!rc_skip_global(compiler)) {
            return false;
        }
    }
    if (compiler->uses_array_index) {
        rc_emit(compiler, "__array_out_of_bounds:\nLDI FFFFh, R0\nMOV R0, RS\nHLT\n");
    }
    rc_emit(compiler, "__stack_overflow:\nLDI %04Xh, R0\nMOV R0, RS\nHLT\n",
            RETROCC_STACK_GUARD);
    {
        RcEntity *global;
        for (global = compiler->globals; global != NULL; global = global->next) {
            size_t index;
            rc_emit(compiler, "%s:\n", global->name);
            for (index = 0u; index < global->element_count; ++index) {
                uint16_t value = index < global->initializer_count
                                     ? global->initializers[index] : 0u;
                rc_emit(compiler, ".word %04Xh\n", value);
            }
        }
    }
    rc_emit(compiler, "__stack_limit:\n.word %04Xh\n", RETROCC_STACK_GUARD);
    return !compiler->failed;
}

static bool rc_output_word_count(const RcBuffer *output, size_t *word_count)
{
    size_t position = 0u;
    size_t count = 0u;

    while (position < output->length) {
        size_t start = position;
        size_t end;
        size_t first;
        size_t token_end;

        while (position < output->length && output->data[position] != '\n') {
            ++position;
        }
        end = position;
        if (position < output->length) {
            ++position;
        }
        if (end > start && output->data[end - 1u] == '\r') {
            --end;
        }
        first = start;
        while (first < end && (output->data[first] == ' ' || output->data[first] == '\t')) {
            ++first;
        }
        if (first == end || output->data[end - 1u] == ':' || output->data[first] == '.') {
            if (first < end && output->data[first] == '.' && end - first >= 5u &&
                memcmp(output->data + first, ".word", 5u) == 0) {
                if (count == SIZE_MAX) return false;
                ++count;
            }
            continue;
        }
        token_end = first;
        while (token_end < end && output->data[token_end] != ' ' &&
               output->data[token_end] != '\t') {
            ++token_end;
        }
        {
            size_t length = token_end - first;
            size_t words = 1u;
            if (length == 3u && memcmp(output->data + first, "LDI", 3u) == 0) {
                words = 2u;
            } else if ((length == 3u &&
                        (memcmp(output->data + first, "JMP", 3u) == 0 ||
                         memcmp(output->data + first, "JNZ", 3u) == 0 ||
                         memcmp(output->data + first, "JNC", 3u) == 0)) ||
                       (length == 2u &&
                        (memcmp(output->data + first, "JZ", 2u) == 0 ||
                         memcmp(output->data + first, "JN", 2u) == 0 ||
                         memcmp(output->data + first, "JP", 2u) == 0 ||
                         memcmp(output->data + first, "JC", 2u) == 0 ||
                         memcmp(output->data + first, "JV", 2u) == 0))) {
                words = 3u;
            }
            if (count > SIZE_MAX - words) {
                return false;
            }
            count += words;
        }
    }
    *word_count = count;
    return true;
}

static bool rc_validate_output(RcCompiler *compiler)
{
    size_t words;
    if (!rc_output_word_count(&compiler->output, &words)) {
        rc_error(compiler, NULL, "unable to count generated program");
        return false;
    }
    if (words > RETROCC_STACK_LIMIT) {
        rc_error(compiler, NULL, "generated program exceeds reserved stack boundary");
        return false;
    }
    return true;
}

static bool rc_write_output(const char *path, const RcBuffer *output)
{
    FILE *file = fopen(path, "wb");
    bool success;

    if (file == NULL) {
        return false;
    }
    success = output->length == 0u ||
              fwrite(output->data, 1u, output->length, file) == output->length;
    if (fclose(file) != 0) {
        success = false;
    }
    return success;
}

static void rc_compiler_clear(RcCompiler *compiler)
{
    free(compiler->tokens);
    compiler->tokens = NULL;
    compiler->token_count = 0u;
    compiler->token_capacity = 0u;
    rc_buffer_clear(&compiler->source);
    rc_buffer_clear(&compiler->output);
    rc_arena_clear(&compiler->arena);
}

static bool rc_compile_file(const char *input_path, const char *output_path)
{
    RcCompiler compiler;
    size_t line = 1u;
    size_t column = 1u;
    bool success = false;
    const char *message = "unknown error";

    memset(&compiler, 0, sizeof(compiler));
    compiler.path = input_path;
    if (!rc_read_source(input_path, &compiler.source, &message)) {
        fprintf(stderr, "%s: error: unable to read source: %s\n", input_path, message);
        goto cleanup;
    }
    if (!rc_lex_source(&compiler, &line, &column) || compiler.failed) {
        goto cleanup;
    }
    if (!rc_collect_entities(&compiler) || !rc_generate(&compiler) ||
        !rc_validate_output(&compiler)) {
        goto cleanup;
    }
    if (!rc_write_output(output_path, &compiler.output)) {
        fprintf(stderr, "%s: error: unable to write output: %s\n",
                output_path, strerror(errno));
        goto cleanup;
    }
    printf("Compiled %s -> %s\n", input_path, output_path);
    success = true;
cleanup:
    rc_compiler_clear(&compiler);
    return success;
}

static void rc_usage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [-h] -o OUTPUT INPUT\n", program);
}

static int rc_arguments(int argc, char **argv, const char **input, const char **output)
{
    int index;
    bool end_options = false;

    *input = NULL;
    *output = NULL;
    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (!end_options && strcmp(argument, "--") == 0) {
            end_options = true;
            continue;
        }
        if (!end_options && (strcmp(argument, "-h") == 0 ||
                             strcmp(argument, "--help") == 0)) {
            return 1;
        }
        if (!end_options && (strcmp(argument, "-o") == 0 ||
                             strcmp(argument, "--output") == 0)) {
            if (index + 1 >= argc) {
                rc_usage(stderr, argv[0]);
                return 2;
            }
            *output = argv[++index];
            continue;
        }
        if (!end_options && argument[0] == '-' && argument[1] != '\0') {
            rc_usage(stderr, argv[0]);
            return 2;
        }
        if (*input != NULL) {
            rc_usage(stderr, argv[0]);
            return 2;
        }
        *input = argument;
    }
    if (*input == NULL || *output == NULL) {
        rc_usage(stderr, argv[0]);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *output = NULL;
    int status = rc_arguments(argc, argv, &input, &output);

    if (status == 1) {
        rc_usage(stdout, argv[0]);
        return 0;
    }
    if (status != 0) {
        return 2;
    }
    return rc_compile_file(input, output) ? 0 : 1;
}
