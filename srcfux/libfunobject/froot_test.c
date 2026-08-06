/*
 * froot_test.c - exercises the FunTux multiroot object model without
 * needing root or the funroot kernel module.
 */

#define _DEFAULT_SOURCE

#include "funroot.h"
#include "funobj.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    FunObject *m = fun_mount_new("proc", "/proc", "proc", 0, "");
    printf("mount class: %s\n", fun_class_name(m));
    printf("mount is kind of FunMount: %d\n", fun_is_kind_of(m, "FunMount"));
    fun_send0(m, "describe");
    printf("mount is-mounted on /: %d\n",
           (int)(intptr_t)fun_send1(m, "is-mounted", (FunValue)"/"));
    free(m);

    FunObject *r = fun_root_new(1);
    printf("root class: %s\n", fun_class_name(r));
    printf("root is kind of FunRoot: %d\n", fun_is_kind_of(r, "FunRoot"));
    fun_send0(r, "describe");
    fun_send0(r, "status");

    FunObject *host = fun_root_new(0);
    printf("host is kind of FunRoot: %d\n", fun_is_kind_of(host, "FunRoot"));
    fun_send0(host, "describe");
    free(host);

    FunObject *s = fun_stratum_new("gentoo");
    printf("stratum class: %s\n", fun_class_name(s));
    printf("stratum is kind of FunRoot: %d\n", fun_is_kind_of(s, "FunRoot"));
    printf("stratum name: %s\n", (char *)fun_send0(s, "name"));
    fun_send0(s, "describe");
    free(s);

    fun_send0(r, "nosuch"); /* should print a warning */
    free(r);
    return 0;
}
