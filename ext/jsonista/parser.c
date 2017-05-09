#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "parser.h"

/* parser stack */
enum parser_state {
    STATE_INIT,
    STATE_VALUE,
    STATE_OBJECT_FIRST_NAME,
    STATE_OBJECT_NAME,
    STATE_OBJECT_NAME_SEP,
    STATE_OBJECT_VALUE,
    STATE_OBJECT_VALUE_SEP,
    STATE_ARRAY_FIRST_VALUE,
    STATE_ARRAY_VALUE,
    STATE_ARRAY_VALUE_SEP,
    STATE_FINISH,
    STATE_BUG,
};

typedef struct stack {
    enum parser_state *head;
    enum parser_state *current;
    enum parser_state *end;
} parser_state_stack_t;

static parser_state_stack_t*
stack_new() {
    size_t size = 1024;
    parser_state_stack_t *p = malloc(sizeof(parser_state_stack_t));
    if (!p) abort();
    p->head = malloc(sizeof(enum parser_state) * size);
    if (!p->head) abort();
    p->current = p->head;
    p->end = p->head + size;
    *p->current = STATE_INIT;
    return p;
}

static void
stack_free(parser_state_stack_t *stack) {
    free(stack->head);
    free(stack);
}

static size_t
stack_memsize(parser_state_stack_t *stack) {
    return sizeof(parser_state_stack_t) + stack->end - stack->head;
}

static void
stack_push(parser_state_stack_t *p, enum parser_state state) {
    if (++p->current > p->end) {
	enum parser_state *ptr = realloc(p->head, (p->end - p->head) * 2);
	if (!ptr) abort();
	p->current = ptr - p->head + p->current;
	p->end = ptr - p->head + p->end;
	p->head = ptr;
    }
    *p->current = state;
}

static enum parser_state
stack_pop(parser_state_stack_t *p) {
    if (p->current == p->head) {
	return STATE_BUG;
    }
    return *p->current--;
}

static void
stack_set(parser_state_stack_t *p, enum parser_state state) {
    *p->current = state;
}

static enum parser_state
stack_peek(parser_state_stack_t *p) {
    return *p->current;
}

static void
stack_clear(parser_state_stack_t *p) {
    p->current = p->head;
    *p->current = STATE_INIT;
}

/* buffer */
typedef struct parser_buffer {
    char *buf;
    char *p;
    char *e;
} buffer_t;

static buffer_t *
buffer_new() {
    const size_t size = 4096;
    buffer_t *buf = malloc(sizeof(buffer_t));
    if (!buf) abort();
    buf->buf = malloc(size);
    if (!buf->buf) abort();
    buf->p = buf->buf;
    buf->e = buf->buf + size;
    return buf;
}

static void
buffer_free(buffer_t *buf) {
    free(buf->buf);
    free(buf);
}

static size_t
buffer_memsize(buffer_t *buf) {
    return sizeof(buffer_t) + buf->e - buf->buf;
}

static void
buffer_ensure_writable(buffer_t *buf, size_t len) {
    if (buf->p + len > buf->e) {
	char *r = realloc(buf->buf, (buf->e - buf->buf) * 2);
	if (!r) abort();
	buf->buf = r;
    }
}

static void
buffer_write(buffer_t *buf, const char *p, size_t len) {
    buffer_ensure_writable(buf, len);
    memcpy(buf->p, p, len);
    buf->p += len;
}

/* parser */
parser_t *
parser_new() {
    parser_t *parser = malloc(sizeof(parser_t));
    if (!parser) abort();
    return parser;
}

void
parser_init(parser_t *parser) {
    if (parser->stack) {
	stack_clear(parser->stack);
    } else {
	parser->stack = stack_new();
    }
    parser->p = NULL;
}

void
parser_free(parser_t *parser) {
    stack_free(parser->stack);
    free(parser);
}

size_t
parser_memsize(parser_t *parser) {
    return sizeof(parser_t) + stack_memsize(parser->stack) + buffer_memsize(parser->buffer);
}

static void
parser_state_push(parser_t *parser, enum parser_state state) {
    stack_push(parser->stack, state);
}

static enum parser_state
parser_state_pop(parser_t *parser) {
    return stack_pop(parser->stack);
}

static enum parser_state
parser_state_get(parser_t *parser) {
    return stack_peek(parser->stack);
}

static void
parser_state_set(parser_t *parser, enum parser_state state) {
    return stack_set(parser->stack, state);
}

static void
skip_ws(const char **pp, const char *e) {
    const char *p = *pp;
    while (p < e) {
	switch(*p) {
	  case ' ':
	  case '\t':
	  case '\r':
	  case '\n':
	    p++;
	    break;
	  default:
	    *pp = p;
	    return;
	}
    }
    *pp = p;
}

