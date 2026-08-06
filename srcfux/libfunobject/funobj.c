#define _DEFAULT_SOURCE

#include "funobj.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FunClass *fun_class_create(const char *name, FunClass *super,
                           const FunMethod *methods, unsigned method_count,
                           size_t ivar_size) {
    FunClass *cls = calloc(1, sizeof(*cls));
    if (!cls) return NULL;
    cls->name = name;
    cls->super = super;
    cls->methods = methods;
    cls->method_count = method_count;
    cls->ivar_size = ivar_size;
    return cls;
}

FunObject *fun_alloc(FunClass *cls) {
    FunObject *obj = calloc(1, sizeof(*obj) + cls->ivar_size);
    if (!obj) return NULL;
    obj->isa = cls;
    return obj;
}

FunObject *fun_new(FunClass *cls) {
    FunObject *obj = fun_alloc(cls);
    if (!obj) return NULL;
    fun_send0(obj, "init");
    return obj;
}

void *fun_ivars(FunObject *self) {
    return self->ivars;
}

const FunMethod *fun_lookup(FunClass *cls, const char *sel) {
    for (FunClass *c = cls; c; c = c->super) {
        for (unsigned i = 0; i < c->method_count; i++) {
            if (strcmp(c->methods[i].name, sel) == 0) return &c->methods[i];
        }
    }
    return NULL;
}

static void does_not_understand(FunObject *self, const char *sel) {
    const char *cn = self->isa ? self->isa->name : "?";
    fprintf(stderr, "funobj: %s does not understand `%s`\n", cn, sel);
}

FunValue fun_send(FunObject *self, const char *sel, unsigned nargs, ...) {
    FunArgs a;
    a.count = nargs > FUN_MAX_ARGS ? FUN_MAX_ARGS : nargs;
    va_list ap;
    va_start(ap, nargs);
    for (unsigned i = 0; i < a.count; i++) a.args[i] = va_arg(ap, FunValue);
    va_end(ap);

    const FunMethod *m = fun_lookup(self->isa, sel);
    if (!m) {
        does_not_understand(self, sel);
        return NULL;
    }
    return m->fn(self, a);
}

FunValue fun_send0(FunObject *self, const char *sel) {
    return fun_send(self, sel, 0);
}

FunValue fun_send1(FunObject *self, const char *sel, FunValue a) {
    return fun_send(self, sel, 1, a);
}

FunValue fun_send2(FunObject *self, const char *sel, FunValue a, FunValue b) {
    return fun_send(self, sel, 2, a, b);
}

const char *fun_class_name(FunObject *self) {
    return self->isa ? self->isa->name : "?";
}

int fun_is_kind_of(FunObject *self, const char *class_name) {
    for (FunClass *c = self->isa; c; c = c->super) {
        if (strcmp(c->name, class_name) == 0) return 1;
    }
    return 0;
}
