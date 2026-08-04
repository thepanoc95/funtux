/*
 * funobj_test.c - smoke test for the FunTux object runtime.
 */

#include "funobj.h"

#include <stdio.h>
#include <stdlib.h>

struct shape_ivars {
    double area;
};

static FunValue shape_init(FunObject *self, FunArgs args) {
    (void)args;
    struct shape_ivars *iv = fun_ivars(self);
    iv->area = 0.0;
    return (FunValue)iv;
}

static FunValue shape_area(FunObject *self, FunArgs args) {
    (void)args;
    struct shape_ivars *iv = fun_ivars(self);
    return (FunValue)(&iv->area);
}

static FunValue shape_describe(FunObject *self, FunArgs args) {
    (void)args;
    struct shape_ivars *iv = fun_ivars(self);
    printf("%s: area = %g\n", fun_class_name(self), iv->area);
    return NULL;
}

static const FunMethod shape_methods[] = {
    { "init", shape_init },
    { "area", shape_area },
    { "describe", shape_describe },
};

struct circle_ivars {
    struct shape_ivars shape;
    double radius;
};

static FunValue circle_init(FunObject *self, FunArgs args) {
    (void)args;
    shape_init(self, args);
    struct circle_ivars *iv = fun_ivars(self);
    iv->radius = 1.0;
    return NULL;
}

static FunValue circle_area(FunObject *self, FunArgs args) {
    (void)args;
    struct circle_ivars *iv = fun_ivars(self);
    iv->shape.area = 3.14159 * iv->radius * iv->radius;
    return (FunValue)(&iv->shape.area);
}

static const FunMethod circle_methods[] = {
    { "init", circle_init },
    { "area", circle_area },
};

int main(void) {
    FunClass *shape = fun_class_create(
        "Shape", NULL, shape_methods,
        sizeof(shape_methods) / sizeof(shape_methods[0]),
        sizeof(struct shape_ivars));
    FunClass *circle = fun_class_create(
        "Circle", shape, circle_methods,
        sizeof(circle_methods) / sizeof(circle_methods[0]),
        sizeof(struct circle_ivars));

    FunObject *s = fun_new(shape);
    FunObject *c = fun_new(circle);

    printf("s class: %s\n", fun_class_name(s));
    printf("c class: %s\n", fun_class_name(c));
    printf("c is kind of Shape: %d\n", fun_is_kind_of(c, "Shape"));
    printf("s is kind of Circle: %d\n", fun_is_kind_of(s, "Circle"));

    double *a1 = fun_send0(s, "area");
    printf("shape area = %g\n", *a1);

    double *a2 = fun_send0(c, "area");
    printf("circle area = %g\n", *a2);

    fun_send0(c, "describe"); /* inherited from Shape */

    fun_send0(c, "nosuch");   /* should print a warning */

    free(s);
    free(c);
    return 0;
}