static enum parse_error
parse_string0(const char **pp, const char *e) {
    const char *p = *pp;
    while (p < e) {
	switch (*p) {
	  case '\\':
	    p += 2;
	    continue;
	  case '"':
	    *pp = ++p;
	    return ERR_SUCCESS;
	}
	p++;
	/* TODO: check invalid bytes */
    }
    return ERR_NEEDMORE;
}

static enum parse_error
parse_string(const char **pp, const char *e) {
    const char *p = *pp;
    skip_ws(&p, e);
    if (p < e) {
	if (*p == '"') {
	    *pp = ++p;
	    return parse_string0(pp, e);
	}
	*pp = p;
	return ERR_INVALID;
    }
    *pp = p;
    return ERR_NEEDMORE;
}

static int
js_isdigit(char c) {
    return '0' <= c && c <= '9';
}

#define POP_STACK() do { state = stack_pop(stack); goto resume; } while (0)
#define SKIP_WS(state) skip_ws(&p, e)
#define ENSURE_READABLE(n) do { \
    if (e - p < n) { \
	goto needmore; \
    } \
} while (0)
#define RAISE(e) do { \
    switch (e) { \
    case ERR_NEEDMORE: goto needmore; \
    case ERR_INVALID: goto invalid; \
    default: break; \
    } \
} while (0)
#define ISDIGIT(c) js_isdigit(c)
#define PUSH_STATE(parser, state) do { \
    parser_state_push(parser, state); \
    parser->p = p; \
} while (0)
#define POP_STATE(parser) parser_state_pop(parser)
#define SET_STATE(parser, state) do { \
    parser_state_set(parser, state); \
    parser->p = p; \
} while (0)

enum parse_error
parser_parse_chunk(parser_t *parser, const char **pp, const char *e) {
    const char *p = *pp;
    unsigned char c;

next_state:
    switch (parser_state_get(parser)) {
      case STATE_INIT:
	fprintf(stderr, "state: STATE_INIT\n");
	SET_STATE(parser, STATE_FINISH);
	PUSH_STATE(parser, STATE_VALUE);
      case STATE_VALUE:
	fprintf(stderr, "state: STATE_VALUE\n");
	goto value;
      case STATE_FINISH:
	fprintf(stderr, "state: STATE_FINISH\n");
	goto finish;
      case STATE_OBJECT_FIRST_NAME:
	goto object_first_name;
      case STATE_OBJECT_NAME:
	goto object_name;
      case STATE_OBJECT_NAME_SEP:
	goto object_name_sep;
      case STATE_OBJECT_VALUE:
	goto object_value;
      case STATE_OBJECT_VALUE_SEP:
	fprintf(stderr, "state: STATE_OBJECT_VALUE_SEP\n");
	goto object_value_sep;
      case STATE_ARRAY_FIRST_VALUE:
	fprintf(stderr, "state: STATE_ARRAY_FIRST_VALUE\n");
	goto array_first_value;
      case STATE_ARRAY_VALUE_SEP:
	fprintf(stderr, "state: STATE_ARRAY_VALUE_SEP\n");
	goto array_value_sep;
      default:
	fprintf(stderr, "unknown state: %d\n", parser_state_get(parser));
	abort();
    }

value:
    SKIP_WS();
    ENSURE_READABLE(1);
    switch (*p++) {
      case '{':
	goto object_first_name;
      case '[':
	goto array_first_value;
      case '"':
	if (parse_string0(&p, e)) {
	    // error
	}
	break;
      case '-':
	ENSURE_READABLE(1);
	switch (*p++) {
	  case '0':
	    goto frac;
	  case '1':case'2':case'3':case'4':case'5':case'6':case'7':case'8':case'9':
	    goto nonzero;
	  default:
	    RAISE(ERR_INVALID);
	}
	// int = zero / ( digit1-9 *DIGIT )
      case '0':
	goto frac;
      case '1':case'2':case'3':case'4':case'5':case'6':case'7':case'8':case'9':
nonzero:
	while (p < e && ISDIGIT(*p)) {
	    p++;
	}
frac:
	if (p < e && *p == '.') {
	    p++;
	    ENSURE_READABLE(1);
	    if (!ISDIGIT(*p)) RAISE(ERR_INVALID);
	    p++;
	    while (p < e && ISDIGIT(*p)) p++;
	}
	if (p < e && *p == 'e') {
	    p++;
	    if (p < e && (*p == '+' || *p == '-')) p++;
	    ENSURE_READABLE(1);
	    if (!ISDIGIT(*p)) RAISE(ERR_INVALID);
	    while (p < e && ISDIGIT(*p)) p++;
	}
	ENSURE_READABLE(1);
	break;
      case 't':
	ENSURE_READABLE(3);
	if (memcmp(p, "rue", 3)) {
	    RAISE(ERR_INVALID);
	}
	p += 3;
	break;
      case 'f':
	ENSURE_READABLE(4);
	if (memcmp(p, "alse", 4)) {
	    RAISE(ERR_INVALID);
	}
	p += 4;
	break;
      case 'n':
	ENSURE_READABLE(3);
	if (memcmp(p, "ull", 3)) {
	    RAISE(ERR_INVALID);
	}
	p += 3;
	break;
      default:
	RAISE(ERR_INVALID);
    }
    POP_STATE(parser);
    goto next_state;

object_first_name:
    SET_STATE(parser, STATE_OBJECT_FIRST_NAME);
    SKIP_WS();
    ENSURE_READABLE(1);
    switch (*p) {
      case '"':
	goto object_name;
      case '}':
	p++;
	POP_STATE(parser);
	goto next_state;
      default:
	RAISE(ERR_INVALID);
    }

object_name:
    SET_STATE(parser, STATE_OBJECT_NAME);
    {
	enum parse_error ret = parse_string(&p, e);
	if (ret) RAISE(ret);
    }

object_name_sep:
    SET_STATE(parser, STATE_OBJECT_NAME_SEP);
    SKIP_WS();
    ENSURE_READABLE(1);
    switch (*p++) {
      case ':':
	goto object_value;
      default:
	RAISE(ERR_INVALID);
    }

object_value:
    SET_STATE(parser, STATE_OBJECT_VALUE_SEP);
    PUSH_STATE(parser, STATE_VALUE);
    goto value;

object_value_sep:
    SET_STATE(parser, STATE_OBJECT_VALUE_SEP);
    SKIP_WS();
    ENSURE_READABLE(1);
    switch (*p++) {
      case ',':
	goto object_name;
      case '}':
	POP_STATE(parser);
	goto next_state;
      default:
	RAISE(ERR_INVALID);
    }

array_first_value:
    SET_STATE(parser, STATE_ARRAY_FIRST_VALUE);
    SKIP_WS();
    ENSURE_READABLE(1);
    if (*p == ']') {
	p++;
	POP_STATE(parser);
	goto next_state;
    }
    goto array_value;

array_value:
    SET_STATE(parser, STATE_ARRAY_VALUE_SEP);
    PUSH_STATE(parser, STATE_VALUE);
    goto value;

array_value_sep:
    SKIP_WS();
    ENSURE_READABLE(1);
    switch (*p++) {
      case ',':
	goto array_value;
      case ']':
	POP_STATE(parser);
	goto next_state;
      default:
	RAISE(ERR_INVALID);
    }

finish:
    SKIP_WS();
    *pp = p;
    if (p < e) {
	return ERR_EXTRABYTE;
    }
    return ERR_SUCCESS;

needmore:
    return ERR_NEEDMORE;
invalid:
    return ERR_INVALID;
}

