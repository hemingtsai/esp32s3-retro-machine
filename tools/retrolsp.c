#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RETROLSP_MAX_DEPTH 64u
#define RETROLSP_MAX_DOCUMENTS 64u
#define RETROLSP_DEFAULT_DEBOUNCE_MS 200u
#define RETROLSP_HEADER_LIMIT 8192u

typedef enum {
    RL_JSON_NULL,
    RL_JSON_BOOL,
    RL_JSON_NUMBER,
    RL_JSON_STRING,
    RL_JSON_ARRAY,
    RL_JSON_OBJECT
} RlJsonKind;

typedef struct RlJson RlJson;

struct RlJson {
    RlJsonKind kind;
    bool boolean;
    double number;
    char *text;
    RlJson **items;
    char **keys;
    size_t count;
    size_t capacity;
};

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} RlBuffer;

typedef struct {
    RlBuffer *out;
    bool first;
    bool keyed;
} RlWriter;

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
    size_t start;
} RlInput;

typedef struct {
    size_t line;
    size_t character;
} RlPosition;

typedef struct {
    size_t line;
    size_t start;
    size_t length;
    uint32_t type;
    size_t order;
} RlSemanticToken;

typedef struct {
    const char *name;
    const char *kind;
    const char *type;
    bool array;
    size_t elements;
    size_t offset;
    size_t length;
    size_t line;
    size_t column;
    size_t decl_line;
    size_t decl_column;
    size_t end_line;
    size_t end_column;
    const char *owner;
    bool has_scope;
    size_t scope;
    const RlJson *parameters;
    const char *document_text;
    size_t document_length;
} RlSymbol;

typedef struct {
    RlBuffer uri;
    RlBuffer text;
    int version;
    RlJson *analysis;
    bool analyzed;
    bool dirty;
    long long dirty_at_ms;
} RlDocument;

typedef struct {
    RlDocument *items;
    size_t count;
    size_t capacity;
    const char *compiler;
    unsigned int debounce_ms;
    bool running;
    bool shutdown_requested;
    int exit_code;
    unsigned int serial;
} RlServer;

enum {
    RL_TOKEN_TYPE = 0u,
    RL_TOKEN_VARIABLE = 1u,
    RL_TOKEN_PARAMETER = 2u,
    RL_TOKEN_FUNCTION = 3u,
    RL_TOKEN_ARRAY = 4u,
    RL_TOKEN_KEYWORD = 5u,
    RL_TOKEN_NUMBER = 6u,
    RL_TOKEN_COMMENT = 7u,
    RL_TOKEN_OPERATOR = 8u,
    RL_TOKEN_PUNCTUATION = 9u,
    RL_TOKEN_RESERVED = 10u,
    RL_TOKEN_INVALID = 11u,
    RL_TOKEN_TYPE_COUNT = 12u
};

