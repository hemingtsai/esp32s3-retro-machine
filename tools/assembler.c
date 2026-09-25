#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define MAX_PROGRAM_WORDS ((size_t)0x10000u)

typedef struct {
    char *text;
    bool owned;
} Error;

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} ByteBuffer;

typedef struct {
    unsigned char *digits;
    size_t length;
    size_t capacity;
} Decimal;

typedef struct {
    char *decimal;
    bool negative;
} Number;

typedef struct {
    char *name;
    size_t address;
} Label;

typedef struct {
    Label *items;
    size_t count;
    size_t capacity;
} LabelTable;

typedef enum {
    LINE_EMPTY,
    LINE_WORD,
    LINE_INSTRUCTION
} LineKind;

typedef struct {
    size_t line_number;
    char **labels;
    size_t label_count;
    size_t label_capacity;
    char *mnemonic;
    char **operands;
    size_t operand_count;
    size_t operand_capacity;
    LineKind kind;
} SourceLine;

typedef struct {
    SourceLine *items;
    size_t count;
    size_t capacity;
} SourceLines;

typedef struct {
    uint16_t *items;
    size_t count;
    size_t capacity;
} WordList;

typedef struct {
    WordList words;
    LabelTable labels;
    size_t entry_address;
} Assembly;

typedef struct {
    const char *name;
    uint16_t opcode;
} Opcode;

typedef enum {
    FORMAT_BIN,
    FORMAT_TEXT,
    FORMAT_HEX,
    FORMAT_ADDRTEXT,
    FORMAT_CMD
} OutputFormat;

typedef enum {
    NUMBER_OK,
    NUMBER_INVALID,
    NUMBER_ERROR
} NumberStatus;

typedef struct {
    uint16_t words[5];
    size_t count;
} EncodedWords;

static const Opcode opcodes[] = {
    {"MOV", 0x00u}, {"LDI", 0x01u}, {"RED", 0x02u}, {"WRT", 0x03u},
    {"PUSH", 0x04u}, {"POP", 0x05u}, {"MNXT", 0x06u}, {"MPRV", 0x07u},
    {"ADD", 0x08u}, {"SUB", 0x09u}, {"DIV", 0x0au}, {"INC", 0x0bu},
    {"DEC", 0x0cu}, {"AND", 0x0du}, {"OR", 0x0eu}, {"XOR", 0x0fu},
    {"NOT", 0x10u}, {"SHL", 0x11u}, {"SHR", 0x12u}, {"CMP", 0x13u},
    {"JMP", 0x14u}, {"JZ", 0x15u}, {"JNZ", 0x16u}, {"JN", 0x17u},
    {"JP", 0x18u}, {"JC", 0x19u}, {"JNC", 0x1au}, {"JV", 0x1bu},
    {"CALL", 0x1cu}, {"RET", 0x1du}, {"NOP", 0x1eu}, {"HLT", 0x1fu}
};

static const char *const registers[] = {
    "ORD0", "ORD1", "PC", "IR", "MAR", "SP", "RS", "DISPLAY",
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7"
};

static void error_init(Error *error)
{
    error->text = NULL;
    error->owned = false;
}

static void error_clear(Error *error)
{
    if (error->owned) {
        free(error->text);
    }
    error->text = NULL;
    error->owned = false;
}

static bool set_error(Error *error, const char *format, ...)
{
    va_list args;
    va_list measure_args;
    int needed;

    if (error->text != NULL) {
        return false;
    }

    va_start(args, format);
    va_copy(measure_args, args);
    needed = vsnprintf(NULL, 0, format, measure_args);
    va_end(measure_args);
    va_end(args);

    if (needed < 0) {
        error->text = "invalid diagnostic";
        error->owned = false;
        return false;
    }

    error->text = malloc((size_t)needed + 1u);
    if (error->text == NULL) {
        error->text = "out of memory";
        error->owned = false;
        return false;
    }

    va_start(args, format);
    needed = vsnprintf(error->text, (size_t)needed + 1u, format, args);
    va_end(args);
    if (needed < 0) {
        free(error->text);
        error->text = "invalid diagnostic";
        error->owned = false;
    } else {
        error->owned = true;
    }
    return false;
}

static bool grow_array(void **data, size_t *capacity, size_t needed, size_t element_size)
{
    size_t new_capacity;
    void *new_data;

    if (needed <= *capacity) {
        return true;
    }
    if (element_size == 0u || needed > SIZE_MAX / element_size) {
        return false;
    }

    new_capacity = *capacity == 0u ? 8u : *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2u;
    }
    if (new_capacity > SIZE_MAX / element_size) {
        return false;
    }

    new_data = realloc(*data, new_capacity * element_size);
    if (new_data == NULL) {
        return false;
    }
    *data = new_data;
    *capacity = new_capacity;
    return true;
}

static char *copy_n(const char *source, size_t length)
{
    char *copy;

    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = malloc(length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0u) {
        memcpy(copy, source, length);
    }
    copy[length] = '\0';
    return copy;
}

static char *copy_upper_n(const char *source, size_t length)
{
    char *copy = copy_n(source, length);
    size_t index;

    if (copy == NULL) {
        return NULL;
    }
    for (index = 0u; index < length; ++index) {
        if (copy[index] >= 'a' && copy[index] <= 'z') {
            copy[index] = (char)(copy[index] - 'a' + 'A');
        }
    }
    return copy;
}

