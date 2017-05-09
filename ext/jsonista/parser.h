#ifndef JSONISTA_PARSER_H
#define JSONISTA_PARSER_H
typedef struct stack parser_state_stack_t;
typedef struct parser_buffer buffer_t;
typedef struct {
    parser_state_stack_t *stack;
    buffer_t *buffer;
    const char *p;
    void(*emit_number)(const char *p, const char *e);
} parser_t;

parser_t *parser_new();
void parser_init(parser_t *parser);
void parser_free(parser_t *parser);
size_t parser_memsize(parser_t *parser);

enum parse_error {
    ERR_SUCCESS = 0,
    ERR_NEEDMORE,
    ERR_INVALID,
    ERR_EXTRABYTE,
};
enum parse_error parser_parse_chunk(parser_t *parser, const char **pp, const char *e);

#endif