static void rl_buffer_clear(RlBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool rl_buffer_reserve(RlBuffer *buffer, size_t additional)
{
    size_t needed;
    size_t capacity;
    unsigned char *data;

    if (additional > SIZE_MAX - buffer->length - 1u) {
        return false;
    }
    needed = buffer->length + additional + 1u;
    if (needed <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
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
    if (buffer->length == 0u) {
        buffer->data[0] = 0u;
    }
    return true;
}

static bool rl_buffer_append(RlBuffer *buffer, const void *data, size_t length)
{
    if (!rl_buffer_reserve(buffer, length)) {
        return false;
    }
    if (length != 0u) {
        memcpy(buffer->data + buffer->length, data, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = 0u;
    return true;
}

static bool rl_buffer_puts(RlBuffer *buffer, const char *text)
{
    return rl_buffer_append(buffer, text, strlen(text));
}

static bool rl_buffer_printf(RlBuffer *buffer, const char *format, ...)
{
    va_list arguments;
    va_list measuring;
    int needed;

    va_start(arguments, format);
    va_copy(measuring, arguments);
    needed = vsnprintf(NULL, 0, format, measuring);
    va_end(measuring);
    if (needed < 0) {
        va_end(arguments);
        return false;
    }
    if (!rl_buffer_reserve(buffer, (size_t)needed)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf((char *)buffer->data + buffer->length, (size_t)needed + 1u, format, arguments);
    va_end(arguments);
    buffer->length += (size_t)needed;
    return true;
}

static char *rl_string_dup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    length = strlen(text);
    copy = malloc(length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1u);
    return copy;
}

static void rl_json_free(RlJson *value)
{
    size_t index;

    if (value == NULL) {
        return;
    }
    free(value->text);
    for (index = 0u; index < value->count; ++index) {
        rl_json_free(value->items[index]);
        if (value->keys != NULL) {
            free(value->keys[index]);
        }
    }
    free(value->items);
    free(value->keys);
    free(value);
}

static RlJson *rl_json_new(RlJsonKind kind)
{
    RlJson *value = calloc(1u, sizeof(RlJson));

    if (value != NULL) {
        value->kind = kind;
    }
    return value;
}

static bool rl_json_grow(RlJson *value)
{
    size_t next = value->capacity == 0u ? 8u : value->capacity * 2u;
    RlJson **items;

    if (next < value->capacity || next > SIZE_MAX / sizeof(RlJson *)) {
        return false;
    }
    items = realloc(value->items, next * sizeof(RlJson *));
    if (items == NULL) {
        return false;
    }
    value->items = items;
    if (value->kind == RL_JSON_OBJECT) {
        char **keys = realloc(value->keys, next * sizeof(char *));
        if (keys == NULL) {
            return false;
        }
        value->keys = keys;
    }
    value->capacity = next;
    return true;
}

static bool rl_json_append(RlJson *value, RlJson *item, const char *key)
{
    if (value->count == value->capacity && !rl_json_grow(value)) {
        return false;
    }
    if (value->kind == RL_JSON_OBJECT) {
        value->keys[value->count] = key != NULL ? rl_string_dup(key) : NULL;
        if (key != NULL && value->keys[value->count] == NULL) {
            return false;
        }
    }
    value->items[value->count++] = item;
    return true;
}

static void rl_json_skip_space(const char **cursor)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\n' || **cursor == '\r') {
        ++*cursor;
    }
}

static bool rl_json_hex4(const char *text, unsigned int *value)
{
    unsigned int result = 0u;
    size_t index;

    for (index = 0u; index < 4u; ++index) {
        unsigned char digit = (unsigned char)text[index];
        result *= 16u;
        if (digit >= '0' && digit <= '9') {
            result += (unsigned int)(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            result += (unsigned int)(digit - 'a') + 10u;
        } else if (digit >= 'A' && digit <= 'F') {
            result += (unsigned int)(digit - 'A') + 10u;
        } else {
            return false;
        }
    }
    *value = result;
    return true;
}

static bool rl_buffer_put_utf8(RlBuffer *buffer, unsigned int code)
{
    unsigned char encoded[4];
    size_t length;

    if (code < 0x80u) {
        encoded[0] = (unsigned char)code;
        length = 1u;
    } else if (code < 0x800u) {
        encoded[0] = (unsigned char)(0xc0u | (code >> 6));
        encoded[1] = (unsigned char)(0x80u | (code & 0x3fu));
        length = 2u;
    } else if (code < 0x10000u) {
        encoded[0] = (unsigned char)(0xe0u | (code >> 12));
        encoded[1] = (unsigned char)(0x80u | ((code >> 6) & 0x3fu));
        encoded[2] = (unsigned char)(0x80u | (code & 0x3fu));
        length = 3u;
    } else {
        encoded[0] = (unsigned char)(0xf0u | (code >> 18));
        encoded[1] = (unsigned char)(0x80u | ((code >> 12) & 0x3fu));
        encoded[2] = (unsigned char)(0x80u | ((code >> 6) & 0x3fu));
        encoded[3] = (unsigned char)(0x80u | (code & 0x3fu));
        length = 4u;
    }
    return rl_buffer_append(buffer, encoded, length);
}

static bool rl_json_parse_string(const char **cursor, char **result)
{
    RlBuffer buffer = {0};
    bool ok = false;

    if (**cursor != '"') {
        return false;
    }
    ++*cursor;
    for (;;) {
        unsigned char value = (unsigned char)**cursor;
        if (value == '\0') {
            break;
        }
        ++*cursor;
        if (value == '"') {
            ok = true;
            break;
        }
        if (value != '\\') {
            if (!rl_buffer_append(&buffer, &value, 1u)) {
                break;
            }
            continue;
        }
        value = (unsigned char)**cursor;
        if (value == '\0') {
            break;
        }
        ++*cursor;
        switch (value) {
        case '"': ok = rl_buffer_append(&buffer, "\"", 1u); break;
        case '\\': ok = rl_buffer_append(&buffer, "\\", 1u); break;
        case '/': ok = rl_buffer_append(&buffer, "/", 1u); break;
        case 'b': ok = rl_buffer_append(&buffer, "\b", 1u); break;
        case 'f': ok = rl_buffer_append(&buffer, "\f", 1u); break;
        case 'n': ok = rl_buffer_append(&buffer, "\n", 1u); break;
        case 'r': ok = rl_buffer_append(&buffer, "\r", 1u); break;
        case 't': ok = rl_buffer_append(&buffer, "\t", 1u); break;
        case 'u': {
            unsigned int code;
            if (strlen(*cursor) < 4u || !rl_json_hex4(*cursor, &code)) {
                ok = false;
                break;
            }
            *cursor += 4;
            if (code >= 0xd800u && code <= 0xdbffu && (*cursor)[0] == '\\' &&
                (*cursor)[1] == 'u' && strlen(*cursor) >= 6u) {
                unsigned int low;
                if (rl_json_hex4(*cursor + 2, &low) && low >= 0xdc00u && low <= 0xdfffu) {
                    code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
                    *cursor += 6;
                }
            }
            ok = rl_buffer_put_utf8(&buffer, code);
            break;
        }
        default:
            ok = false;
            break;
        }
        if (!ok) {
            break;
        }
    }
    if (ok) {
        if (buffer.data == NULL && !rl_buffer_reserve(&buffer, 0u)) {
            ok = false;
        }
    }
    if (ok) {
        *result = (char *)buffer.data;
        buffer.data = NULL;
    }
    rl_buffer_clear(&buffer);
    return ok;
}

static RlJson *rl_json_parse_value(const char **cursor, unsigned int depth);

static RlJson *rl_json_parse_object(const char **cursor, unsigned int depth)
{
    RlJson *value = rl_json_new(RL_JSON_OBJECT);

    if (value == NULL) {
        return NULL;
    }
    ++*cursor;
    rl_json_skip_space(cursor);
    if (**cursor == '}') {
        ++*cursor;
        return value;
    }
    for (;;) {
        char *key = NULL;
        RlJson *item;

        rl_json_skip_space(cursor);
        if (!rl_json_parse_string(cursor, &key)) {
            rl_json_free(value);
            return NULL;
        }
        rl_json_skip_space(cursor);
        if (**cursor != ':') {
            free(key);
            rl_json_free(value);
            return NULL;
        }
        ++*cursor;
        item = rl_json_parse_value(cursor, depth + 1u);
        if (item == NULL || !rl_json_append(value, item, key)) {
            free(key);
            rl_json_free(item);
            rl_json_free(value);
            return NULL;
        }
        free(key);
        rl_json_skip_space(cursor);
        if (**cursor == ',') {
            ++*cursor;
            continue;
        }
        if (**cursor == '}') {
            ++*cursor;
            return value;
        }
        rl_json_free(value);
        return NULL;
    }
}

static RlJson *rl_json_parse_array(const char **cursor, unsigned int depth)
{
    RlJson *value = rl_json_new(RL_JSON_ARRAY);

    if (value == NULL) {
        return NULL;
    }
    ++*cursor;
    rl_json_skip_space(cursor);
    if (**cursor == ']') {
        ++*cursor;
        return value;
    }
    for (;;) {
        RlJson *item = rl_json_parse_value(cursor, depth + 1u);
        if (item == NULL || !rl_json_append(value, item, NULL)) {
            rl_json_free(item);
            rl_json_free(value);
            return NULL;
        }
        rl_json_skip_space(cursor);
        if (**cursor == ',') {
            ++*cursor;
            continue;
        }
        if (**cursor == ']') {
            ++*cursor;
            return value;
        }
        rl_json_free(value);
        return NULL;
    }
}

static RlJson *rl_json_parse_value(const char **cursor, unsigned int depth)
{
    RlJson *value;

    if (depth > RETROLSP_MAX_DEPTH) {
        return NULL;
    }
    rl_json_skip_space(cursor);
    if (**cursor == '{') {
        return rl_json_parse_object(cursor, depth);
    }
    if (**cursor == '[') {
        return rl_json_parse_array(cursor, depth);
    }
    if (**cursor == '"') {
        char *text = NULL;
        if (!rl_json_parse_string(cursor, &text)) {
            return NULL;
        }
        value = rl_json_new(RL_JSON_STRING);
        if (value == NULL) {
            free(text);
            return NULL;
        }
        value->text = text;
        return value;
    }
    if (strncmp(*cursor, "true", 4u) == 0) {
        *cursor += 4;
        value = rl_json_new(RL_JSON_BOOL);
        if (value != NULL) {
            value->boolean = true;
        }
        return value;
    }
    if (strncmp(*cursor, "false", 5u) == 0) {
        *cursor += 5;
        return rl_json_new(RL_JSON_BOOL);
    }
    if (strncmp(*cursor, "null", 4u) == 0) {
        *cursor += 4;
        return rl_json_new(RL_JSON_NULL);
    }
    {
        char *end = NULL;
        double number = strtod(*cursor, &end);
        if (end == *cursor) {
            return NULL;
        }
        *cursor = end;
        value = rl_json_new(RL_JSON_NUMBER);
        if (value != NULL) {
            value->number = number;
        }
        return value;
    }
}

static RlJson *rl_json_parse(const char *text)
{
    const char *cursor = text;
    RlJson *value = rl_json_parse_value(&cursor, 0u);

    if (value == NULL) {
        return NULL;
    }
    rl_json_skip_space(&cursor);
    if (*cursor != '\0') {
        rl_json_free(value);
        return NULL;
    }
    return value;
}

static RlJson *rl_json_get(const RlJson *value, const char *key)
{
    size_t index;

    if (value == NULL || value->kind != RL_JSON_OBJECT || value->keys == NULL) {
        return NULL;
    }
    for (index = 0u; index < value->count; ++index) {
        if (value->keys[index] != NULL && strcmp(value->keys[index], key) == 0) {
            return value->items[index];
        }
    }
    return NULL;
}

static const char *rl_json_text(const RlJson *value)
{
    if (value == NULL || value->kind != RL_JSON_STRING) {
        return NULL;
    }
    return value->text;
}

static long long rl_json_integer(const RlJson *value, long long fallback)
{
    if (value == NULL || value->kind != RL_JSON_NUMBER) {
        return fallback;
    }
    return (long long)value->number;
}

static bool rl_json_flag(const RlJson *value, bool fallback)
{
    if (value == NULL || value->kind != RL_JSON_BOOL) {
        return fallback;
    }
    return value->boolean;
}

static const RlJson *rl_json_at(const RlJson *value, size_t index)
{
    if (value == NULL || value->kind != RL_JSON_ARRAY || index >= value->count) {
        return NULL;
    }
    return value->items[index];
}

static size_t rl_json_length(const RlJson *value)
{
    if (value == NULL || value->kind != RL_JSON_ARRAY) {
        return 0u;
    }
    return value->count;
}

static const char *rl_field_text(const RlJson *value, const char *key)
{
    return rl_json_text(rl_json_get(value, key));
}

static size_t rl_field_size(const RlJson *value, const char *key, size_t fallback)
{
    const RlJson *field = rl_json_get(value, key);

    if (field == NULL || field->kind != RL_JSON_NUMBER) {
        return fallback;
    }
    if (field->number < 0.0) {
        return fallback;
    }
    return (size_t)field->number;
}

static bool rl_field_flag(const RlJson *value, const char *key, bool fallback)
{
    return rl_json_flag(rl_json_get(value, key), fallback);
}

static bool rl_write_json_string(RlBuffer *out, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (!rl_buffer_append(out, "\"", 1u)) {
        return false;
    }
    for (; *cursor != '\0'; ++cursor) {
        char escape[8];
        const char *piece = escape;
        size_t length;

        switch (*cursor) {
        case '"': piece = "\\\""; length = 2u; break;
        case '\\': piece = "\\\\"; length = 2u; break;
        case '\b': piece = "\\b"; length = 2u; break;
        case '\f': piece = "\\f"; length = 2u; break;
        case '\n': piece = "\\n"; length = 2u; break;
        case '\r': piece = "\\r"; length = 2u; break;
        case '\t': piece = "\\t"; length = 2u; break;
        default:
            if (*cursor < 0x20u) {
                (void)snprintf(escape, sizeof(escape), "\\u%04X", (unsigned int)*cursor);
                length = 6u;
            } else {
                escape[0] = (char)*cursor;
                length = 1u;
            }
            break;
        }
        if (!rl_buffer_append(out, piece, length)) {
            return false;
        }
    }
    return rl_buffer_append(out, "\"", 1u);
}

static bool rl_writer_before(RlWriter *writer)
{
    if (writer->keyed) {
        writer->keyed = false;
        return true;
    }
    if (!writer->first && !rl_buffer_append(writer->out, ",", 1u)) {
        return false;
    }
    writer->first = false;
    return true;
}

static bool rl_writer_key(RlWriter *writer, const char *name)
{
    if (!rl_writer_before(writer)) {
        return false;
    }
    if (!rl_write_json_string(writer->out, name) || !rl_buffer_append(writer->out, ":", 1u)) {
        return false;
    }
    writer->keyed = true;
    return true;
}

static bool rl_writer_raw(RlWriter *writer, const char *text)
{
    return rl_writer_before(writer) && rl_buffer_puts(writer->out, text);
}

static bool rl_writer_string(RlWriter *writer, const char *text)
{
    return rl_writer_before(writer) && rl_write_json_string(writer->out, text);
}

static bool rl_writer_number(RlWriter *writer, long long value)
{
    return rl_writer_before(writer) && rl_buffer_printf(writer->out, "%lld", value);
}

static bool rl_writer_bool(RlWriter *writer, bool value)
{
    return rl_writer_raw(writer, value ? "true" : "false");
}

static bool rl_writer_null(RlWriter *writer)
{
    return rl_writer_raw(writer, "null");
}

static bool rl_writer_open(RlWriter *writer, char bracket)
{
    if (!rl_writer_before(writer) || !rl_buffer_append(writer->out, &bracket, 1u)) {
        return false;
    }
    writer->first = true;
    writer->keyed = false;
    return true;
}

static bool rl_writer_close(RlWriter *writer, char bracket)
{
    if (!rl_buffer_append(writer->out, &bracket, 1u)) {
        return false;
    }
    writer->first = false;
    writer->keyed = false;
    return true;
}

static bool rl_writer_position(RlWriter *writer, RlPosition position)
{
    return rl_writer_open(writer, '{') && rl_writer_key(writer, "line") &&
           rl_writer_number(writer, (long long)position.line) && rl_writer_key(writer, "character") &&
           rl_writer_number(writer, (long long)position.character) && rl_writer_close(writer, '}');
}

static bool rl_writer_range(RlWriter *writer, RlPosition start, RlPosition end)
{
    return rl_writer_open(writer, '{') && rl_writer_key(writer, "start") &&
           rl_writer_position(writer, start) && rl_writer_key(writer, "end") &&
           rl_writer_position(writer, end) && rl_writer_close(writer, '}');
}

static bool rl_is_identifier_byte(unsigned char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static size_t rl_utf8_sequence(unsigned char lead)
{
    if (lead < 0x80u) {
        return 1u;
    }
    if ((lead & 0xe0u) == 0xc0u) {
        return 2u;
    }
    if ((lead & 0xf0u) == 0xe0u) {
        return 3u;
    }
    if ((lead & 0xf8u) == 0xf0u) {
        return 4u;
    }
    return 1u;
}

static size_t rl_utf16_units(const char *text, size_t length)
{
    const unsigned char *data = (const unsigned char *)text;
    size_t units = 0u;
    size_t index = 0u;

    while (index < length) {
        size_t step = rl_utf8_sequence(data[index]);
        if (index + step > length) {
            step = 1u;
        }
        units += 1u;
        index += step;
    }
    return units;
}

static RlPosition rl_position_of(const char *text, size_t length, size_t offset)
{
    RlPosition position = {0u, 0u};
    size_t line_start = 0u;
    size_t index;

    if (offset > length) {
        offset = length;
    }
    for (index = 0u; index < offset; ++index) {
        if (text[index] == '\n') {
            ++position.line;
            line_start = index + 1u;
        }
    }
    position.character = rl_utf16_units(text + line_start, offset - line_start);
    return position;
}

static size_t rl_offset_of(const char *text, size_t length, RlPosition position)
{
    size_t line = 0u;
    size_t index = 0u;
    size_t units = 0u;

    while (line < position.line && index < length) {
        if (text[index] == '\n') {
            ++line;
            units = 0u;
        }
        ++index;
    }
    if (line < position.line) {
        return length;
    }
    while (index < length && units < position.character && text[index] != '\n') {
        index += rl_utf8_sequence((unsigned char)text[index]);
        ++units;
    }
    if (index > length) {
        return length;
    }
    return index;
}

static RlPosition rl_request_position(const RlJson *position)
{
    RlPosition result = {0u, 0u};

    result.line = (size_t)rl_json_integer(rl_json_get(position, "line"), 0);
    result.character = (size_t)rl_json_integer(rl_json_get(position, "character"), 0);
    return result;
}

static bool rl_write_message(FILE *stream, const char *body, size_t length)
{
    if (fprintf(stream, "Content-Length: %zu\r\n\r\n", length) < 0) {
        return false;
    }
    if (length != 0u && fwrite(body, 1u, length, stream) != length) {
        return false;
    }
    return fflush(stream) == 0;
}

static long long rl_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool rl_input_compact(RlInput *input)
{
    size_t keep = input->length - input->start;
    size_t next;
    unsigned char *grown;

    if (input->start != 0u) {
        memmove(input->data, input->data + input->start, keep);
        input->start = 0u;
        input->length = keep;
    }
    if (input->length < input->capacity) {
        return true;
    }
    next = input->capacity == 0u ? 4096u : input->capacity * 2u;
    if (next < input->capacity) {
        return false;
    }
    grown = realloc(input->data, next);
    if (grown == NULL) {
        return false;
    }
    input->data = grown;
    input->capacity = next;
    return true;
}

static bool rl_input_fill(RlInput *input)
{
    ssize_t count;

    for (;;) {
        if (!rl_input_compact(input)) {
            return false;
        }
        count = read(STDIN_FILENO, input->data + input->length,
                     input->capacity - input->length);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        input->length += (size_t)count;
        return true;
    }
}

static bool rl_input_need(RlInput *input, size_t needed)
{
    while (input->length < needed) {
        if (!rl_input_fill(input)) {
            return false;
        }
    }
    return true;
}

static bool rl_input_skip(RlInput *input, size_t bytes)
{
    input->start += bytes;
    if (input->start >= input->length) {
        input->start = 0u;
        input->length = 0u;
        return true;
    }
    return false;
}

static bool rl_input_next(RlInput *input, RlBuffer *message, bool *eof)
{
    *eof = false;
    for (;;) {
        size_t header_end = 0u;
        size_t content_length = 0u;
        bool have_length = false;
        size_t position;
        size_t scan;

        for (scan = input->start; scan + 4u <= input->length; ++scan) {
            if (input->data[scan] == '\r' && input->data[scan + 1u] == '\n' &&
                input->data[scan + 2u] == '\r' && input->data[scan + 3u] == '\n') {
                header_end = scan + 4u;
                break;
            }
        }
        if (header_end == 0u) {
            if (input->length - input->start >= RETROLSP_HEADER_LIMIT) {
                input->start = 0u;
                input->length = 0u;
                return true;
            }
            if (!rl_input_fill(input)) {
                *eof = true;
                return true;
            }
            continue;
        }
        position = input->start;
        while (position + 1u < header_end) {
            size_t line_end = position;
            while (line_end + 1u < header_end &&
                   !(input->data[line_end] == '\r' && input->data[line_end + 1u] == '\n')) {
                ++line_end;
            }
            if (line_end + 2u <= position) {
                break;
            }
            if (line_end - position > 15u &&
                memcmp(input->data + position, "Content-Length:", 15u) == 0) {
                size_t digits = position + 15u;
                size_t value = 0u;
                while (digits < line_end &&
                       (input->data[digits] == ' ' || input->data[digits] == '\t')) {
                    ++digits;
                }
                if (digits >= line_end) {
                    break;
                }
                for (; digits < line_end; ++digits) {
                    unsigned char digit = input->data[digits];
                    if (digit < '0' || digit > '9') {
                        break;
                    }
                    if (value > (SIZE_MAX - (size_t)(digit - '0')) / 10u) {
                        break;
                    }
                    value = value * 10u + (size_t)(digit - '0');
                }
                if (digits == line_end) {
                    content_length = value;
                    have_length = true;
                }
            }
            position = line_end + 2u;
        }
        if (!have_length) {
            if (rl_input_skip(input, header_end - input->start)) {
                return true;
            }
            continue;
        }
        if (!rl_input_need(input, header_end + content_length)) {
            *eof = true;
            return true;
        }
        message->length = 0u;
        if (!rl_buffer_append(message, input->data + header_end, content_length)) {
            return false;
        }
        (void)rl_input_skip(input, header_end + content_length - input->start);
        return true;
    }
}

static bool rl_input_ready(const RlInput *input)
{
    size_t available = input->length;
    size_t scan;
    size_t header_end = 0u;
    size_t content_length = 0u;
    bool have_length = false;
    size_t position;

    for (scan = input->start; scan + 4u <= available; ++scan) {
        if (input->data[scan] == '\r' && input->data[scan + 1u] == '\n' &&
            input->data[scan + 2u] == '\r' && input->data[scan + 3u] == '\n') {
            header_end = scan + 4u;
            break;
        }
    }
    if (header_end == 0u) {
        return false;
    }
    position = input->start;
    while (position + 1u < header_end) {
        size_t line_end = position;
        while (line_end + 1u < header_end &&
               !(input->data[line_end] == '\r' && input->data[line_end + 1u] == '\n')) {
            ++line_end;
        }
        if (line_end + 2u <= position) {
            break;
        }
        if (line_end - position > 15u &&
            memcmp(input->data + position, "Content-Length:", 15u) == 0) {
            size_t digits = position + 15u;
            size_t value = 0u;
            while (digits < line_end &&
                   (input->data[digits] == ' ' || input->data[digits] == '\t')) {
                ++digits;
            }
            if (digits >= line_end) {
                return false;
            }
            for (; digits < line_end; ++digits) {
                unsigned char digit = input->data[digits];
                if (digit < '0' || digit > '9') {
                    return false;
                }
                value = value * 10u + (size_t)(digit - '0');
            }
            content_length = value;
            have_length = true;
        }
        position = line_end + 2u;
    }
    if (!have_length) {
        return false;
    }
    return available >= header_end + content_length;
}

static RlDocument *rl_document_find(RlServer *server, const char *uri)
{
    size_t index;

    for (index = 0u; index < server->count; ++index) {
        if (strcmp((const char *)server->items[index].uri.data, uri) == 0) {
            return &server->items[index];
        }
    }
    return NULL;
}

static RlDocument *rl_document_store(RlServer *server, const char *uri, const char *text,
                                     size_t length, int version)
{
    RlDocument *document = rl_document_find(server, uri);

    if (document == NULL) {
        if (server->count >= RETROLSP_MAX_DOCUMENTS) {
            return NULL;
        }
        if (server->count == server->capacity) {
            size_t next = server->capacity == 0u ? 8u : server->capacity * 2u;
            RlDocument *items = realloc(server->items, next * sizeof(RlDocument));
            if (items == NULL) {
                return NULL;
            }
            memset(items + server->capacity, 0, (next - server->capacity) * sizeof(RlDocument));
            server->items = items;
            server->capacity = next;
        }
        document = &server->items[server->count++];
        memset(document, 0, sizeof(*document));
        if (!rl_buffer_append(&document->uri, uri, strlen(uri))) {
            return NULL;
        }
    }
    rl_json_free(document->analysis);
    document->analysis = NULL;
    document->analyzed = false;
    document->version = version;
    document->text.length = 0u;
    if (!rl_buffer_append(&document->text, text, length)) {
        return NULL;
    }
    document->dirty = true;
    document->dirty_at_ms = rl_now_ms();
    return document;
}

static void rl_document_drop(RlServer *server, const char *uri)
{
    size_t index;

    for (index = 0u; index < server->count; ++index) {
        if (strcmp((const char *)server->items[index].uri.data, uri) != 0) {
            continue;
        }
        rl_buffer_clear(&server->items[index].uri);
        rl_buffer_clear(&server->items[index].text);
        rl_json_free(server->items[index].analysis);
        memmove(&server->items[index], &server->items[index + 1u],
                (server->count - index - 1u) * sizeof(RlDocument));
        --server->count;
        return;
    }
}

static void rl_server_clear(RlServer *server)
{
    while (server->count != 0u) {
        rl_document_drop(server, (const char *)server->items[server->count - 1u].uri.data);
    }
    free(server->items);
    server->items = NULL;
    server->capacity = 0u;
}

static bool rl_shell_quote(RlBuffer *buffer, const char *text)
{
    if (!rl_buffer_append(buffer, "'", 1u)) {
        return false;
    }
    for (; *text != '\0'; ++text) {
        if (*text == '\'') {
            if (!rl_buffer_puts(buffer, "'\\''")) {
                return false;
            }
            continue;
        }
        if (!rl_buffer_append(buffer, text, 1u)) {
            return false;
        }
    }
    return rl_buffer_append(buffer, "'", 1u);
}

static bool rl_run_analysis(RlServer *server, RlDocument *document)
{
    RlBuffer path = {0};
    RlBuffer inner = {0};
    RlBuffer command = {0};
    RlBuffer result = {0};
    bool ok = false;
    FILE *file;
    FILE *pipe;
    char name[128];

    (void)snprintf(name, sizeof(name), "/tmp/retrolsp-%ld-%u.rc", (long)getpid(),
                   server->serial++);
    if (!rl_buffer_puts(&path, name)) {
        goto cleanup;
    }
    file = fopen((const char *)path.data, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    if (document->text.length != 0u &&
        fwrite(document->text.data, 1u, document->text.length, file) != document->text.length) {
        fclose(file);
        remove((const char *)path.data);
        goto cleanup;
    }
    if (fclose(file) != 0) {
        remove((const char *)path.data);
        goto cleanup;
    }
    if (!rl_buffer_puts(&inner, server->compiler) || !rl_buffer_puts(&inner, " --analyze -o - ") ||
        !rl_shell_quote(&inner, (const char *)path.data)) {
        goto cleanup;
    }
    if (!rl_buffer_puts(&command, "sh -c ") || !rl_shell_quote(&command, (const char *)inner.data)) {
        goto cleanup;
    }
    pipe = popen((const char *)command.data, "r");
    if (pipe == NULL) {
        goto cleanup;
    }
    for (;;) {
        unsigned char chunk[4096];
        size_t count = fread(chunk, 1u, sizeof(chunk), pipe);
        if (count != 0u && !rl_buffer_append(&result, chunk, count)) {
            break;
        }
        if (count != sizeof(chunk)) {
            break;
        }
    }
    (void)pclose(pipe);
    if (result.length == 0u) {
        goto cleanup;
    }
    rl_json_free(document->analysis);
    document->analysis = rl_json_parse((const char *)result.data);
    document->analyzed = document->analysis != NULL;
    ok = document->analyzed;
cleanup:
    if (path.length != 0u) {
        remove((const char *)path.data);
    }
    rl_buffer_clear(&path);
    rl_buffer_clear(&inner);
    rl_buffer_clear(&command);
    rl_buffer_clear(&result);
    return ok;
}

static RlSymbol *rl_collect_symbols(const RlDocument *document, size_t *count)
{
    const RlJson *symbols = rl_json_get(document->analysis, "symbols");
    size_t total = rl_json_length(symbols);
    RlSymbol *result;
    size_t index;

    *count = 0u;
    if (total == 0u) {
        return NULL;
    }
    result = calloc(total, sizeof(RlSymbol));
    if (result == NULL) {
        return NULL;
    }
    for (index = 0u; index < total; ++index) {
        const RlJson *item = rl_json_at(symbols, index);
        const RlJson *scope = rl_json_get(item, "scope");
        RlSymbol *symbol = &result[index];
        symbol->name = rl_field_text(item, "name");
        symbol->kind = rl_field_text(item, "kind");
        symbol->type = rl_field_text(item, "type");
        symbol->array = rl_field_flag(item, "array", false);
        symbol->elements = rl_field_size(item, "elements", 1u);
        symbol->offset = rl_field_size(item, "offset", 0u);
        symbol->length = rl_field_size(item, "length", 0u);
        symbol->line = rl_field_size(item, "line", 0u);
        symbol->column = rl_field_size(item, "column", 0u);
        symbol->decl_line = rl_field_size(item, "declLine", 0u);
        symbol->decl_column = rl_field_size(item, "declColumn", 0u);
        symbol->end_line = rl_field_size(item, "endLine", 0u);
        symbol->end_column = rl_field_size(item, "endColumn", 0u);
        symbol->owner = rl_field_text(item, "owner");
        symbol->has_scope = scope != NULL && scope->kind == RL_JSON_NUMBER;
        symbol->scope = symbol->has_scope ? (size_t)scope->number : 0u;
        symbol->parameters = rl_json_get(item, "parameters");
        symbol->document_text = (const char *)document->text.data;
        symbol->document_length = document->text.length;
        if (symbol->name == NULL) {
            free(result);
            return NULL;
        }
    }
    *count = total;
    return result;
}

static const RlSymbol *rl_symbol_at_offset(const RlSymbol *symbols, size_t count, size_t offset)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        if (symbols[index].offset == offset) {
            return &symbols[index];
        }
    }
    return NULL;
}

static const RlSymbol *rl_symbol_by_name(const RlSymbol *symbols, size_t count, const char *name)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        if (strcmp(symbols[index].name, name) == 0) {
            return &symbols[index];
        }
    }
    return NULL;
}

static bool rl_symbol_in_scope(const RlDocument *document, const RlSymbol *symbol, size_t offset)
{
    const RlJson *scopes = rl_json_get(document->analysis, "scopes");
    const RlJson *scope;
    size_t start;
    size_t end;

    if (!symbol->has_scope) {
        return true;
    }
    scope = rl_json_at(scopes, symbol->scope);
    if (scope == NULL) {
        return false;
    }
    start = rl_field_size(scope, "startOffset", 0u);
    end = rl_field_size(scope, "endOffset", 0u);
    if (end < start) {
        return false;
    }
    return start <= offset && offset <= end;
}

static uint32_t rl_symbol_token_type(const RlSymbol *symbol)
{
    if (symbol == NULL) {
        return RL_TOKEN_VARIABLE;
    }
    if (strcmp(symbol->kind, "function") == 0) {
        return RL_TOKEN_FUNCTION;
    }
    if (strcmp(symbol->kind, "parameter") == 0) {
        return RL_TOKEN_PARAMETER;
    }
    if (symbol->array) {
        return RL_TOKEN_ARRAY;
    }
    return RL_TOKEN_VARIABLE;
}

static const RlSymbol *rl_token_symbol(const RlDocument *document, const RlSymbol *symbols,
                                       size_t count, size_t offset, const char *text)
{
    const RlSymbol *symbol = rl_symbol_at_offset(symbols, count, offset);
    const RlJson *references;
    size_t index;

    if (symbol != NULL) {
        return symbol;
    }
    references = rl_json_get(document->analysis, "references");
    for (index = 0u; index < rl_json_length(references); ++index) {
        const RlJson *item = rl_json_at(references, index);
        if (rl_field_size(item, "offset", SIZE_MAX) == offset) {
            const char *name = rl_field_text(item, "name");
            if (name != NULL) {
                return rl_symbol_by_name(symbols, count, name);
            }
        }
    }
    if (strncmp(text, "__", 2u) == 0) {
        return NULL;
    }
    return NULL;
}

static int rl_token_compare(const void *left, const void *right)
{
    const RlSemanticToken *first = left;
    const RlSemanticToken *second = right;

    if (first->line != second->line) {
        return first->line < second->line ? -1 : 1;
    }
    if (first->start != second->start) {
        return first->start < second->start ? -1 : 1;
    }
    if (first->order != second->order) {
        return first->order < second->order ? -1 : 1;
    }
    return 0;
}

static bool rl_semantic_push(RlSemanticToken **items, size_t *count, size_t *capacity,
                             size_t line, size_t start, size_t length, uint32_t type,
                             size_t order)
{
    RlSemanticToken *token;

    if (length == 0u) {
        return true;
    }
    if (*count == *capacity) {
        size_t next = *capacity == 0u ? 128u : *capacity * 2u;
        RlSemanticToken *grown = realloc(*items, next * sizeof(RlSemanticToken));
        if (grown == NULL) {
            return false;
        }
        *items = grown;
        *capacity = next;
    }
    token = &(*items)[(*count)++];
    token->line = line;
    token->start = start;
    token->length = length;
    token->type = type;
    token->order = order;
    return true;
}

static bool rl_semantic_tokens(RlDocument *document, RlBuffer *out)
{
    RlSemanticToken *items = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    RlSymbol *symbols = NULL;
    size_t symbol_count = 0u;
    const RlJson *tokens = rl_json_get(document->analysis, "tokens");
    const RlJson *comments = rl_json_get(document->analysis, "comments");
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    size_t index;
    size_t order = 0u;
    size_t emitted = 0u;
    size_t previous_line = 0u;
    size_t previous_start = 0u;
    bool ok = true;

    symbols = rl_collect_symbols(document, &symbol_count);
    for (index = 0u; ok && index < rl_json_length(tokens); ++index) {
        const RlJson *item = rl_json_at(tokens, index);
        const char *kind = rl_field_text(item, "kind");
        const char *spelling = rl_field_text(item, "text");
        size_t offset = rl_field_size(item, "offset", 0u);
        size_t span = rl_field_size(item, "length", 0u);
        uint32_t type;
        RlPosition start;
        if (kind == NULL || spelling == NULL) {
            continue;
        }
        if (strcmp(kind, "type") == 0) {
            type = RL_TOKEN_TYPE;
        } else if (strcmp(kind, "number") == 0) {
            type = RL_TOKEN_NUMBER;
        } else if (strcmp(kind, "keyword") == 0) {
            type = RL_TOKEN_KEYWORD;
        } else if (strcmp(kind, "operator") == 0) {
            type = RL_TOKEN_OPERATOR;
        } else if (strcmp(kind, "punctuation") == 0) {
            type = RL_TOKEN_PUNCTUATION;
        } else if (strcmp(kind, "unknown") == 0) {
            type = RL_TOKEN_INVALID;
        } else {
            type = rl_symbol_token_type(
                rl_token_symbol(document, symbols, symbol_count, offset, spelling));
            if (type == RL_TOKEN_VARIABLE && strncmp(spelling, "__", 2u) == 0) {
                type = RL_TOKEN_RESERVED;
            }
        }
        start = rl_position_of(text, length, offset);
        ok = rl_semantic_push(&items, &count, &capacity, start.line, start.character,
                              rl_utf16_units(text + offset, span), type, order++);
    }
    for (index = 0u; ok && index < rl_json_length(comments); ++index) {
        const RlJson *item = rl_json_at(comments, index);
        size_t offset = rl_field_size(item, "offset", 0u);
        size_t span = rl_field_size(item, "length", 0u);
        size_t consumed = 0u;
        while (ok && consumed < span) {
            size_t newline = consumed;
            size_t chunk;
            RlPosition start;
            while (newline < span && text[offset + newline] != '\n') {
                ++newline;
            }
            chunk = newline - consumed;
            if (chunk > 0u) {
                start = rl_position_of(text, length, offset + consumed);
                ok = rl_semantic_push(&items, &count, &capacity, start.line, start.character,
                                      rl_utf16_units(text + offset + consumed, chunk),
                                      RL_TOKEN_COMMENT, order++);
            }
            if (newline >= span) {
                break;
            }
            consumed = newline + 1u;
        }
    }
    free(symbols);
    if (!ok) {
        free(items);
        return false;
    }
    qsort(items, count, sizeof(RlSemanticToken), rl_token_compare);
    {
        RlWriter writer;
        writer.out = out;
        writer.first = true;
        writer.keyed = false;
        if (!rl_writer_open(&writer, '{') || !rl_writer_key(&writer, "data") ||
            !rl_writer_open(&writer, '[')) {
            free(items);
            return false;
        }
        for (index = 0u; index < count; ++index) {
            size_t delta_line = items[index].line - previous_line;
            size_t delta_start = delta_line == 0u && index != 0u
                                     ? items[index].start - previous_start
                                     : items[index].start;
            if (!rl_writer_number(&writer, (long long)delta_line) ||
                !rl_writer_number(&writer, (long long)delta_start) ||
                !rl_writer_number(&writer, (long long)items[index].length) ||
                !rl_writer_number(&writer, (long long)items[index].type) ||
                !rl_writer_number(&writer, 0)) {
                free(items);
                return false;
            }
            previous_line = items[index].line;
            previous_start = items[index].start;
            ++emitted;
        }
        if (!rl_writer_close(&writer, ']') || !rl_writer_close(&writer, '}')) {
            free(items);
            return false;
        }
    }
    free(items);
    (void)emitted;
    return true;
}

static bool rl_symbol_signature(const RlSymbol *symbol, RlBuffer *out)
{
    size_t index;

    if (!rl_buffer_printf(out, "%s %s", symbol->type != NULL ? symbol->type : "u16",
                          symbol->name)) {
        return false;
    }
    if (strcmp(symbol->kind, "function") == 0) {
        if (!rl_buffer_puts(out, "(")) {
            return false;
        }
        for (index = 0u; index < rl_json_length(symbol->parameters); ++index) {
            const RlJson *parameter = rl_json_at(symbol->parameters, index);
            const char *name = rl_field_text(parameter, "name");
            if (index != 0u && !rl_buffer_puts(out, ", ")) {
                return false;
            }
            if (!rl_buffer_printf(out, "u16 %s", name != NULL ? name : "?")) {
                return false;
            }
        }
        if (!rl_buffer_puts(out, ")")) {
            return false;
        }
    } else if (symbol->array) {
        if (!rl_buffer_printf(out, "[%zu]", symbol->elements)) {
            return false;
        }
    }
    return true;
}

static bool rl_publish_diagnostics(RlDocument *document)
{
    RlBuffer body = {0};
    RlWriter writer;
    const RlJson *diagnostics = rl_json_get(document->analysis, "diagnostics");
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    size_t index;
    bool ok;

    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "jsonrpc") &&
         rl_writer_string(&writer, "2.0") && rl_writer_key(&writer, "method") &&
         rl_writer_string(&writer, "textDocument/publishDiagnostics") &&
         rl_writer_key(&writer, "params") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "uri") && rl_writer_string(&writer, (const char *)document->uri.data) &&
         rl_writer_key(&writer, "diagnostics") && rl_writer_open(&writer, '[');
    for (index = 0u; ok && index < rl_json_length(diagnostics); ++index) {
        const RlJson *item = rl_json_at(diagnostics, index);
        const char *message = rl_field_text(item, "message");
        long long offset = (long long)rl_field_size(item, "offset", 0u);
        long long end_offset = (long long)rl_field_size(item, "endOffset", 0u);
        RlPosition start;
        RlPosition end;
        if (end_offset < offset) {
            end_offset = offset;
        }
        start = rl_position_of(text, length, (size_t)offset);
        end = rl_position_of(text, length, (size_t)end_offset);
        ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "range") &&
             rl_writer_range(&writer, start, end) && rl_writer_key(&writer, "severity") &&
             rl_writer_number(&writer, 1) && rl_writer_key(&writer, "source") &&
             rl_writer_string(&writer, "retrocc") && rl_writer_key(&writer, "message") &&
             rl_writer_string(&writer, message != NULL ? message : "compilation failed") &&
             rl_writer_close(&writer, '}');
    }
    ok = ok && rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}') &&
         rl_writer_close(&writer, '}');
    if (ok) {
        ok = rl_write_message(stdout, (const char *)body.data, body.length);
    }
    rl_buffer_clear(&body);
    return ok;
}

