/*
 * SystemBuilder.c - FunTux System Builder (Motif).
 * Lists moka recipes and builds them into an mroot root.
 */

#define _DEFAULT_SOURCE

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <X11/StringDefs.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNUSED(x) ((void)(x))
#define BASE_PKGS_FILE "/etc/funtux/base-packages"
#define MAX_ATOMS 4096

static Widget app_shell;
static Widget pkg_list;
static Widget index_field;
static Widget output_text;
static char moka_cmd[4096];

static const char *roots_dir(void) {
    const char *env = getenv("MROOT_ROOTS_DIR");
    return env && *env ? env : "/var/roots";
}

static const char *moka_bin(void) {
    if (moka_cmd[0]) return moka_cmd;
    const char *env = getenv("FUNTUX_MOKA");
    if (env && *env) {
        snprintf(moka_cmd, sizeof(moka_cmd), "%s", env);
        return moka_cmd;
    }
    snprintf(moka_cmd, sizeof(moka_cmd), "moka");
    return moka_cmd;
}

static char *sq(const char *s) {
    size_t len = strlen(s) * 2 + 3;
    char *out = malloc(len);
    if (!out) return NULL;
    char *d = out;
    *d++ = '\'';
    for (const char *c = s; *c; c++) {
        if (*c == '\'') {
            *d++ = '\'';
            *d++ = '\\';
            *d++ = '\'';
            *d++ = '\'';
        } else {
            *d++ = *c;
        }
    }
    *d++ = '\'';
    *d = '\0';
    return out;
}

static void append_output(const char *s) {
    XmTextInsert(output_text, XmTextGetLastPosition(output_text), (char *)s);
    XmTextInsert(output_text, XmTextGetLastPosition(output_text), "\n");
}

static int run_capture(const char *cmd, char *out, size_t outsz) {
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    size_t used = 0;
    out[0] = 0;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (used + n + 1 < outsz) {
            memcpy(out + used, buf, n);
            used += n;
            out[used] = 0;
        }
    }
    int rc = pclose(f);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static int run_and_show(const char *cmd) {
    char out[65536];
    append_output(cmd);
    int rc = run_capture(cmd, out, sizeof(out));
    if (out[0]) append_output(out);
    if (rc != 0) {
        char b[64];
        snprintf(b, sizeof(b), "exit status %d", rc);
        append_output(b);
    }
    return rc;
}

static int current_index(void) {
    return atoi(XmTextFieldGetString(index_field));
}

static void on_refresh(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char cmd[9216];
    char out[262144];
    snprintf(cmd, sizeof(cmd), "%s search '' 2>&1", moka_bin());
    if (run_capture(cmd, out, sizeof(out)) < 0) return;
    XmString *items = NULL;
    int count = 0;
    char *save = NULL;
    char *line = strtok_r(out, "\n", &save);
    while (line) {
        items = realloc(items, (size_t)(count + 1) * sizeof(XmString));
        items[count++] = XmStringCreateLocalized(line);
        line = strtok_r(NULL, "\n", &save);
    }
    XmListDeleteAllItems(pkg_list);
    if (count > 0) XmListAddItems(pkg_list, items, count, 1);
    for (int i = 0; i < count; i++) XmStringFree(items[i]);
    free(items);
    append_output("package list refreshed");
}

static void build_atoms_into_root(int idx, char *const *atoms, int n) {
    char root[4096];
    snprintf(root, sizeof(root), "%s/%d", roots_dir(), idx);
    for (int i = 0; i < n; i++) {
        char *qroot = sq(root);
        char *qatom = sq(atoms[i]);
        char cmd[16384];
        snprintf(cmd, sizeof(cmd), "FUNTUX_ROOT=%s %s install %s 2>&1",
                 qroot, moka_bin(), qatom);
        free(qroot);
        free(qatom);
        run_and_show(cmd);
    }
}

static void on_build_selected(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    int *pos = NULL;
    int pos_count = 0;
    XmListGetSelectedPos(pkg_list, &pos, &pos_count);
    if (pos_count <= 0) {
        append_output("no packages selected");
        return;
    }
    XmStringTable items = NULL;
    int item_count = 0;
    XtVaGetValues(pkg_list, XmNitems, &items, XmNitemCount, &item_count, NULL);
    char *atoms[MAX_ATOMS] = {0};
    int count = 0;
    for (int i = 0; i < pos_count && count < MAX_ATOMS; i++) {
        if (pos[i] < 1 || pos[i] > item_count) continue;
        char *str = NULL;
        if (!XmStringGetLtoR(items[pos[i] - 1], XmFONTLIST_DEFAULT_TAG, &str) || !str) {
            continue;
        }
        char *tab = strchr(str, ' ');
        if (tab) *tab = '\0';
        atoms[count++] = str;
    }
    char b[128];
    snprintf(b, sizeof(b), "building %d package(s) into root %d", count, current_index());
    append_output(b);
    build_atoms_into_root(current_index(), atoms, count);
    for (int i = 0; i < count; i++) XtFree(atoms[i]);
    XtFree((char *)pos);
}

