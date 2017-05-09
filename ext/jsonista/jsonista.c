#include "jsonista.h"

VALUE rb_mJsonista;

void
Init_jsonista(void)
{
  rb_mJsonista = rb_define_module("Jsonista");
}