static bool rl_send_empty_diagnostics(const char *uri)
{
    RlBuffer body = {0};
    RlWriter writer;
    bool ok;

    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "jsonrpc") &&
         rl_writer_string(&writer, "2.0") && rl_writer_key(&writer, "method") &&
         rl_writer_string(&writer, "textDocument/publishDiagnostics") &&
         rl_writer_key(&writer, "params") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "uri") && rl_writer_string(&writer, uri) &&
         rl_writer_key(&writer, "diagnostics") && rl_writer_open(&writer, '[') &&
         rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}') &&
         rl_writer_close(&writer, '}');
    if (ok) {
        ok = rl_write_message(stdout, (const char *)body.data, body.length);
    }
    rl_buffer_clear(&body);
    return ok;
}

static void rl_send_result(RlBuffer *id_text, const char *result)
{
    RlBuffer body = {0};
    RlWriter writer;
    bool ok;

    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "jsonrpc") &&
         rl_writer_string(&writer, "2.0") && rl_writer_key(&writer, "id");
    if (ok) {
        ok = id_text->length != 0u && rl_buffer_puts(&body, (const char *)id_text->data);
        writer.keyed = false;
    }
    ok = ok && rl_writer_key(&writer, "result");
    if (ok) {
        ok = result != NULL ? rl_buffer_puts(&body, result) : rl_writer_null(&writer);
    }
    ok = ok && rl_writer_close(&writer, '}');
    if (ok) {
        (void)rl_write_message(stdout, (const char *)body.data, body.length);
    }
    rl_buffer_clear(&body);
}