static void
assert_invalid(enum parse_error err, const char *p, char c) {
    if (err != ERR_INVALID) {
	fprintf(stderr, "%d: ERR_INVALID is expected but %d\n", __LINE__, err);
	abort();
    }
    if (*p == ':') {
	fprintf(stderr, "%d: error pos is '%s'\n", __LINE__, p);
	abort();
    }
}

static void
assert_success(enum parse_error err) {
    if (err != ERR_SUCCESS) {
	fprintf(stderr, "%d: ERR_SUCCESS is expected but %d\n", __LINE__, err);
	abort();
    }
}

static void
assert_needmore(enum parse_error err) {
    if (err != ERR_NEEDMORE) {
	fprintf(stderr, "%d: ERR_NEEDMORE is expected but %d\n", __LINE__, err);
	abort();
    }
}

/*
int
main(void) {
    const char *p;
    const char *e;
    parser_t *parser = parser_new();
    enum parse_error err;

    p = " {  \"foo\" : {},\"bar\":-2.0e+1,\"baz\":[[], 1]}";
    e = p + strlen(p);
    parse_value(parser, &p, e);
    if (p != e) {
	fprintf(stderr, "%s\n", p);
	abort();
    }

    parser_init(parser);
    p = " {  \"foo\" : { },\"bar\":-2.0e+1,\"baz\":[[  ], 1]} ";
    e = p + strlen(p);
    parse_value(parser, &p, e);
    if (p != e) {
	fprintf(stderr, "%s\n", p);
	abort();
    }

    parser_init(parser);
    p = " { : ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_invalid(err, parser->p, ':');

    parser_init(parser);
    p = " {  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_needmore(err);
    p = " }  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_success(err);

    parser_init(parser);
    p = " [  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_needmore(err);
    p = " ]  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_success(err);

    parser_init(parser);
    p = " [ 1 ]  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_success(err);

    parser_init(parser);
    p = " [ true, false, null ]  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_success(err);

    parser_init(parser);
    p = " [ tru ]  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_invalid(err, parser->p, ' ');

    parser_init(parser);
    p = " [  1";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_needmore(err);
    p = "1 ]  ";
    e = p + strlen(p);
    err = parse_value(parser, &p, e);
    assert_success(err);

    return 0;
}
*/
