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

typedef struct stack_st {
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
typedef struct parser_buffer_st {
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

static void
buffer_clear(buffer_t *buf) {
    buf->p = buf->buf;
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

static void
buffer_write_char(buffer_t *buf, int c) {
    if (c <= 0x7F) {
	buffer_ensure_writable(buf, 1);
	buf->p[0] = c;
	buf->p++;
    } else if (c <= 0x7FF) {
	buffer_ensure_writable(buf, 2);
	buf->p[0] = 0xC0 | ((c >>  6) & 0x1F);
	buf->p[1] = 0x80 | ((c      ) & 0x3F);
	buf->p += 2;
    } else if (c <= 0xFFFF) {
	buffer_ensure_writable(buf, 3);
	buf->p[0] = 0xE0 | ((c >> 12) & 0x0F);
	buf->p[1] = 0x80 | ((c >>  6) & 0x3F);
	buf->p[2] = 0x80 | ( c        & 0x3F);
	buf->p += 3;
    } else if (c <= 0x10FFFF) {
	buffer_ensure_writable(buf, 4);
	buf->p[0] = 0xF0 | ((c >> 18) & 0x0F);
	buf->p[1] = 0x80 | ((c >> 12) & 0x3F);
	buf->p[2] = 0x80 | ((c >>  6) & 0x3F);
	buf->p[3] = 0x80 | ( c        & 0x3F);
	buf->p += 4;
    }
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
    if (parser->buffer) {
	buffer_clear(parser->buffer);
    } else {
	parser->buffer = buffer_new();
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
parser_buffer_write_char(parser_t *parser, int c) {
    buffer_write_char(parser->buffer, c);
}

static void
parser_buffer_write(parser_t *parser, const char *p, size_t len) {
    buffer_write(parser->buffer, p, len);
}

static void
parser_tmp_replace(parser_t *parser, const char *p, const char *e) {
    size_t len = e - p;
    memcpy(parser->tmp, p, len);
    parser->tmp[len] = 0;
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

static int
js_isdigit(char c) {
    return '0' <= c && c <= '9';
}

static int
istrail(char c) {
    unsigned char u = (unsigned char)c;
    return 0x80 <= u && u <= 0xBF;
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

static int
to_i(char c) {
    const int x = 0x10000;
    const int tbl[256] = {
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  x,  x,  x,  x,  x,  x,
	 x, 10, 11, 12, 13, 14, 15,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x, 10, 11, 12, 13, 14, 15,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
	 x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,  x,
    };
    return tbl[c];
}

/* convert 4 digits to integer */
static int
digits2i(const char *p) {
    int c = (to_i(p[0]) << 12) | (to_i(p[1]) << 8) | (to_i(p[2]) << 4) | to_i(p[3]);
    if (c > 0xFFFF) {
	if (c & 0x10000) return -1;
	if (c & 0x100000) return -2;
	if (c & 0x1000000) return -3;
	if (c & 0x10000000) return -4;
    }
    return c;
}

static enum parse_error
parse_escape(parser_t *parser, const char **pp, const char *e) {
    const char *p = *pp;
    int c;
    //fprintf(stderr, "%d: ESCAPE: \"%s\"\n",__LINE__,p);
    switch (*p++) {
      case '"':  parser_buffer_write_char(parser, '"');  break;
      case '\\': parser_buffer_write_char(parser, '\\'); break;
      case '/':  parser_buffer_write_char(parser, '/');  break;
      case 'b':  parser_buffer_write_char(parser, '\b'); break;
      case 'f':  parser_buffer_write_char(parser, '\f'); break;
      case 'n':  parser_buffer_write_char(parser, '\n'); break;
      case 'r':  parser_buffer_write_char(parser, '\r'); break;
      case 't':  parser_buffer_write_char(parser, '\t'); break;
      case 'u':
	ENSURE_READABLE(4);
	c = digits2i(p);
	if (c < 0) {
	    p -= c;
	    goto invalid;
	} else if (0xD800 <= c && c <= 0xDBFF) {
	    int d;
	    ENSURE_READABLE(6);
	    if (p[4] != '\\') { p += 4; goto invalid; }
	    if (p[5] != 'u') { p += 5; goto invalid; }
	    d = digits2i(p+6);
	    if (d < 0) {
		p += 6 - d;
		goto invalid;
	    }
	    c &= 0x3FF;
	    c <<= 10;
	    c += 0x10000;
	    c |= d &0x3FF;
	    parser_buffer_write_char(parser, c);
	    p += 10;
    fprintf(stderr, "%d: ESCAPE: \"%s\" %x\n",__LINE__,p, c);
	} else if (0xDC00 <= c && c <= 0xDFFF) {
	    goto invalid;
	} else {
	    parser_buffer_write_char(parser, c);
	    p += 4;
	}
	break;
      default:
	p--;
	goto invalid;
    }
    *pp = p;
    return ERR_SUCCESS;
needmore:
    parser_tmp_replace(parser, *pp, e);
    return ERR_NEEDMORE;
invalid:
    *pp = p;
    return ERR_INVALID;
}

static enum parse_error
parse_string0(parser_t *parser, const char **pp, const char *e) {
    const char *p = *pp;
    while (p < e) {
	if (*p == '"') {
	    goto success;
	} else if (*p == '\\') {
	    int err;
	    if (++p >= e) goto needmore;
	    err = parse_escape(parser, &p, e);
	    switch (err) {
	      case ERR_INVALID:
		goto invalid;
	      case ERR_NEEDMORE:
		goto invalid;
	      case ERR_SUCCESS:
		break;
	    }
	} else if (*p < 0x20) {
	    goto invalid;
	} else if (*p <= 0x7F) {
	    parser_buffer_write_char(parser, *p);
	    p++;
	} else if ((uint8_t)*p < 0xC2) {
	    goto invalid;
	} else if ((uint8_t)*p < 0xE0) {
	    ENSURE_READABLE(2);
	    if (!istrail(p[1])) { p += 1; goto invalid; }
	    parser_buffer_write(parser, p, 2);
	} else if ((uint8_t)*p == 0xE0) {
	    ENSURE_READABLE(3);
	    if (0xA0 <= (uint8_t)p[1] && (uint8_t)p[1] <= 0xBF) {
		p += 1;
		goto invalid;
	    }
	    if (!istrail(p[2])) { p += 2; goto invalid; }
	    parser_buffer_write(parser, p, 3);
	} else if ((uint8_t)*p < 0xF0) {
	    ENSURE_READABLE(3);
	    if (!istrail(p[1])) { p += 1; goto invalid; }
	    if (!istrail(p[2])) { p += 2; goto invalid; }
	    parser_buffer_write(parser, p, 3);
	} else if ((uint8_t)*p == 0xF0) {
	    ENSURE_READABLE(4);
	    if (0x90 <= (uint8_t)p[1] && (uint8_t)p[1] <= 0xBF) {
		p += 1;
		goto invalid;
	    }
	    if (!istrail(p[2])) { p += 2; goto invalid; }
	    if (!istrail(p[3])) { p += 3; goto invalid; }
	    parser_buffer_write(parser, p, 4);
	} else if ((uint8_t)*p < 0xF4) {
	    ENSURE_READABLE(4);
	    if (!istrail(p[1])) { p += 1; goto invalid; }
	    if (!istrail(p[2])) { p += 2; goto invalid; }
	    if (!istrail(p[3])) { p += 3; goto invalid; }
	    parser_buffer_write(parser, p, 4);
	} else if ((uint8_t)*p == 0xF4) {
	    ENSURE_READABLE(4);
	    if (0x80 <= (uint8_t)p[1] && (uint8_t)p[1] <= 0x8F) {
		p += 1;
		goto invalid;
	    }
	    if (!istrail(p[2])) { p += 2; goto invalid; }
	    if (!istrail(p[3])) { p += 3; goto invalid; }
	    parser_buffer_write(parser, p, 4);
	} else {
	    goto invalid;
	}
    }
needmore:
    fprintf(stderr, "%d: STRING:NEEDMORE \"%s\"\n",__LINE__,p);
    return ERR_NEEDMORE;
invalid:
    *pp = p;
    fprintf(stderr, "%d: STRING:INVALID \"%s\"\n",__LINE__,p);
    return ERR_INVALID;
success:
    parser_buffer_write_char(parser, 0);
    fprintf(stderr, "%d: STRING: \"%s\"\n",__LINE__,parser->buffer->buf);
    *pp = p;
    return ERR_SUCCESS;
}

static enum parse_error
parse_string(parser_t *parser, const char **pp, const char *e) {
    const char *p = *pp;
    skip_ws(&p, e);
    if (p < e) {
	if (*p == '"') {
	    *pp = ++p;
	    return parse_string0(parser, pp, e);
	}
	*pp = p;
	return ERR_INVALID;
    }
    *pp = p;
    return ERR_NEEDMORE;
}

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
	if (parse_string0(parser, &p, e)) {
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
	enum parse_error ret = parse_string(parser, &p, e);
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