static void rl_send_error(RlBuffer *id_text, long long code, const char *message)
{
    RlBuffer body = {0};
    RlWriter writer;
    bool ok;

    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "jsonrpc") &&
         rl_writer_string(&writer, "2.0") && rl_writer_key(&writer, "id");
    if (ok) {
        ok = id_text->length != 0u && rl_buffer_puts(&body, (const char *)id_text->data);
        writer.keyed = false;
    }
    ok = ok && rl_writer_key(&writer, "error") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "code") && rl_writer_number(&writer, code) &&
         rl_writer_key(&writer, "message") && rl_writer_string(&writer, message) &&
         rl_writer_close(&writer, '}') && rl_writer_close(&writer, '}');
    if (ok) {
        (void)rl_write_message(stdout, (const char *)body.data, body.length);
    }
    rl_buffer_clear(&body);
}

static const char *const rl_keywords[] = {
    "u16", "void", "if", "else", "while", "for", "break", "continue", "return"
};

static const char *const rl_token_types[RL_TOKEN_TYPE_COUNT] = {
    "type", "variable", "parameter", "function", "property", "keyword",
    "number", "comment", "operator", "punctuation", "macro", "invalid"
};

static bool rl_result_initialize(RlBuffer *out)
{
    RlWriter writer;
    size_t index;
    bool ok;

    writer.out = out;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "capabilities") &&
         rl_writer_open(&writer, '{') && rl_writer_key(&writer, "positionEncoding") &&
         rl_writer_string(&writer, "utf-16") && rl_writer_key(&writer, "textDocumentSync") &&
         rl_writer_number(&writer, 1) && rl_writer_key(&writer, "documentSymbolProvider") &&
         rl_writer_bool(&writer, true) && rl_writer_key(&writer, "hoverProvider") &&
         rl_writer_bool(&writer, true) && rl_writer_key(&writer, "definitionProvider") &&
         rl_writer_bool(&writer, true) && rl_writer_key(&writer, "completionProvider") &&
         rl_writer_open(&writer, '{') && rl_writer_key(&writer, "triggerCharacters") &&
         rl_writer_open(&writer, '[') && rl_writer_string(&writer, "(") &&
         rl_writer_string(&writer, "[") && rl_writer_string(&writer, ".") &&
         rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}') &&
         rl_writer_key(&writer, "signatureHelpProvider") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "triggerCharacters") && rl_writer_open(&writer, '[') &&
         rl_writer_string(&writer, "(") && rl_writer_string(&writer, ",") &&
         rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}') &&
         rl_writer_key(&writer, "semanticTokensProvider") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "legend") && rl_writer_open(&writer, '{') &&
         rl_writer_key(&writer, "tokenTypes") && rl_writer_open(&writer, '[');
    for (index = 0u; ok && index < RL_TOKEN_TYPE_COUNT; ++index) {
        ok = rl_writer_string(&writer, rl_token_types[index]);
    }
    ok = ok && rl_writer_close(&writer, ']') && rl_writer_key(&writer, "tokenModifiers") &&
         rl_writer_open(&writer, '[') && rl_writer_close(&writer, ']') &&
         rl_writer_close(&writer, '}') && rl_writer_key(&writer, "full") &&
         rl_writer_bool(&writer, true) && rl_writer_close(&writer, '}') &&
         rl_writer_close(&writer, '}') && rl_writer_key(&writer, "serverInfo") &&
         rl_writer_open(&writer, '{') && rl_writer_key(&writer, "name") &&
         rl_writer_string(&writer, "retrolsp") && rl_writer_close(&writer, '}') &&
         rl_writer_close(&writer, '}');
    return ok;
}

