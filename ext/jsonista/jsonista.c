#include "jsonista.h"
#include "parser.h"

static VALUE mJsonista, cParser, eParseError;
static ID id_src, id_pos;

#define GetJsonistaParserVal(obj, tobj) ((tobj) = get_jsonista_parser_val(obj))
#define GetNewJsonistaParserVal(obj, tobj) ((tobj) = get_new_jsonista_parser_val(obj))
#define JSONISTA_PARSER_INIT_P(tobj) ((tobj)->stack)

static void
jsonista_parser_free(void *parser) {
    parser_free((void *)parser);
}

static size_t
jsonista_parser_memsize(const void *parser) {
    return parser_memsize((void *)parser);
}

static const rb_data_type_t jsonista_parser_data_type = {
    "jsonista_parser",
    {
	NULL, jsonista_parser_free, jsonista_parser_memsize,
    },
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY|RUBY_TYPED_WB_PROTECTED
#endif
};

static VALUE
jsonista_parser_s_alloc(VALUE klass)
{
    VALUE obj;
    parser_t *tobj;
    obj = TypedData_Make_Struct(klass, parser_t,
				&jsonista_parser_data_type, tobj);
    parser_init(tobj);
    return obj;
}

static parser_t *
get_jsonista_parser_val(VALUE obj)
{
    parser_t *tobj;
    TypedData_Get_Struct(obj, parser_t, &jsonista_parser_data_type,
			 tobj);
    if (!JSONISTA_PARSER_INIT_P(tobj)) {
	rb_raise(rb_eTypeError, "uninitialized %" PRIsVALUE, rb_obj_class(obj));
    }
    return tobj;
}

static parser_t *
get_new_jsonista_parser_val(VALUE obj)
{
    parser_t *tobj;
    TypedData_Get_Struct(obj, parser_t, &jsonista_parser_data_type,
			 tobj);
    if (JSONISTA_PARSER_INIT_P(tobj)) {
	rb_raise(rb_eTypeError, "already initialized %" PRIsVALUE,
		 rb_obj_class(obj));
    }
    return tobj;
}

/*
 * @overload new()
 *
 * returns parser object
 */
static VALUE
jsonista_parser_initialize(VALUE self)
{
    parser_t *tobj;
    TypedData_Get_Struct(self, parser_t, &jsonista_parser_data_type, tobj);
    return self;
}

static void parse_error_src_pos(VALUE src, ptrdiff_t pos);

/*
 * @overload parser_chunk(str)
 *   @param str [String] full or partial JSON string
 *
 * returns nil
 */
static VALUE
jsonista_parser_parse_chunk(VALUE self, VALUE str)
{
    parser_t *tobj;
    const char *s, *p, *e;
    enum parse_error err;

    TypedData_Get_Struct(self, parser_t, &jsonista_parser_data_type, tobj);
    StringValue(str);
    s = p = RSTRING_PTR(str);
    e = RSTRING_END(str);
    err = parser_parse_chunk(tobj, &p, e);
    switch (err) {
      case ERR_INVALID:
	parse_error_src_pos(str, p - s);
	break;
      case ERR_NEEDMORE:
      case ERR_SUCCESS:
      case ERR_EXTRABYTE:
	break;
    }
    return Qnil;
}

static void
parse_error_src_pos(VALUE src, ptrdiff_t pos)
{
    VALUE exc, argv[3];
    const char *s = RSTRING_PTR(src);
    const char *p = s + pos;
    argv[0] = rb_sprintf("unexpected byte '%c' at %zd", *p, p - s);
    argv[1] = src;
    argv[2] = pos;
    exc = rb_class_new_instance(3, argv, eParseError);
    rb_exc_raise(exc);
}

/*
 * call-seq:
 *   Jsonista::ParseError.new(msg, src, pos)  -> parse_error
 *
 * Construct a new Jsonista::ParseError exception.
 */

static VALUE
parse_err_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE pos = (argc > 2) ? argv[--argc] : Qnil;
    VALUE src = (argc > 1) ? argv[--argc] : Qnil;
    rb_call_super(argc, argv);
    rb_ivar_set(self, id_pos, pos);
    rb_ivar_set(self, id_src, src);
    return self;
}

/*
 * call-seq:
 *   parse_error.src  -> string
 *
 * Returns source string.
 */

static VALUE
parse_err_src(VALUE self)
{
    return rb_attr_get(self, id_src);
}

/*
 * call-seq:
 *   parse_error.pos  -> integer
 *
 * Returns source byte position of the unexpected byte.
 */

static VALUE
parse_err_pos(VALUE self)
{
    return rb_attr_get(self, id_pos);
}

void
Init_jsonista(void)
{
    mJsonista = rb_define_module("Jsonista");
    cParser = rb_define_class_under(mJsonista, "Parser", rb_cObject);
    rb_define_alloc_func(cParser, jsonista_parser_s_alloc);
    rb_define_method(cParser, "initialize", jsonista_parser_initialize, 0);
    rb_define_method(cParser, "parse_chunk", jsonista_parser_parse_chunk, 1);

    eParseError = rb_define_class_under(mJsonista, "ParseError", rb_eStandardError);
    rb_define_method(eParseError, "initialize", parse_err_initialize, -1);
    rb_define_method(eParseError, "src", parse_err_src, 0);
    rb_define_method(eParseError, "pos", parse_err_pos, 0);
}