static void buffer_init(ByteBuffer *buffer)
{
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static void buffer_clear(ByteBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool buffer_reserve(ByteBuffer *buffer, size_t extra)
{
    size_t needed;

    if (extra > SIZE_MAX - buffer->length) {
        return false;
    }
    needed = buffer->length + extra;
    if (needed == SIZE_MAX) {
        return false;
    }
    return grow_array((void **)&buffer->data, &buffer->capacity, needed + 1u, sizeof(unsigned char));
}

static bool buffer_append(ByteBuffer *buffer, const unsigned char *source, size_t length)
{
    if (!buffer_reserve(buffer, length)) {
        return false;
    }
    if (length != 0u) {
        memcpy(buffer->data + buffer->length, source, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = 0u;
    return true;
}

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\v' || value == '\f' ||
           value == '\x1c' || value == '\x1d' || value == '\x1e';
}

static bool is_line_break(unsigned char value)
{
    return value == '\n' || value == '\r' || value == '\v' || value == '\f' ||
           value == '\x1c' || value == '\x1d' || value == '\x1e';
}

static bool is_utf8_continuation(unsigned char value)
{
    return value >= 0x80u && value <= 0xbfu;
}

static bool validate_utf8(const unsigned char *data, size_t length, Error *error)
{
    size_t position = 0u;

    while (position < length) {
        unsigned char first = data[position];
        size_t remaining = length - position;

        if (first == 0u) {
            return set_error(error, "source contains NUL byte at byte %zu", position);
        }
        if (first <= 0x7fu) {
            ++position;
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            if (remaining < 2u || !is_utf8_continuation(data[position + 1u])) {
                return set_error(error, "source is not valid UTF-8 at byte %zu", position);
            }
            position += 2u;
            continue;
        }
        if (first >= 0xe0u && first <= 0xefu) {
            unsigned char second;
            if (remaining < 3u || !is_utf8_continuation(data[position + 1u]) ||
                !is_utf8_continuation(data[position + 2u])) {
                return set_error(error, "source is not valid UTF-8 at byte %zu", position);
            }
            second = data[position + 1u];
            if ((first == 0xe0u && second < 0xa0u) ||
                (first == 0xedu && second > 0x9fu)) {
                return set_error(error, "source is not valid UTF-8 at byte %zu", position);
            }
            position += 3u;
            continue;
        }
        if (first >= 0xf0u && first <= 0xf4u) {
            unsigned char second;
            if (remaining < 4u || !is_utf8_continuation(data[position + 1u]) ||
                !is_utf8_continuation(data[position + 2u]) ||
                !is_utf8_continuation(data[position + 3u])) {
                return set_error(error, "source is not valid UTF-8 at byte %zu", position);
            }
            second = data[position + 1u];
            if ((first == 0xf0u && second < 0x90u) ||
                (first == 0xf4u && second > 0x8fu)) {
                return set_error(error, "source is not valid UTF-8 at byte %zu", position);
            }
            position += 4u;
            continue;
        }
        return set_error(error, "source is not valid UTF-8 at byte %zu", position);
    }
    return true;
}

static bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

static bool is_hex_digit(char value)
{
    return is_digit(value) || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool is_binary_digit(char value)
{
    return value == '0' || value == '1';
}

static bool is_octal_digit(char value)
{
    return value >= '0' && value <= '7';
}

static bool is_identifier_start(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

static bool is_identifier_char(char value)
{
    return is_identifier_start(value) || is_digit(value);
}

static char upper_ascii(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }
    return value;
}

static bool equal_ascii_n(const char *left, size_t left_length, const char *right, size_t right_length)
{
    size_t index;

    if (left_length != right_length) {
        return false;
    }
    for (index = 0u; index < left_length; ++index) {
        if (upper_ascii(left[index]) != upper_ascii(right[index])) {
            return false;
        }
    }
    return true;
}

static bool equal_ascii(const char *left, const char *right)
{
    return equal_ascii_n(left, strlen(left), right, strlen(right));
}

static size_t skip_spaces(const char *text, size_t position, size_t length)
{
    while (position < length && is_space(text[position])) {
        ++position;
    }
    return position;
}

static size_t trim_end(const char *text, size_t length)
{
    while (length > 0u && is_space(text[length - 1u])) {
        --length;
    }
    return length;
}

static void trim_in_place(char *text)
{
    size_t length = strlen(text);
    size_t start = skip_spaces(text, 0u, length);
    size_t end = trim_end(text, length);

    if (start >= end) {
        text[0] = '\0';
        return;
    }
    if (start != 0u) {
        memmove(text, text + start, end - start);
        end -= start;
    }
    text[end] = '\0';
}

static void decimal_init(Decimal *decimal)
{
    decimal->digits = NULL;
    decimal->length = 0u;
    decimal->capacity = 0u;
}

static void decimal_clear(Decimal *decimal)
{
    free(decimal->digits);
    decimal->digits = NULL;
    decimal->length = 0u;
    decimal->capacity = 0u;
}

static bool decimal_normalize(Decimal *decimal)
{
    while (decimal->length > 1u && decimal->digits[decimal->length - 1u] == 0u) {
        --decimal->length;
    }
    return true;
}

static bool decimal_mul_add(Decimal *decimal, unsigned int base, unsigned int digit)
{
    unsigned int carry = digit;
    size_t index;

    if (decimal->length == SIZE_MAX) {
        return false;
    }
    if (decimal->length == 0u) {
        if (!grow_array((void **)&decimal->digits, &decimal->capacity, 1u, sizeof(unsigned char))) {
            return false;
        }
        decimal->length = 1u;
        decimal->digits[0] = 0u;
    }

    for (index = 0u; index < decimal->length; ++index) {
        unsigned int value = (unsigned int)decimal->digits[index] * base + carry;
        decimal->digits[index] = (unsigned char)(value % 10u);
        carry = value / 10u;
    }
    while (carry != 0u) {
        if (!grow_array((void **)&decimal->digits, &decimal->capacity,
                        decimal->length + 1u, sizeof(unsigned char))) {
            return false;
        }
        decimal->digits[decimal->length] = (unsigned char)(carry % 10u);
        decimal->length += 1u;
        carry /= 10u;
    }
    return true;
}

static bool decimal_to_text(const Decimal *decimal, const char *digits, size_t *index, char *output)
{
    size_t position = *index;
    size_t reverse;

    for (reverse = decimal->length; reverse > 0u; --reverse) {
        output[position] = digits[decimal->digits[reverse - 1u]];
        ++position;
    }
    *index = position;
    return true;
}

static char *number_make_text(const Decimal *decimal, bool negative, Error *error)
{
    char *text;
    size_t length;
    size_t extra = negative ? 2u : 1u;
    size_t index = 0u;

    if (decimal->length > SIZE_MAX - extra) {
        set_error(error, "number is too large");
        return NULL;
    }
    length = decimal->length + extra;
    text = malloc(length);
    if (text == NULL) {
        set_error(error, "out of memory");
        return NULL;
    }
    if (negative) {
        text[index] = '-';
        ++index;
    }
    if (!decimal_to_text(decimal, "0123456789", &index, text)) {
        free(text);
        set_error(error, "number conversion failed");
        return NULL;
    }
    text[index] = '\0';
    if (index == 0u) {
        free(text);
        set_error(error, "number conversion failed");
        return NULL;
    }
    return text;
}

static bool span_starts_with(const char *text, size_t length, const char *prefix)
{
    size_t prefix_length = strlen(prefix);
    return length >= prefix_length &&
           equal_ascii_n(text, prefix_length, prefix, prefix_length);
}

static bool span_ends_with(const char *text, size_t length, const char *suffix)
{
    size_t suffix_length = strlen(suffix);
    return suffix_length <= length &&
           equal_ascii_n(text + length - suffix_length, suffix_length, suffix, suffix_length);
}

static unsigned int digit_value(char value)
{
    if (value >= '0' && value <= '9') {
        return (unsigned int)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (unsigned int)(value - 'a' + 10);
    }
    return (unsigned int)(value - 'A' + 10);
}

static NumberStatus parse_number(const char *token, Number *number, Error *error)
{
    const char *body;
    size_t body_length;
    bool negative = false;
    unsigned int base = 10u;
    size_t digit_start = 0u;
    bool allow_leading_underscore = false;
    Decimal decimal;
    size_t index;
    bool have_digit = false;
    bool previous_underscore = false;

    number->decimal = NULL;
    number->negative = false;
    body = token;
    body_length = strlen(token);
    if (body_length > 0u && body[0] == '-') {
        negative = true;
        ++body;
        --body_length;
        while (body_length > 0u && is_space(body[0])) {
            ++body;
            --body_length;
        }
        body_length = trim_end(body, body_length);
    }

    if (body_length >= 2u && span_starts_with(body, body_length, "0x")) {
        base = 16u;
        digit_start = 2u;
        allow_leading_underscore = true;
    } else if (body_length > 0u && span_ends_with(body, body_length, "h")) {
        base = 16u;
        digit_start = 0u;
        body_length -= 1u;
    } else if (body_length >= 2u && span_starts_with(body, body_length, "0b")) {
        base = 2u;
        digit_start = 2u;
        allow_leading_underscore = true;
    } else if (body_length >= 2u && span_starts_with(body, body_length, "0o")) {
        base = 8u;
        digit_start = 2u;
        allow_leading_underscore = true;
    } else {
        for (index = 0u; index < body_length; ++index) {
            if (!is_digit(body[index])) {
                return NUMBER_INVALID;
            }
        }
        if (body_length == 0u) {
            return NUMBER_INVALID;
        }
    }

    decimal_init(&decimal);
    for (index = digit_start; index < body_length; ++index) {
        char value = body[index];
        bool valid_digit;
        unsigned int numeric;

        if (value == '_') {
            if (previous_underscore || (!have_digit && !allow_leading_underscore) ||
                index + 1u >= body_length) {
                decimal_clear(&decimal);
                return NUMBER_INVALID;
            }
            previous_underscore = true;
            continue;
        }
        previous_underscore = false;
        if (base == 16u) {
            valid_digit = is_hex_digit(value);
        } else if (base == 8u) {
            valid_digit = is_octal_digit(value);
        } else if (base == 2u) {
            valid_digit = is_binary_digit(value);
        } else {
            valid_digit = is_digit(value);
        }
        if (!valid_digit) {
            decimal_clear(&decimal);
            return NUMBER_INVALID;
        }
        numeric = digit_value(value);
        if (!decimal_mul_add(&decimal, base, numeric)) {
            decimal_clear(&decimal);
            set_error(error, "out of memory");
            return NUMBER_ERROR;
        }
        have_digit = true;
    }

    if (!have_digit) {
        decimal_clear(&decimal);
        return NUMBER_INVALID;
    }
    decimal_normalize(&decimal);
    if (decimal.length == 1u && decimal.digits[0] == 0u) {
        negative = false;
    }
    number->negative = negative;
    number->decimal = number_make_text(&decimal, negative, error);
    decimal_clear(&decimal);
    if (number->decimal == NULL) {
        return NUMBER_ERROR;
    }
    return NUMBER_OK;
}

static void number_clear(Number *number)
{
    free(number->decimal);
    number->decimal = NULL;
    number->negative = false;
}

static bool number_is_u16(const Number *number, uint16_t *value)
{
    size_t index;
    uint32_t parsed = 0u;

    if (number->negative) {
        if (strcmp(number->decimal, "0") == 0) {
            *value = 0u;
            return true;
        }
        return false;
    }
    if (number->decimal[0] == '\0') {
        return false;
    }
    for (index = 0u; number->decimal[index] != '\0'; ++index) {
        uint32_t digit;
        if (index >= 5u) {
            return false;
        }
        digit = (uint32_t)(number->decimal[index] - '0');
        if (parsed > (UINT32_C(65535) - digit) / UINT32_C(10)) {
            return false;
        }
        parsed = parsed * UINT32_C(10) + digit;
    }
    *value = (uint16_t)parsed;
    return true;
}

static bool require_u16(const Number *number, size_t line_number, const char *what, Error *error)
{
    uint16_t value;

    if (!number_is_u16(number, &value)) {
        return set_error(error, "line %zu: %s out of 16-bit range: %s", line_number,
                         what, number->decimal);
    }
    return true;
}

static void label_table_init(LabelTable *table)
{
    table->items = NULL;
    table->count = 0u;
    table->capacity = 0u;
}

static void label_table_clear(LabelTable *table)
{
    size_t index;

    for (index = 0u; index < table->count; ++index) {
        free(table->items[index].name);
    }
    free(table->items);
    table->items = NULL;
    table->count = 0u;
    table->capacity = 0u;
}

static Label *label_find(const LabelTable *table, const char *name)
{
    size_t index;

    for (index = 0u; index < table->count; ++index) {
        if (equal_ascii(table->items[index].name, name)) {
            return &table->items[index];
        }
    }
    return NULL;
}

static bool label_add(LabelTable *table, const char *name, size_t address, size_t line_number, Error *error)
{
    Label *label;

    if (label_find(table, name) != NULL) {
        return set_error(error, "line %zu: duplicate label: %s", line_number, name);
    }
    if (!grow_array((void **)&table->items, &table->capacity, table->count + 1u, sizeof(Label))) {
        return set_error(error, "out of memory");
    }
    label = &table->items[table->count];
    label->name = copy_upper_n(name, strlen(name));
    if (label->name == NULL) {
        return set_error(error, "out of memory");
    }
    label->address = address;
    ++table->count;
    return true;
}

static void source_line_init(SourceLine *line)
{
    line->line_number = 0u;
    line->labels = NULL;
    line->label_count = 0u;
    line->label_capacity = 0u;
    line->mnemonic = NULL;
    line->operands = NULL;
    line->operand_count = 0u;
    line->operand_capacity = 0u;
    line->kind = LINE_EMPTY;
}

static void source_line_clear(SourceLine *line)
{
    size_t index;

    for (index = 0u; index < line->label_count; ++index) {
        free(line->labels[index]);
    }
    for (index = 0u; index < line->operand_count; ++index) {
        free(line->operands[index]);
    }
    free(line->labels);
    free(line->operands);
    free(line->mnemonic);
    source_line_init(line);
}

static bool source_line_add_label(SourceLine *line, char *label)
{
    if (!grow_array((void **)&line->labels, &line->label_capacity,
                    line->label_count + 1u, sizeof(char *))) {
        free(label);
        return false;
    }
    line->labels[line->label_count] = label;
    ++line->label_count;
    return true;
}

static bool source_line_add_operand(SourceLine *line, const char *value, size_t length)
{
    char *operand;

    if (!grow_array((void **)&line->operands, &line->operand_capacity,
                    line->operand_count + 1u, sizeof(char *))) {
        return false;
    }
    operand = copy_n(value, length);
    if (operand == NULL) {
        return false;
    }
    line->operands[line->operand_count] = operand;
    ++line->operand_count;
    return true;
}

static void source_lines_init(SourceLines *lines)
{
    lines->items = NULL;
    lines->count = 0u;
    lines->capacity = 0u;
}

static void source_lines_clear(SourceLines *lines)
{
    size_t index;

    for (index = 0u; index < lines->count; ++index) {
        source_line_clear(&lines->items[index]);
    }
    free(lines->items);
    lines->items = NULL;
    lines->count = 0u;
    lines->capacity = 0u;
}

static bool source_lines_append(SourceLines *lines, SourceLine *line)
{
    if (!grow_array((void **)&lines->items, &lines->capacity,
                    lines->count + 1u, sizeof(SourceLine))) {
        return false;
    }
    lines->items[lines->count] = *line;
    ++lines->count;
    source_line_init(line);
    return true;
}

static bool add_empty_line(SourceLines *lines, size_t line_number, Error *error)
{
    SourceLine line;

    source_line_init(&line);
    line.line_number = line_number;
    if (!source_lines_append(lines, &line)) {
        source_line_clear(&line);
        return set_error(error, "out of memory");
    }
    return true;
}

static bool is_word_directive(const char *text, size_t length)
{
    bool has_dot = length > 0u && text[0] == '.';
    size_t word_start = has_dot ? 1u : 0u;
    size_t word_length = 4u;

    if (length < word_start + word_length ||
        !equal_ascii_n(text + word_start, word_length, "word", word_length)) {
        return false;
    }
    return length == word_start + word_length || !is_identifier_char(text[word_start + word_length]);
}

static bool is_section_line(const char *text)
{
    size_t length = strlen(text);
    size_t name_length;

    if (length >= 5u && equal_ascii_n(text, 5u, ".text", 5u)) {
        name_length = 5u;
    } else if (length >= 5u && equal_ascii_n(text, 5u, ".data", 5u)) {
        name_length = 5u;
    } else {
        return false;
    }
    return length == name_length || is_space(text[name_length]);
}

static int entry_line_name(const char *text, char **name)
{
    size_t length = strlen(text);
    size_t position = 0u;
    size_t start;
    size_t end;
    char *upper;

    if (length < 6u || !equal_ascii_n(text, 6u, ".entry", 6u) || !is_space(text[6])) {
        return 0;
    }
    position = skip_spaces(text, 6u, length);
    if (position == length || !is_identifier_start(text[position])) {
        return 0;
    }
    start = position;
    while (position < length && is_identifier_char(text[position])) {
        ++position;
    }
    end = position;
    position = skip_spaces(text, position, length);
    if (position != length) {
        return 0;
    }
    upper = copy_upper_n(text + start, end - start);
    if (upper == NULL) {
        return -1;
    }
    *name = upper;
    return 1;
}

static int take_label(const char *text, size_t length, size_t *position, char **name)
{
    size_t cursor = skip_spaces(text, *position, length);
    size_t start;
    size_t end;

    if (cursor >= length || !is_identifier_start(text[cursor])) {
        return 0;
    }
    start = cursor;
    while (cursor < length && is_identifier_char(text[cursor])) {
        ++cursor;
    }
    if (cursor >= length || text[cursor] != ':') {
        return 0;
    }
    end = cursor;
    ++cursor;
    *name = copy_upper_n(text + start, end - start);
    if (*name == NULL) {
        return -1;
    }
    *position = skip_spaces(text, cursor, length);
    return 1;
}

static bool split_operands(SourceLine *line, const char *text, size_t length, Error *error)
{
    size_t start = skip_spaces(text, 0u, length);
    size_t end = trim_end(text, length);
    size_t piece_start;

    if (start == end) {
        return true;
    }
    piece_start = start;
    while (piece_start <= end) {
        size_t comma;
        size_t piece_end;
        piece_start = skip_spaces(text, piece_start, end);
        comma = piece_start;
        while (comma < end && text[comma] != ',') {
            ++comma;
        }
        piece_end = comma;
        while (piece_end > piece_start && is_space(text[piece_end - 1u])) {
            --piece_end;
        }
        if (!source_line_add_operand(line, text + piece_start, piece_end - piece_start)) {
            return set_error(error, "out of memory");
        }
        if (comma == end) {
            break;
        }
        piece_start = comma + 1u;
    }
    return true;
}

static bool parse_source_line(SourceLine *line, const char *text, size_t line_number, Error *error)
{
    size_t length = strlen(text);
    size_t position = 0u;
    char *label;
    int label_result;
    size_t mnemonic_start;
    size_t mnemonic_end;
    const char *rest;
    size_t rest_length;

    source_line_init(line);
    line->line_number = line_number;

    while ((label_result = take_label(text, length, &position, &label)) > 0) {
        if (!source_line_add_label(line, label)) {
            return set_error(error, "out of memory");
        }
    }
    if (label_result < 0) {
        return set_error(error, "out of memory");
    }
    position = skip_spaces(text, position, length);
    if (position == length) {
        line->kind = LINE_EMPTY;
        return true;
    }

    mnemonic_start = position;
    while (position < length && !is_space(text[position])) {
        ++position;
    }
    mnemonic_end = position;
    line->mnemonic = copy_upper_n(text + mnemonic_start, mnemonic_end - mnemonic_start);
    if (line->mnemonic == NULL) {
        return set_error(error, "out of memory");
    }
    rest = text + position;
    rest_length = length - position;
    if (is_word_directive(text + mnemonic_start, mnemonic_end - mnemonic_start)) {
        line->kind = LINE_WORD;
    } else {
        line->kind = LINE_INSTRUCTION;
    }
    if (!split_operands(line, rest, rest_length, error)) {
        return false;
    }
    return true;
}

static bool preprocess_source(const ByteBuffer *source, SourceLines *lines, char **entry_label, Error *error)
{
    size_t position = 0u;
    size_t line_number = 1u;

    while (position < source->length) {
        size_t start = position;
        size_t end;
        char *line;
        char *entry_name = NULL;
        int entry_result;
        SourceLine parsed;

        while (position < source->length && !is_line_break(source->data[position])) {
            ++position;
        }
        end = position;
        if (position < source->length) {
            if (source->data[position] == '\r' && position + 1u < source->length &&
                source->data[position + 1u] == '\n') {
                position += 2u;
            } else {
                ++position;
            }
        }
        line = copy_n((const char *)source->data + start, end - start);
        if (line == NULL) {
            return set_error(error, "out of memory");
        }
        {
            char *comment = strpbrk(line, ";#");
            if (comment != NULL) {
                *comment = '\0';
            }
        }
        trim_in_place(line);
        if (line[0] == '\0') {
            free(line);
            ++line_number;
            continue;
        }

        entry_result = entry_line_name(line, &entry_name);
        if (entry_result < 0) {
            free(line);
            return set_error(error, "out of memory");
        }
        if (entry_result > 0) {
            if (*entry_label != NULL) {
                free(entry_name);
                free(line);
                return set_error(error, "line %zu: duplicate .entry directive", line_number);
            }
            *entry_label = entry_name;
            if (!add_empty_line(lines, line_number, error)) {
                free(entry_name);
                free(line);
                return false;
            }
            free(line);
            ++line_number;
            continue;
        }
        if (is_section_line(line)) {
            if (!add_empty_line(lines, line_number, error)) {
                free(line);
                return false;
            }
            free(line);
            ++line_number;
            continue;
        }
        if (!parse_source_line(&parsed, line, line_number, error)) {
            free(line);
            source_line_clear(&parsed);
            return false;
        }
        free(line);
        if (!source_lines_append(lines, &parsed)) {
            source_line_clear(&parsed);
            return set_error(error, "out of memory");
        }
        ++line_number;
    }
    return true;
}

static const Opcode *find_opcode(const char *name)
{
    size_t index;

    for (index = 0u; index < sizeof(opcodes) / sizeof(opcodes[0]); ++index) {
        if (equal_ascii(opcodes[index].name, name)) {
            return &opcodes[index];
        }
    }
    return NULL;
}

static int find_register(const char *name)
{
    size_t index;

    for (index = 0u; index < sizeof(registers) / sizeof(registers[0]); ++index) {
        if (equal_ascii(registers[index], name)) {
            return (int)index;
        }
    }
    return -1;
}

static bool is_jump_opcode(const char *name)
{
    return equal_ascii(name, "JMP") || equal_ascii(name, "JZ") || equal_ascii(name, "JNZ") ||
           equal_ascii(name, "JN") || equal_ascii(name, "JP") || equal_ascii(name, "JC") ||
           equal_ascii(name, "JNC") || equal_ascii(name, "JV") || equal_ascii(name, "CALL");
}

static bool is_one_register_opcode(const char *name)
{
    return equal_ascii(name, "RED") || equal_ascii(name, "WRT") || equal_ascii(name, "PUSH") ||
           equal_ascii(name, "POP");
}

static bool is_no_operand_opcode(const char *name)
{
    return equal_ascii(name, "MNXT") || equal_ascii(name, "MPRV") || equal_ascii(name, "ADD") ||
           equal_ascii(name, "SUB") || equal_ascii(name, "DIV") || equal_ascii(name, "INC") ||
           equal_ascii(name, "DEC") || equal_ascii(name, "AND") || equal_ascii(name, "OR") ||
           equal_ascii(name, "XOR") || equal_ascii(name, "NOT") || equal_ascii(name, "SHL") ||
           equal_ascii(name, "SHR") || equal_ascii(name, "CMP") || equal_ascii(name, "RET") ||
           equal_ascii(name, "NOP") || equal_ascii(name, "HLT");
}

static bool parse_register_operand(const char *token, int *value, Error *error)
{
    int register_value = find_register(token);

    if (register_value < 0) {
        return set_error(error, "invalid register: %s", token);
    }
    *value = register_value;
    return true;
}

static bool parse_value_operand(const char *token, const LabelTable *labels, size_t line_number,
                                const char *range_what, uint16_t *value, Error *error)
{
    Number number;
    NumberStatus status = parse_number(token, &number, error);

    if (status == NUMBER_ERROR) {
        return false;
    }
    if (status == NUMBER_OK) {
        bool valid = number_is_u16(&number, value);
        if (!valid) {
            bool result = set_error(error, "line %zu: %s: %s",
                                    line_number, range_what, number.decimal);
            number_clear(&number);
            return result;
        }
        number_clear(&number);
        return true;
    }

    {
        Label *label = label_find(labels, token);
        if (label == NULL) {
            return set_error(error, "line %zu: invalid value or unknown label: %s",
                             line_number, token);
        }
        if (label->address > 0xffffu) {
            return set_error(error, "line %zu: address out of 16-bit range: %zu",
                             line_number, label->address);
        }
        *value = (uint16_t)label->address;
    }
    return true;
}

static bool encode_line(const SourceLine *line, const LabelTable *labels, size_t pc,
                        EncodedWords *encoded, Error *error)
{
    const Opcode *opcode = find_opcode(line->mnemonic);
    int register_value;
    int source_register;
    int destination_register;
    uint16_t value;
    uint16_t target_word;
    Number number;
    NumberStatus number_status;

    encoded->count = 0u;
    if (opcode == NULL) {
        return set_error(error, "line %zu: unknown instruction: %s",
                         line->line_number, line->mnemonic);
    }

    if (equal_ascii(line->mnemonic, "MOV")) {
        if (line->operand_count != 2u) {
            return set_error(error, "line %zu: MOV requires source,destination", line->line_number);
        }
        if (!parse_register_operand(line->operands[0], &source_register, error) ||
            !parse_register_operand(line->operands[1], &destination_register, error)) {
            return false;
        }
        if (destination_register == find_register("IR")) {
            return set_error(error, "line %zu: IR cannot be a MOV destination", line->line_number);
        }
        encoded->words[0] = (uint16_t)((opcode->opcode << 11) |
                                      ((unsigned int)source_register << 7) |
                                      ((unsigned int)destination_register << 3));
        encoded->count = 1u;
        return true;
    }

    if (equal_ascii(line->mnemonic, "LDI")) {
        if (line->operand_count != 2u) {
            return set_error(error, "line %zu: LDI immediate,destination", line->line_number);
        }
        number_status = parse_number(line->operands[0], &number, error);
        if (number_status == NUMBER_ERROR) {
            return false;
        }
        if (number_status == NUMBER_OK) {
            if (!require_u16(&number, line->line_number, "immediate", error)) {
                number_clear(&number);
                return false;
            }
            value = 0u;
            if (!number_is_u16(&number, &value)) {
                number_clear(&number);
                return set_error(error, "line %zu: immediate out of 16-bit range", line->line_number);
            }
            number_clear(&number);
        } else {
            Label *label = label_find(labels, line->operands[0]);
            if (label == NULL) {
                return set_error(error, "line %zu: invalid value or unknown label: %s",
                                 line->line_number, line->operands[0]);
            }
            value = (uint16_t)label->address;
        }
        if (!parse_register_operand(line->operands[1], &register_value, error)) {
            return false;
        }
        if (register_value < find_register("R0") || register_value > find_register("R7")) {
            return set_error(error, "line %zu: LDI destination must be R0-R7", line->line_number);
        }
        encoded->words[0] = (uint16_t)((opcode->opcode << 11) |
                                      ((unsigned int)register_value << 7));
        encoded->words[1] = value;
        encoded->count = 2u;
        return true;
    }

    if (is_one_register_opcode(line->mnemonic)) {
        if (line->operand_count != 1u) {
            return set_error(error, "line %zu: %s requires one register",
                             line->line_number, line->mnemonic);
        }
        if (!parse_register_operand(line->operands[0], &register_value, error)) {
            return false;
        }
        encoded->words[0] = (uint16_t)((opcode->opcode << 11) |
                                      ((unsigned int)register_value << 7));
        encoded->count = 1u;
        return true;
    }

    if (is_jump_opcode(line->mnemonic)) {
        if (line->operand_count != 1u) {
            return set_error(error, "line %zu: %s requires target register or label",
                             line->line_number, line->mnemonic);
        }
        target_word = (uint16_t)(opcode->opcode << 11);
        register_value = find_register(line->operands[0]);
        if (register_value >= 0) {
            encoded->words[0] = (uint16_t)(target_word |
                                           ((unsigned int)register_value << 7));
            encoded->count = 1u;
            return true;
        }
        {
            Label *label = label_find(labels, line->operands[0]);
            if (label == NULL) {
                return set_error(error, "line %zu: unknown label: %s",
                                 line->line_number, line->operands[0]);
            }
            if (label->address > 0xffffu) {
                return set_error(error, "line %zu: address out of 16-bit range: %zu",
                                 line->line_number, label->address);
            }
            value = (uint16_t)label->address;
        }
        if (equal_ascii(line->mnemonic, "CALL")) {
            uint16_t ldi_target = (uint16_t)(0x01u << 11);
            uint16_t ldi_return = (uint16_t)(0x01u << 11);
            size_t return_address = pc + 5u;
            if (return_address > 0xffffu) {
                return set_error(error, "line %zu: return address out of 16-bit range: %zu",
                                 line->line_number, return_address);
            }
            encoded->words[0] = (uint16_t)(ldi_target | ((unsigned int)find_register("R7") << 7));
            encoded->words[1] = value;
            encoded->words[2] = (uint16_t)(ldi_return | ((unsigned int)find_register("R6") << 7));
            encoded->words[3] = (uint16_t)return_address;
            encoded->words[4] = (uint16_t)(target_word | ((unsigned int)find_register("R7") << 7));
            encoded->count = 5u;
        } else {
            uint16_t ldi_target = (uint16_t)(0x01u << 11);
            encoded->words[0] = (uint16_t)(ldi_target | ((unsigned int)find_register("R7") << 7));
            encoded->words[1] = value;
            encoded->words[2] = (uint16_t)(target_word | ((unsigned int)find_register("R7") << 7));
            encoded->count = 3u;
        }
        return true;
    }

    if (is_no_operand_opcode(line->mnemonic)) {
        if (line->operand_count != 0u) {
            return set_error(error, "line %zu: %s takes no operands",
                             line->line_number, line->mnemonic);
        }
        encoded->words[0] = (uint16_t)(opcode->opcode << 11);
        encoded->count = 1u;
        return true;
    }

    return set_error(error, "line %zu: assembler has no encoding rule for %s",
                     line->line_number, line->mnemonic);
}

static void word_list_init(WordList *words)
{
    words->items = NULL;
    words->count = 0u;
    words->capacity = 0u;
}

static void word_list_clear(WordList *words)
{
    free(words->items);
    words->items = NULL;
    words->count = 0u;
    words->capacity = 0u;
}

static bool word_list_append(WordList *words, uint16_t value, Error *error)
{
    if (words->count >= MAX_PROGRAM_WORDS) {
        return set_error(error, "program exceeds 64 Ki words");
    }
    if (!grow_array((void **)&words->items, &words->capacity,
                    words->count + 1u, sizeof(uint16_t))) {
        return set_error(error, "out of memory");
    }
    words->items[words->count] = value;
    ++words->count;
    return true;
}

static bool add_pc(size_t *pc, size_t amount, bool *overflow)
{
    if (*overflow) {
        return true;
    }
    if (amount > SIZE_MAX - *pc) {
        *overflow = true;
        *pc = SIZE_MAX;
        return true;
    }
    *pc += amount;
    return true;
}

static bool first_pass(const SourceLines *lines, LabelTable *labels, Error *error)
{
    size_t pc = 0u;
    bool overflow = false;
    size_t line_index;

    for (line_index = 0u; line_index < lines->count; ++line_index) {
        const SourceLine *line = &lines->items[line_index];
        size_t label_index;

        for (label_index = 0u; label_index < line->label_count; ++label_index) {
            if (pc > 0xffffu) {
                return set_error(error, "line %zu: label %s address out of 16-bit range: %zu",
                                 line->line_number, line->labels[label_index], pc);
            }
            if (!label_add(labels, line->labels[label_index], pc, line->line_number, error)) {
                return false;
            }
        }
        if (line->kind == LINE_EMPTY) {
            continue;
        }
        if (line->kind == LINE_WORD) {
            if (line->operand_count == 0u) {
                return set_error(error, "line %zu: .word requires a value", line->line_number);
            }
            if (!add_pc(&pc, line->operand_count, &overflow)) {
                return false;
            }
            continue;
        }
        if (find_opcode(line->mnemonic) == NULL) {
            return set_error(error, "line %zu: unknown instruction: %s",
                             line->line_number, line->mnemonic);
        }
        if (equal_ascii(line->mnemonic, "LDI")) {
            if (!add_pc(&pc, 2u, &overflow)) {
                return false;
            }
        } else if (!add_pc(&pc, 1u, &overflow)) {
            return false;
        }
        if (is_jump_opcode(line->mnemonic) && line->operand_count == 1u &&
            find_register(line->operands[0]) < 0) {
            if (!add_pc(&pc, equal_ascii(line->mnemonic, "CALL") ? 4u : 2u, &overflow)) {
                return false;
            }
        }
    }
    if (overflow || pc > MAX_PROGRAM_WORDS) {
        return set_error(error, "program exceeds 64 Ki words");
    }
    return true;
}

static bool second_pass(const SourceLines *lines, const LabelTable *labels, WordList *words, Error *error)
{
    size_t line_index;

    for (line_index = 0u; line_index < lines->count; ++line_index) {
        const SourceLine *line = &lines->items[line_index];
        size_t operand_index;
        EncodedWords encoded;

        if (line->kind == LINE_EMPTY) {
            continue;
        }
        if (line->kind == LINE_WORD) {
            for (operand_index = 0u; operand_index < line->operand_count; ++operand_index) {
                uint16_t value;
                if (!parse_value_operand(line->operands[operand_index], labels,
                                         line->line_number, ".word value out of range", &value, error)) {
                    return false;
                }
                if (!word_list_append(words, value, error)) {
                    return false;
                }
            }
            continue;
        }
        if (!encode_line(line, labels, words->count, &encoded, error)) {
            return false;
        }
        for (operand_index = 0u; operand_index < encoded.count; ++operand_index) {
            if (!word_list_append(words, encoded.words[operand_index], error)) {
                return false;
            }
        }
    }
    return true;
}

static void assembly_init(Assembly *assembly)
{
    word_list_init(&assembly->words);
    label_table_init(&assembly->labels);
    assembly->entry_address = 0u;
}

static void assembly_clear(Assembly *assembly)
{
    word_list_clear(&assembly->words);
    label_table_clear(&assembly->labels);
    assembly->entry_address = 0u;
}

static bool assemble_buffer(const ByteBuffer *source, Assembly *assembly, Error *error)
{
    SourceLines lines;
    char *entry_label = NULL;
    bool success = false;

    source_lines_init(&lines);
    if (!preprocess_source(source, &lines, &entry_label, error)) {
        goto cleanup;
    }
    if (!first_pass(&lines, &assembly->labels, error)) {
        goto cleanup;
    }
    if (entry_label == NULL) {
        assembly->entry_address = 0u;
    } else {
        Label *entry = label_find(&assembly->labels, entry_label);
        if (entry == NULL) {
            set_error(error, "unknown .entry label: %s", entry_label);
            goto cleanup;
        }
        assembly->entry_address = entry->address;
    }
    if (!second_pass(&lines, &assembly->labels, &assembly->words, error)) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(entry_label);
    source_lines_clear(&lines);
    return success;
}

static bool set_io_error(Error *error, int error_number, const char *path)
{
    return set_error(error, "[Errno %d] %s: '%s'", error_number, strerror(error_number), path);
}

static bool read_source(const char *path, ByteBuffer *source, Error *error)
{
    FILE *file = fopen(path, "rb");
    unsigned char chunk[8192];
    size_t count;

    if (file == NULL) {
        int error_number = errno;
        return set_io_error(error, error_number, path);
    }
    while ((count = fread(chunk, 1u, sizeof(chunk), file)) != 0u) {
        if (!buffer_append(source, chunk, count)) {
            fclose(file);
            return set_error(error, "out of memory");
        }
    }
    if (ferror(file) != 0) {
        int error_number = errno;
        fclose(file);
        return set_io_error(error, error_number, path);
    }
    if (fclose(file) != 0) {
        int error_number = errno;
        return set_io_error(error, error_number, path);
    }
    return validate_utf8(source->data, source->length, error);
}

static bool write_bits(FILE *file, uint16_t word, char *buffer)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        buffer[index] = (word & (uint16_t)(1u << (15u - index))) != 0u ? '1' : '0';
    }
    buffer[8] = ' ';
    for (index = 8u; index < 16u; ++index) {
        buffer[index + 1u] = (word & (uint16_t)(1u << (15u - index))) != 0u ? '1' : '0';
    }
    buffer[17] = '\n';
    return fwrite(buffer, 1u, 18u, file) == 18u;
}

static bool write_output(const char *path, OutputFormat format, const Assembly *assembly, Error *error)
{
    FILE *file = fopen(path, "wb");
    bool success = true;
    size_t index;

    if (file == NULL) {
        int error_number = errno;
        return set_io_error(error, error_number, path);
    }
    if (format == FORMAT_BIN) {
        for (index = 0u; index < assembly->words.count; ++index) {
            unsigned char bytes[2];
            uint16_t word = assembly->words.items[index];
            bytes[0] = (unsigned char)(word & 0xffu);
            bytes[1] = (unsigned char)(word >> 8);
            if (fwrite(bytes, 1u, sizeof(bytes), file) != sizeof(bytes)) {
                success = false;
                break;
            }
        }
    } else if (format == FORMAT_TEXT) {
        char bits[18];
        for (index = 0u; index < assembly->words.count; ++index) {
            if (!write_bits(file, assembly->words.items[index], bits)) {
                success = false;
                break;
            }
        }
    } else if (format == FORMAT_ADDRTEXT) {
        for (index = 0u; index < assembly->words.count; ++index) {
            char bits[18];
            if (fprintf(file, "%04X: ", (unsigned int)index) < 0 ||
                !write_bits(file, assembly->words.items[index], bits)) {
                success = false;
                break;
            }
        }
    } else if (format == FORMAT_HEX) {
        for (index = 0u; index < assembly->words.count; ++index) {
            if (fprintf(file, "%04X\n", (unsigned int)assembly->words.items[index]) < 0) {
                success = false;
                break;
            }
        }
    } else {
        if (fprintf(file, "load 0000\n") < 0) {
            success = false;
        }
        for (index = 0u; success && index < assembly->words.count; index += 8u) {
            size_t chunk_start = index;
            size_t chunk_end = index + 8u;
            size_t word_index;
            if (chunk_end < chunk_start || chunk_end > assembly->words.count) {
                chunk_end = assembly->words.count;
            }
            for (word_index = chunk_start; word_index < chunk_end; ++word_index) {
                if (word_index != chunk_start && fprintf(file, " ") < 0) {
                    success = false;
                    break;
                }
                if (fprintf(file, "%04X", (unsigned int)assembly->words.items[word_index]) < 0) {
                    success = false;
                    break;
                }
            }
            if (success && fputc('\n', file) == EOF) {
                success = false;
            }
        }
        if (success && fprintf(file, ".\npc %04X\n", (unsigned int)assembly->entry_address) < 0) {
            success = false;
        }
    }
    if (fclose(file) != 0) {
        success = false;
    }
    if (!success) {
        int error_number = errno;
        return set_io_error(error, error_number, path);
    }
    return true;
}

static bool print_symbols(const Assembly *assembly, Error *error)
{
    size_t *order = NULL;
    size_t order_capacity = 0u;
    size_t index;

    if (!grow_array((void **)&order, &order_capacity, assembly->labels.count,
                    sizeof(size_t))) {
        return set_error(error, "out of memory");
    }
    for (index = 0u; index < assembly->labels.count; ++index) {
        order[index] = index;
    }
    for (index = 1u; index < assembly->labels.count; ++index) {
        size_t key = order[index];
        size_t position = index;
        while (position > 0u &&
               assembly->labels.items[order[position - 1u]].address >
               assembly->labels.items[key].address) {
            order[position] = order[position - 1u];
            --position;
        }
        order[position] = key;
    }

    printf("Entry: %04Xh\n", (unsigned int)assembly->entry_address);
    printf("Symbols:\n");
    for (index = 0u; index < assembly->labels.count; ++index) {
        size_t label_index = order[index];
        printf("  %-20s %04Xh\n", assembly->labels.items[label_index].name,
               (unsigned int)assembly->labels.items[label_index].address);
    }
    free(order);
    return true;
}

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [-h] -o OUTPUT [--format {bin,text,hex,addrtext,cmd}] [--symbols] input\n",
            program);
}