static bool rl_write_symbol(RlWriter *writer, const RlSymbol *symbol,
                            const RlSymbol *members, size_t member_count, bool is_function)
{
    const char *text = symbol->document_text;
    RlPosition range_start;
    RlPosition range_end;
    RlPosition name_start;
    RlPosition name_end;
    RlBuffer signature = {0};
    size_t index;
    bool ok;

    range_start.line = symbol->decl_line > 0u ? symbol->decl_line - 1u : 0u;
    range_start.character = symbol->decl_column > 0u ? symbol->decl_column - 1u : 0u;
    range_end.line = symbol->end_line > 0u ? symbol->end_line - 1u : range_start.line;
    range_end.character = symbol->end_column > 0u ? symbol->end_column - 1u : 0u;
    name_start = rl_position_of(text, symbol->document_length, symbol->offset);
    name_end.line = name_start.line;
    name_end.character = name_start.character + symbol->length;
    ok = rl_symbol_signature(symbol, &signature);
    ok = ok && rl_writer_open(writer, '{') && rl_writer_key(writer, "name") &&
         rl_writer_string(writer, symbol->name) && rl_writer_key(writer, "detail") &&
         rl_writer_string(writer, (const char *)signature.data) &&
         rl_writer_key(writer, "kind") && rl_writer_number(writer, is_function ? 12 : 13) &&
         rl_writer_key(writer, "range") && rl_writer_range(writer, range_start, range_end) &&
         rl_writer_key(writer, "selectionRange") &&
         rl_writer_range(writer, name_start, name_end);
    rl_buffer_clear(&signature);
    if (ok && is_function) {
        ok = rl_writer_key(writer, "children") && rl_writer_open(writer, '[');
    }
    for (index = 0u; ok && is_function && index < member_count; ++index) {
        const RlSymbol *member = &members[index];
        if (member->owner == NULL || strcmp(member->owner, symbol->name) != 0) {
            continue;
        }
        ok = rl_write_symbol(writer, member, NULL, 0u, false);
    }
    if (ok && is_function) {
        ok = rl_writer_close(writer, ']');
    }
    return ok && rl_writer_close(writer, '}');
}

