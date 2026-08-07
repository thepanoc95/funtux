#ifndef FUNOBJ_H
#define FUNOBJ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fun_class FunClass;
typedef struct fun_object FunObject;

typedef void *FunValue;

#define FUN_MAX_ARGS 8

typedef struct {
    unsigned count;
    FunValue args[FUN_MAX_ARGS];
} FunArgs;

typedef FunValue (*FunMethodFn)(FunObject *self, FunArgs args);

typedef struct {
    const char *name;
    FunMethodFn fn;
} FunMethod;

struct fun_class {
    const char *name;
    FunClass *super;
    const FunMethod *methods;
    unsigned method_count;
    size_t ivar_size;
};

struct fun_object {
    FunClass *isa;
    char ivars[];
};

FunClass *fun_class_create(const char *name, FunClass *super,
                           const FunMethod *methods, unsigned method_count,
                           size_t ivar_size);

FunObject *fun_alloc(FunClass *cls);
FunObject *fun_new(FunClass *cls);
void *fun_ivars(FunObject *self);
FunValue fun_send(FunObject *self, const char *sel, unsigned nargs, ...);
FunValue fun_send0(FunObject *self, const char *sel);
FunValue fun_send1(FunObject *self, const char *sel, FunValue a);
FunValue fun_send2(FunObject *self, const char *sel, FunValue a, FunValue b);
const FunMethod *fun_lookup(FunClass *cls, const char *sel);
const char *fun_class_name(FunObject *self);
int fun_is_kind_of(FunObject *self, const char *class_name);

#ifdef __cplusplus
}
#endif

#endif /* FUNOBJ_H */