static int argument_error(const char *message, const char *program)
{
    fprintf(stderr, "%s: error: %s\n", program, message);
    print_usage(stderr, program);
    return 2;
}

static bool parse_format(const char *text, OutputFormat *format)
{
    if (strcmp(text, "bin") == 0) {
        *format = FORMAT_BIN;
        return true;
    }
    if (strcmp(text, "text") == 0) {
        *format = FORMAT_TEXT;
        return true;
    }
    if (strcmp(text, "hex") == 0) {
        *format = FORMAT_HEX;
        return true;
    }
    if (strcmp(text, "addrtext") == 0) {
        *format = FORMAT_ADDRTEXT;
        return true;
    }
    if (strcmp(text, "cmd") == 0) {
        *format = FORMAT_CMD;
        return true;
    }
    return false;
}

static bool is_long_option_prefix(const char *argument, const char *name)
{
    const char *equals = strchr(argument, '=');
    size_t argument_length = equals != NULL ? (size_t)(equals - argument) : strlen(argument);
    size_t name_length = strlen(name);
    size_t index;

    if (argument_length < 2u || argument[0] != '-' || argument[1] != '-' ||
        argument_length > name_length) {
        return false;
    }
    for (index = 0u; index < argument_length; ++index) {
        if (argument[index] != name[index]) {
            return false;
        }
    }
    return true;
}