static bool rl_result_document_symbols(RlDocument *document, RlBuffer *out)
{
    RlSymbol *symbols = NULL;
    size_t symbol_count = 0u;
    size_t index;
    RlWriter writer;
    bool ok = true;

    symbols = rl_collect_symbols(document, &symbol_count);
    writer.out = out;
    writer.first = true;
    writer.keyed = false;
    if (!rl_writer_open(&writer, '[')) {
        free(symbols);
        return false;
    }
    for (index = 0u; ok && index < symbol_count; ++index) {
        const RlSymbol *symbol = &symbols[index];
        if (strcmp(symbol->kind, "function") != 0 && strcmp(symbol->kind, "global") != 0) {
            continue;
        }
        ok = rl_write_symbol(&writer, symbol, symbols, symbol_count,
                             strcmp(symbol->kind, "function") == 0);
    }
    ok = ok && rl_writer_close(&writer, ']');
    free(symbols);
    return ok;
}

static size_t rl_symbol_index_at_position(const RlDocument *document, const RlSymbol *symbols,
                                          size_t count, size_t offset)
{
    const RlJson *references;
    size_t index;

    for (index = 0u; index < count; ++index) {
        if (symbols[index].offset == offset) {
            return index;
        }
    }
    references = rl_json_get(document->analysis, "references");
    for (index = 0u; index < rl_json_length(references); ++index) {
        const RlJson *item = rl_json_at(references, index);
        size_t start = rl_field_size(item, "offset", SIZE_MAX);
        size_t span = rl_field_size(item, "length", 0u);
        const char *name;
        size_t candidate;
        if (start == SIZE_MAX || offset < start || offset > start + span) {
            continue;
        }
        name = rl_field_text(item, "name");
        if (name == NULL) {
            return SIZE_MAX;
        }
        for (candidate = 0u; candidate < count; ++candidate) {
            if (strcmp(symbols[candidate].name, name) == 0) {
                return candidate;
            }
        }
        return SIZE_MAX;
    }
    return SIZE_MAX;
}

static bool rl_result_hover(RlDocument *document, RlBuffer *id_text, const RlJson *position)
{
    RlSymbol *symbols = NULL;
    size_t count = 0u;
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    RlPosition where = rl_request_position(position);
    size_t offset = rl_offset_of(text, length, where);
    size_t found;
    const RlSymbol *symbol;
    RlBuffer signature = {0};
    RlBuffer body = {0};
    RlWriter writer;
    bool ok;

    symbols = rl_collect_symbols(document, &count);
    found = rl_symbol_index_at_position(document, symbols, count, offset);
    symbol = found == SIZE_MAX ? NULL : &symbols[found];
    if (symbol == NULL) {
        free(symbols);
        rl_send_result(id_text, "null");
        return true;
    }
    ok = rl_symbol_signature(symbol, &signature);
    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    if (ok) {
        size_t first = offset;
        size_t last = offset;
        RlPosition start;
        RlPosition end;
        while (first > 0u && rl_is_identifier_byte((unsigned char)text[first - 1u])) {
            --first;
        }
        while (last < length && rl_is_identifier_byte((unsigned char)text[last])) {
            ++last;
        }
        start = rl_position_of(text, length, first);
        end = rl_position_of(text, length, last);
        ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "contents") &&
             rl_writer_open(&writer, '{') && rl_writer_key(&writer, "kind") &&
             rl_writer_string(&writer, "markdown") && rl_writer_key(&writer, "value") &&
             rl_writer_string(&writer, (const char *)signature.data) &&
             rl_writer_close(&writer, '}') && rl_writer_key(&writer, "range") &&
             rl_writer_range(&writer, start, end) && rl_writer_close(&writer, '}');
    }
    free(symbols);
    if (ok) {
        rl_send_result(id_text, (const char *)body.data);
    } else {
        rl_send_error(id_text, -32603, "internal error");
    }
    rl_buffer_clear(&signature);
    rl_buffer_clear(&body);
    return ok;
}