static void on_bootstrap(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    FILE *f = fopen(BASE_PKGS_FILE, "r");
    if (!f) {
        char b[256];
        snprintf(b, sizeof(b), "%s not found; add one atom per line", BASE_PKGS_FILE);
        append_output(b);
        return;
    }
    char *atoms[MAX_ATOMS];
    int count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f) && count < MAX_ATOMS) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (!*p || *p == '#') continue;
        atoms[count++] = strdup(p);
    }
    fclose(f);
    char b[128];
    snprintf(b, sizeof(b), "bootstrapping %d package(s) into root %d", count, current_index());
    append_output(b);
    build_atoms_into_root(current_index(), atoms, count);
    for (int i = 0; i < count; i++) free(atoms[i]);
}

static void on_mount(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mroot mount %d 2>&1", current_index());
    run_and_show(cmd);
}

static void on_umount(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mroot umount %d 2>&1", current_index());
    run_and_show(cmd);
}

static void on_enter(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    int idx = current_index();
    static const char *terms[] = {
        "xterm", "urxvt", "rxvt", "konsole", "gnome-terminal",
        "xfce4-terminal", "kitty", "alacritty", "foot", NULL
    };
    char term[64] = "xterm";
    for (int i = 0; terms[i]; i++) {
        char p[256];
        snprintf(p, sizeof(p), "/usr/bin/%s", terms[i]);
        if (access(p, X_OK) == 0) {
            snprintf(term, sizeof(term), "%s", terms[i]);
            break;
        }
    }
    char cmd[9216];
    snprintf(cmd, sizeof(cmd),
             "setsid %s -title \"mroot enter %d\" -e mroot enter %d >/dev/null 2>&1 &",
             term, idx, idx);
    system(cmd);
    char b[128];
    snprintf(b, sizeof(b), "launched %s for root %d", term, idx);
    append_output(b);
}

static void on_status(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mroot status %d 2>&1", current_index());
    run_and_show(cmd);
}

int main(int argc, char **argv) {
    XtAppContext app;
    Widget shell = XtVaAppInitialize(&app, "SystemBuilder", NULL, 0, &argc, argv, NULL,
                                     XmNtitle, "FunTux System Builder",
                                     XmNwidth, 760, XmNheight, 620, NULL);
    app_shell = shell;

    Widget mainw = XmCreateMainWindow(shell, "main", NULL, 0);
    XtManageChild(mainw);

    Widget form = XmCreateForm(mainw, "form", NULL, 0);

    Widget ctl = XmCreateRowColumn(form, "controls", NULL, 0);
    XtVaSetValues(ctl, XmNpacking, XmPACK_TIGHT, XmNorientation, XmHORIZONTAL, NULL);
    Widget lbl = XmCreateLabel(ctl, "Root index:", NULL, 0);
    XtManageChild(lbl);
    index_field = XmCreateTextField(ctl, "index", NULL, 0);
    XtVaSetValues(index_field, XmNcolumns, 4, XmNvalue, "1", NULL);
    XtManageChild(index_field);
    Widget refresh = XmCreatePushButton(ctl, "Refresh packages", NULL, 0);
    XtAddCallback(refresh, XmNactivateCallback, on_refresh, NULL);
    XtManageChild(refresh);
    XtManageChild(ctl);

    Widget blabel = XmCreateLabel(form, "Available recipes (multi-select):", NULL, 0);
    XtManageChild(blabel);

    Widget sl = XmCreateList(form, "packages", NULL, 0);
    XtVaSetValues(sl,
                  XmNselectionPolicy, XmEXTENDED_SELECT,
                  XmNvisibleItemCount, 8, NULL);
    pkg_list = sl;
    XtManageChild(sl);

    Widget btns = XmCreateRowColumn(form, "buttons", NULL, 0);
    XtVaSetValues(btns, XmNpacking, XmPACK_TIGHT, XmNorientation, XmHORIZONTAL, NULL);
    struct { const char *label; XtCallbackProc cb; } buttons[] = {
        { "Build selected into root", on_build_selected },
        { "Bootstrap base",           on_bootstrap },
        { "Mount root",               on_mount },
        { "Umount root",              on_umount },
        { "Enter root",               on_enter },
        { "Status",                   on_status },
    };
    size_t nb = sizeof(buttons) / sizeof(buttons[0]);
    for (size_t i = 0; i < nb; i++) {
        Widget bb = XmCreatePushButton(btns, (char *)buttons[i].label, NULL, 0);
        XtAddCallback(bb, XmNactivateCallback, buttons[i].cb, NULL);
        XtManageChild(bb);
    }
    XtManageChild(btns);

    output_text = XmCreateText(form, "output", NULL, 0);
    XtVaSetValues(output_text,
                  XmNeditable, False,
                  XmNeditMode, XmMULTI_LINE_EDIT,
                  XmNrows, 12,
                  XmNcolumns, 110,
                  XmNscrollVertical, True,
                  XmNscrollHorizontal, True,
                  NULL);
    XtManageChild(output_text);

    XtVaSetValues(ctl,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(blabel,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, ctl,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(sl,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, blabel,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(btns,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, sl,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(output_text,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, btns,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, NULL);
    XtManageChild(form);

    XmMainWindowSetAreas(mainw, NULL, NULL, NULL, NULL, form);

    on_refresh(NULL, NULL, NULL);
    append_output("Welcome to the FunTux System Builder");
    append_output("(moka: recipes / mroot: roots)");

    XtRealizeWidget(shell);
    XtAppMainLoop(app);
    return 0;
}