static bool is_option_argument(const char *argument)
{
    return argument[0] == '-' && argument[1] != '\0';
}

static int parse_arguments(int argc, char **argv, const char **input, const char **output,
                           OutputFormat *format, bool *symbols)
{
    int index;
    bool end_options = false;

    *input = NULL;
    *output = NULL;
    *format = FORMAT_BIN;
    *symbols = false;

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (!end_options && strcmp(argument, "--") == 0) {
            end_options = true;
            continue;
        }
        if (!end_options && strcmp(argument, "-h") == 0) {
            return 1;
        }
        if (!end_options && is_long_option_prefix(argument, "--help")) {
            if (strchr(argument, '=') != NULL) {
                return argument_error("argument --help: ignored explicit argument", argv[0]);
            }
            return 1;
        }
        if (!end_options && is_long_option_prefix(argument, "--symbols")) {
            if (strchr(argument, '=') != NULL) {
                return argument_error("argument --symbols: ignored explicit argument", argv[0]);
            }
            *symbols = true;
            continue;
        }
        if (!end_options && is_long_option_prefix(argument, "--output")) {
            const char *value;
            const char *equals = strchr(argument, '=');
            if (equals != NULL) {
                value = equals + 1u;
            } else {
                if (index + 1 >= argc || is_option_argument(argv[index + 1])) {
                    return argument_error("argument -o/--output: expected one argument", argv[0]);
                }
                ++index;
                value = argv[index];
            }
            *output = value;
            continue;
        }
        if (!end_options && strcmp(argument, "-o") == 0) {
            if (index + 1 >= argc || is_option_argument(argv[index + 1])) {
                return argument_error("argument -o/--output: expected one argument", argv[0]);
            }
            ++index;
            *output = argv[index];
            continue;
        }
        if (!end_options && strncmp(argument, "-o", 2u) == 0 && argument[2] != '\0') {
            *output = argument + 2;
            continue;
        }
        if (!end_options && is_long_option_prefix(argument, "--format")) {
            const char *value;
            const char *equals = strchr(argument, '=');
            if (equals != NULL) {
                value = equals + 1u;
            } else {
                if (index + 1 >= argc || is_option_argument(argv[index + 1])) {
                    return argument_error("argument --format: expected one argument", argv[0]);
                }
                ++index;
                value = argv[index];
            }
            if (!parse_format(value, format)) {
                return argument_error("argument --format: invalid choice", argv[0]);
            }
            continue;
        }
        if (!end_options && argument[0] == '-' && argument[1] != '\0') {
            return argument_error("unrecognized arguments", argv[0]);
        }
        if (*input != NULL) {
            return argument_error("unrecognized arguments", argv[0]);
        }
        *input = argument;
    }

    if (*output == NULL) {
        return argument_error("the following arguments are required: -o/--output", argv[0]);
    }
    if (*input == NULL) {
        return argument_error("the following arguments are required: input", argv[0]);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    OutputFormat format = FORMAT_BIN;
    bool symbols = false;
    int argument_status;
    ByteBuffer source;
    Assembly assembly;
    Error error;
    int exit_code = 0;

    error_init(&error);
    argument_status = parse_arguments(argc, argv, &input_path, &output_path, &format, &symbols);
    if (argument_status == 1) {
        print_usage(stdout, argv[0]);
        error_clear(&error);
        return 0;
    }
    if (argument_status != 0) {
        error_clear(&error);
        return 2;
    }

    buffer_init(&source);
    assembly_init(&assembly);
    if (!read_source(input_path, &source, &error)) {
        exit_code = 1;
    } else if (!assemble_buffer(&source, &assembly, &error)) {
        exit_code = 1;
    } else if (!write_output(output_path, format, &assembly, &error)) {
        exit_code = 1;
    } else {
        printf("Assembled %zu word(s) -> %s\n", assembly.words.count, output_path);
        if (symbols && !print_symbols(&assembly, &error)) {
            exit_code = 1;
        }
    }

    if (exit_code != 0) {
        printf("Error: %s\n", error.text != NULL ? error.text : "assembly failed");
    }
    assembly_clear(&assembly);
    buffer_clear(&source);
    error_clear(&error);
    return exit_code;
}