static bool rl_result_definition(RlDocument *document, RlBuffer *id_text,
                                 const RlJson *position)
{
    RlSymbol *symbols = NULL;
    size_t count = 0u;
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    RlPosition where = rl_request_position(position);
    size_t offset = rl_offset_of(text, length, where);
    size_t found;
    const RlSymbol *symbol;
    RlBuffer body = {0};
    RlWriter writer;
    RlPosition start;
    RlPosition end;
    bool ok;

    symbols = rl_collect_symbols(document, &count);
    found = rl_symbol_index_at_position(document, symbols, count, offset);
    symbol = found == SIZE_MAX ? NULL : &symbols[found];
    if (symbol == NULL) {
        free(symbols);
        rl_send_result(id_text, "null");
        return true;
    }
    start = rl_position_of(text, length, symbol->offset);
    end.line = start.line;
    end.character = start.character + symbol->length;
    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "uri") &&
         rl_writer_string(&writer, (const char *)document->uri.data) &&
         rl_writer_key(&writer, "range") && rl_writer_range(&writer, start, end) &&
         rl_writer_close(&writer, '}');
    free(symbols);
    if (ok) {
        rl_send_result(id_text, (const char *)body.data);
    } else {
        rl_send_error(id_text, -32603, "internal error");
    }
    rl_buffer_clear(&body);
    return ok;
}

static bool rl_result_completion(RlDocument *document, RlBuffer *id_text, const RlJson *position)
{
    RlSymbol *symbols = NULL;
    size_t count = 0u;
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    RlPosition where = rl_request_position(position);
    size_t offset = rl_offset_of(text, length, where);
    RlBuffer body = {0};
    RlWriter writer;
    size_t index;
    bool ok;

    symbols = rl_collect_symbols(document, &count);
    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "isIncomplete") &&
         rl_writer_bool(&writer, false) && rl_writer_key(&writer, "items") &&
         rl_writer_open(&writer, '[');
    for (index = 0u; ok && index < sizeof(rl_keywords) / sizeof(rl_keywords[0]); ++index) {
        ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "label") &&
             rl_writer_string(&writer, rl_keywords[index]) && rl_writer_key(&writer, "kind") &&
             rl_writer_number(&writer, 14) && rl_writer_key(&writer, "sortText") &&
             rl_writer_string(&writer, "9") && rl_writer_close(&writer, '}');
    }
    for (index = 0u; ok && index < count; ++index) {
        const RlSymbol *symbol = &symbols[index];
        RlBuffer signature = {0};
        const char *sort;
        long long kind;
        if (!rl_symbol_in_scope(document, symbol, offset)) {
            continue;
        }
        if (strcmp(symbol->kind, "function") == 0) {
            sort = "5";
            kind = 3;
        } else if (strcmp(symbol->kind, "parameter") == 0) {
            sort = "0";
            kind = 6;
        } else if (strcmp(symbol->kind, "global") == 0) {
            sort = "6";
            kind = symbol->array ? 6 : 6;
        } else {
            sort = "1";
            kind = 6;
        }
        ok = rl_symbol_signature(symbol, &signature);
        if (ok) {
            ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "label") &&
                 rl_writer_string(&writer, symbol->name) && rl_writer_key(&writer, "kind") &&
                 rl_writer_number(&writer, kind) && rl_writer_key(&writer, "detail") &&
                 rl_writer_string(&writer, (const char *)signature.data) &&
                 rl_writer_key(&writer, "sortText") && rl_writer_string(&writer, sort) &&
                 rl_writer_close(&writer, '}');
        }
        rl_buffer_clear(&signature);
    }
    ok = ok && rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}');
    free(symbols);
    if (ok) {
        rl_send_result(id_text, (const char *)body.data);
    } else {
        rl_send_error(id_text, -32603, "internal error");
    }
    rl_buffer_clear(&body);
    return ok;
}

static bool rl_call_context(const RlDocument *document, size_t offset, const char **name,
                            size_t *active)
{
    const RlJson *tokens = rl_json_get(document->analysis, "tokens");
    long long depth = 0;
    long long commas = 0;
    long long index;
    *name = NULL;
    *active = 0u;
    for (index = (long long)rl_json_length(tokens) - 1; index >= 0; --index) {
        const RlJson *item = rl_json_at(tokens, (size_t)index);
        const char *spelling = rl_field_text(item, "text");
        size_t start = rl_field_size(item, "offset", 0u);
        if (spelling == NULL || start >= offset) {
            continue;
        }
        if (strcmp(spelling, ")") == 0) {
            ++depth;
            continue;
        }
        if (strcmp(spelling, "(") == 0) {
            if (depth == 0) {
                long long scan = index - 1;
                while (scan >= 0) {
                    const RlJson *previous = rl_json_at(tokens, (size_t)scan);
                    const char *previous_text = rl_field_text(previous, "text");
                    if (previous_text == NULL) {
                        break;
                    }
                    if (strcmp(previous_text, "identifier") == 0) {
                        break;
                    }
                    if (strcmp(previous_text, ",") == 0) {
                        --scan;
                        continue;
                    }
                    break;
                }
                if (scan >= 0) {
                    const RlJson *previous = rl_json_at(tokens, (size_t)scan);
                    const char *previous_kind = rl_field_text(previous, "kind");
                    if (previous_kind != NULL && strcmp(previous_kind, "identifier") == 0) {
                        *name = rl_field_text(previous, "text");
                        *active = (size_t)commas;
                    }
                }
                return true;
            }
            --depth;
            continue;
        }
        if (depth == 0 && strcmp(spelling, ",") == 0) {
            ++commas;
        }
    }
    return false;
}

static bool rl_result_signature_help(RlDocument *document, RlBuffer *id_text,
                                     const RlJson *position)
{
    const char *text = (const char *)document->text.data;
    size_t length = document->text.length;
    RlPosition where = rl_request_position(position);
    size_t offset = rl_offset_of(text, length, where);
    const char *name = NULL;
    size_t active = 0u;
    RlSymbol *symbols = NULL;
    size_t count = 0u;
    const RlSymbol *symbol;
    RlBuffer signature = {0};
    RlBuffer label = {0};
    RlBuffer parameters = {0};
    RlBuffer body = {0};
    RlWriter writer;
    size_t index;
    size_t total;
    bool ok;

    (void)rl_call_context(document, offset, &name, &active);
    symbols = rl_collect_symbols(document, &count);
    symbol = name != NULL ? rl_symbol_by_name(symbols, count, name) : NULL;
    if (symbol == NULL || strcmp(symbol->kind, "function") != 0) {
        free(symbols);
        rl_send_result(id_text, "null");
        return true;
    }
    ok = rl_symbol_signature(symbol, &signature);
    for (index = 0u; ok && index < rl_json_length(symbol->parameters); ++index) {
        const RlJson *parameter = rl_json_at(symbol->parameters, index);
        if (index != 0u) {
            ok = rl_buffer_puts(&parameters, ",");
        }
        if (ok) {
            ok = rl_buffer_printf(&parameters, "u16 %s",
                                  rl_field_text(parameter, "name") != NULL
                                      ? rl_field_text(parameter, "name")
                                      : "?");
        }
    }
    total = rl_json_length(symbol->parameters);
    if (ok) {
        ok = rl_buffer_printf(&label, "%s(%s)", symbol->name, (const char *)parameters.data);
    }
    writer.out = &body;
    writer.first = true;
    writer.keyed = false;
    if (ok) {
        ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "signatures") &&
             rl_writer_open(&writer, '[') && rl_writer_open(&writer, '{') &&
             rl_writer_key(&writer, "label") && rl_writer_string(&writer, (const char *)label.data) &&
             rl_writer_key(&writer, "parameters") && rl_writer_open(&writer, '[');
    }
    for (index = 0u; ok && index < total; ++index) {
        const RlJson *parameter = rl_json_at(symbol->parameters, index);
        ok = rl_writer_open(&writer, '{') && rl_writer_key(&writer, "label") &&
             rl_writer_string(&writer, rl_field_text(parameter, "name") != NULL
                                                  ? rl_field_text(parameter, "name")
                                                  : "?") && rl_writer_close(&writer, '}');
    }
    ok = ok && rl_writer_close(&writer, ']') && rl_writer_close(&writer, '}') &&
         rl_writer_close(&writer, ']') && rl_writer_key(&writer, "activeParameter") &&
         rl_writer_number(&writer, (long long)(total == 0u ? 0u : (active < total ? active
                                                                        : total - 1u))) &&
         rl_writer_close(&writer, '}');
    free(symbols);
    if (ok) {
        rl_send_result(id_text, (const char *)body.data);
    } else {
        rl_send_error(id_text, -32603, "internal error");
    }
    rl_buffer_clear(&signature);
    rl_buffer_clear(&label);
    rl_buffer_clear(&parameters);
    rl_buffer_clear(&body);
    return ok;
}

