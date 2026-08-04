/*
 * funobj.h - FunTux object runtime.
 *
 * A small Objective-C-style runtime in C: classes with inheritance,
 * objects holding ivars, and message dispatch. No build-time magic:
 * classes are plain structs populated at runtime.
 */

#ifndef FUNOBJ_H
#define FUNOBJ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fun_class FunClass;
typedef struct fun_object FunObject;

/* A generic method argument/return value. */
typedef void *FunValue;

#define FUN_MAX_ARGS 8

typedef struct {
    unsigned count;
    FunValue args[FUN_MAX_ARGS];
} FunArgs;

/* Method implementation: receive the receiver plus a generic arg vector. */
typedef FunValue (*FunMethodFn)(FunObject *self, FunArgs args);

typedef struct {
    const char *name;   /* selector */
    FunMethodFn fn;
} FunMethod;

struct fun_class {
    const char *name;
    FunClass *super;            /* NULL for root classes */
    const FunMethod *methods;   /* static table, caller-owned */
    unsigned method_count;
    size_t ivar_size;           /* extra bytes allocated per instance */
};

struct fun_object {
    FunClass *isa;              /* class of this object */
    char ivars[];               /* instance data follows */
};

/* Build a class. The method table must outlive the class. */
FunClass *fun_class_create(const char *name, FunClass *super,
                           const FunMethod *methods, unsigned method_count,
                           size_t ivar_size);

/* Allocate a zeroed instance of a class (does not send "init"). */
FunObject *fun_alloc(FunClass *cls);

/* Allocate + send "init" (falls back to the "init" method, if any). */
FunObject *fun_new(FunClass *cls);

/* Pointer to the instance data block (of size cls->ivar_size). */
void *fun_ivars(FunObject *self);

/* Dispatch a message. Returns the method's return value, or NULL if the
 * selector is not understood (with a diagnostic on stderr). */
FunValue fun_send(FunObject *self, const char *sel, unsigned nargs, ...);

/* Convenience wrappers. */
FunValue fun_send0(FunObject *self, const char *sel);
FunValue fun_send1(FunObject *self, const char *sel, FunValue a);
FunValue fun_send2(FunObject *self, const char *sel, FunValue a, FunValue b);

/* Look up a method on a class, walking the superclass chain. */
const FunMethod *fun_lookup(FunClass *cls, const char *sel);

const char *fun_class_name(FunObject *self);

/* True if the object is of the named class or any subclass of it. */
int fun_is_kind_of(FunObject *self, const char *class_name);

#ifdef __cplusplus
}
#endif

#endif /* FUNOBJ_H */