static bool rl_handle_request(RlServer *server, const RlJson *method, RlBuffer *id_text,
                              const RlJson *params)
{
    const char *name = rl_json_text(method);
    const RlJson *document_node = rl_json_get(params, "textDocument");
    const char *uri = rl_field_text(document_node, "uri");
    RlDocument *document = uri != NULL ? rl_document_find(server, uri) : NULL;
    RlBuffer result = {0};
    bool ok = true;

    if (name == NULL) {
        rl_send_error(id_text, -32600, "missing method");
        return true;
    }
    if (strcmp(name, "initialize") == 0) {
        RlBuffer result = {0};
        bool ok = rl_result_initialize(&result);
        if (ok) {
            rl_send_result(id_text, (const char *)result.data);
        } else {
            rl_send_error(id_text, -32603, "internal error");
        }
        rl_buffer_clear(&result);
        return true;
    }
    if (strcmp(name, "shutdown") == 0) {
        server->shutdown_requested = true;
        rl_send_result(id_text, "null");
        return true;
    }
    if (strcmp(name, "textDocument/semanticTokens/full") == 0 ||
        strcmp(name, "textDocument/documentSymbol") == 0 ||
        strcmp(name, "textDocument/hover") == 0 ||
        strcmp(name, "textDocument/definition") == 0 ||
        strcmp(name, "textDocument/completion") == 0 ||
        strcmp(name, "textDocument/signatureHelp") == 0) {
        if (document == NULL) {
            rl_send_error(id_text, -32602, "unknown document");
            return true;
        }
        if (document->dirty || !document->analyzed) {
            if (!rl_run_analysis(server, document)) {
                rl_send_error(id_text, -32603, "analysis failed");
                return true;
            }
            document->dirty = false;
            (void)rl_publish_diagnostics(document);
        }
    }
    if (strcmp(name, "textDocument/semanticTokens/full") == 0) {
        ok = rl_semantic_tokens(document, &result);
    } else if (strcmp(name, "textDocument/documentSymbol") == 0) {
        ok = rl_result_document_symbols(document, &result);
    } else if (strcmp(name, "textDocument/hover") == 0) {
        return rl_result_hover(document, id_text, rl_json_get(params, "position"));
    } else if (strcmp(name, "textDocument/definition") == 0) {
        return rl_result_definition(document, id_text, rl_json_get(params, "position"));
    } else if (strcmp(name, "textDocument/completion") == 0) {
        return rl_result_completion(document, id_text, rl_json_get(params, "position"));
    } else if (strcmp(name, "textDocument/signatureHelp") == 0) {
        return rl_result_signature_help(document, id_text, rl_json_get(params, "position"));
    } else {
        rl_send_error(id_text, -32601, "method not found");
        rl_buffer_clear(&result);
        return true;
    }
    if (!ok) {
        rl_send_error(id_text, -32603, "internal error");
    } else {
        rl_send_result(id_text, (const char *)result.data);
    }
    rl_buffer_clear(&result);
    return true;
}

static void rl_handle_notification(RlServer *server, const RlJson *method, const RlJson *params)
{
    const char *name = rl_json_text(method);
    const RlJson *document_node;
    const char *uri;

    if (name == NULL) {
        return;
    }
    if (strcmp(name, "exit") == 0) {
        server->running = false;
        server->exit_code = server->shutdown_requested ? 0 : 1;
        return;
    }
    if (strcmp(name, "initialized") == 0 || strcmp(name, "$/cancelRequest") == 0) {
        return;
    }
    document_node = rl_json_get(params, "textDocument");
    uri = rl_field_text(document_node, "uri");
    if (uri == NULL) {
        return;
    }
    if (strcmp(name, "textDocument/didOpen") == 0) {
        const char *text = rl_json_text(rl_json_get(document_node, "text"));
        int version = (int)rl_json_integer(rl_json_get(document_node, "version"), 0);
        if (text == NULL) {
            return;
        }
        (void)rl_document_store(server, uri, text, strlen(text), version);
        return;
    }
    if (strcmp(name, "textDocument/didChange") == 0) {
        const RlJson *changes = rl_json_get(params, "contentChanges");
        const RlJson *change = rl_json_at(changes, rl_json_length(changes) - 1u);
        const char *text = rl_field_text(change, "text");
        int version = (int)rl_json_integer(rl_json_get(document_node, "version"), 0);
        RlDocument *document;
        if (text == NULL) {
            return;
        }
        document = rl_document_find(server, uri);
        if (document == NULL) {
            (void)rl_document_store(server, uri, text, strlen(text), version);
            return;
        }
        document->text.length = 0u;
        if (rl_buffer_append(&document->text, text, strlen(text))) {
            document->version = version;
            document->dirty = true;
            document->analyzed = false;
            document->dirty_at_ms = rl_now_ms();
        }
        return;
    }
    if (strcmp(name, "textDocument/didClose") == 0) {
        rl_document_drop(server, uri);
        (void)rl_send_empty_diagnostics(uri);
        return;
    }
}

static void rl_dispatch(RlServer *server, const char *body)
{
    RlJson *message = rl_json_parse(body);
    RlJson *id = rl_json_get(message, "id");
    RlJson *method = rl_json_get(message, "method");
    RlJson *params = rl_json_get(message, "params");
    RlBuffer id_text = {0};

    if (message == NULL) {
        return;
    }
    if (id != NULL && id->kind == RL_JSON_NUMBER) {
        (void)rl_buffer_printf(&id_text, "%lld", (long long)id->number);
    } else if (id != NULL && id->kind == RL_JSON_STRING) {
        (void)rl_buffer_puts(&id_text, "\"");
        (void)rl_buffer_puts(&id_text, id->text);
        (void)rl_buffer_puts(&id_text, "\"");
    } else if (id != NULL && id->kind == RL_JSON_NULL) {
        (void)rl_buffer_puts(&id_text, "null");
    }
    if (method != NULL && method->kind == RL_JSON_STRING) {
        if (id_text.length != 0u) {
            (void)rl_handle_request(server, method, &id_text, params);
        } else {
            rl_handle_notification(server, method, params);
        }
    }
    rl_buffer_clear(&id_text);
    rl_json_free(message);
}

static bool rl_flush(RlServer *server)
{
    size_t index;

    for (index = 0u; index < server->count; ++index) {
        RlDocument *document = &server->items[index];
        if (document->dirty && !document->analyzed) {
            (void)rl_run_analysis(server, document);
            document->dirty = false;
            (void)rl_publish_diagnostics(document);
        }
    }
    return true;
}

static int rl_wait_ms(RlServer *server)
{
    long long now = rl_now_ms();
    long long wait = -1;
    size_t index;

    for (index = 0u; index < server->count; ++index) {
        RlDocument *document = &server->items[index];
        long long due;
        if (!document->dirty) {
            continue;
        }
        due = document->dirty_at_ms + (long long)server->debounce_ms - now;
        if (due < 0) {
            due = 0;
        }
        if (wait < 0 || due < wait) {
            wait = due;
        }
    }
    if (wait < 0) {
        return -1;
    }
    return (int)wait;
}

static void rl_usage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [-h] [--stdio] [--compiler PATH] [--debounce MS]\n", program);
}

int main(int argc, char **argv)
{
    RlServer server;
    RlInput input;
    RlBuffer message = {0};
    bool stdio_mode = false;
    int index;

    memset(&server, 0, sizeof(server));
    memset(&input, 0, sizeof(input));
    server.running = true;
    server.exit_code = 0;
    server.debounce_ms = RETROLSP_DEFAULT_DEBOUNCE_MS;
    server.compiler = "retrocc";
    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            rl_usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argument, "--stdio") == 0) {
            stdio_mode = true;
            continue;
        }
        if (strcmp(argument, "--compiler") == 0 && index + 1 < argc) {
            server.compiler = argv[++index];
            continue;
        }
        if (strcmp(argument, "--debounce") == 0 && index + 1 < argc) {
            long value = strtol(argv[++index], NULL, 10);
            server.debounce_ms = value < 0 ? 0u : (unsigned int)value;
            continue;
        }
        rl_usage(stderr, argv[0]);
        return 2;
    }
    (void)stdio_mode;
    while (server.running) {
        struct pollfd descriptor;
        int ready;
        bool eof = false;

        (void)rl_flush(&server);
        if (!rl_input_ready(&input)) {
            descriptor.fd = STDIN_FILENO;
            descriptor.events = POLLIN;
            descriptor.revents = 0;
            ready = poll(&descriptor, 1u, rl_wait_ms(&server));
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ready == 0) {
                continue;
            }
        }
        if (!rl_input_next(&input, &message, &eof)) {
            break;
        }
        if (eof) {
            break;
        }
        if (message.length != 0u) {
            rl_dispatch(&server, (const char *)message.data);
        }
        if (server.shutdown_requested) {
            server.running = false;
        }
    }
    rl_buffer_clear(&message);
    free(input.data);
    rl_server_clear(&server);
    return server.exit_code;
}
