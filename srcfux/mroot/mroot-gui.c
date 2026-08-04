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

static Widget app_shell;
static Widget list_w;
static Widget index_field;
static Widget media_field;
static Widget src_field;
static Widget dst_field;
static Widget output_text;

static char mroot_cmd[8192];

static const char *mroot_bin(void) {
    if (mroot_cmd[0]) return mroot_cmd;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(mroot_cmd, sizeof(mroot_cmd), "%s/mroot", exe);
        } else {
            snprintf(mroot_cmd, sizeof(mroot_cmd), "mroot");
        }
        if (access(mroot_cmd, X_OK) == 0) return mroot_cmd;
    }
    snprintf(mroot_cmd, sizeof(mroot_cmd), "mroot");
    return mroot_cmd;
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

static void exec_mroot(const char *fmt, ...) {
    char cmd[8192];
    char full[24576];
    char out[32768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    snprintf(full, sizeof(full), "%s %s 2>&1", mroot_bin(), cmd);
    int rc = run_capture(full, out, sizeof(out));
    append_output(full);
    if (out[0]) append_output(out);
    if (rc != 0) {
        char b[64];
        snprintf(b, sizeof(b), "exit status %d", rc);
        append_output(b);
    }
}

static int current_index(void) {
    return atoi(XmTextFieldGetString(index_field));
}

static void on_refresh(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char cmd[9216];
    char out[65536];
    snprintf(cmd, sizeof(cmd), "%s list 2>&1", mroot_bin());
    if (run_capture(cmd, out, sizeof(out)) < 0) return;
    XmString *items = NULL;
    int count = 0;
    char *save = NULL;
    char *line = strtok_r(out, "\n", &save);
    while (line) {
        if (strchr(line, '\t') && strncmp(line, "index", 5) != 0) {
            items = realloc(items, (count + 1) * sizeof(XmString));
            items[count++] = XmStringCreateLocalized(line);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    XmListDeleteAllItems(list_w);
    if (count > 0) XmListAddItems(list_w, items, count, 1);
    for (int i = 0; i < count; i++) XmStringFree(items[i]);
    free(items);
}

static void on_list_select(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data);
    XmListCallbackStruct *cs = (XmListCallbackStruct *)cbs;
    if (!cs->item) return;
    char *str = NULL;
    if (!XmStringGetLtoR(cs->item, XmFONTLIST_DEFAULT_TAG, &str)) return;
    char *tab = strchr(str, '\t');
    if (tab) *tab = 0;
    XmTextFieldSetString(index_field, str);
    XtFree(str);
}

static void on_init(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    char *media = XmTextFieldGetString(media_field);
    if (media && *media) exec_mroot("init --media %s %d", media, current_index());
    else exec_mroot("init %d", current_index());
    if (media) XtFree(media);
    on_refresh(NULL, NULL, NULL);
}

static void on_mount(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("mount %d", current_index());
}

static void on_umount(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("umount %d", current_index());
}

static void on_status(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("status %d", current_index());
}

static void on_info(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("info %d", current_index());
}

static void on_attach(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("attach %d", current_index());
}

static void on_detach(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("detach %d", current_index());
}

static void on_snapshot(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("snapshot %d", current_index());
}

static void on_media(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("list-media");
}

static void on_list(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    exec_mroot("list");
    on_refresh(NULL, NULL, NULL);
}

static void on_clone(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    int src = atoi(XmTextFieldGetString(src_field));
    int dst = atoi(XmTextFieldGetString(dst_field));
    char *media = XmTextFieldGetString(media_field);
    if (media && *media) exec_mroot("clone --media %s %d %d", media, src, dst);
    else exec_mroot("clone %d %d", src, dst);
    if (media) XtFree(media);
    on_refresh(NULL, NULL, NULL);
}

static void on_remove_ok(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(cbs);
    int idx = (int)(long)data;
    exec_mroot("remove -f %d", idx);
    XtDestroyWidget(XtParent(w));
    on_refresh(NULL, NULL, NULL);
}

static void on_remove(Widget w, XtPointer data, XtPointer cbs) {
    UNUSED(w); UNUSED(data); UNUSED(cbs);
    int idx = current_index();
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete root %d and all its files?", idx);
    XmString mstr = XmStringCreateLocalized(msg);
    Widget dialog = XmCreateMessageDialog(app_shell, "confirm", NULL, 0);
    XtVaSetValues(dialog, XmNmessageString, mstr, NULL);
    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_APPLY_BUTTON));
    Widget ok = XmMessageBoxGetChild(dialog, XmDIALOG_OK_BUTTON);
    XtAddCallback(ok, XmNactivateCallback, on_remove_ok, (XtPointer)(long)idx);
    XtManageChild(dialog);
    XmStringFree(mstr);
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
             "setsid %s -title \"mroot enter %d\" -e %s enter %d >/dev/null 2>&1 &",
             term, idx, mroot_bin(), idx);
    system(cmd);
    char b[128];
    snprintf(b, sizeof(b), "launched %s for root %d", term, idx);
    append_output(b);
}

int main(int argc, char **argv) {
    XtAppContext app;
    Widget shell = XtVaAppInitialize(&app, "mroot-gui", NULL, 0, &argc, argv, NULL,
                                     XmNtitle, "mroot - root manager",
                                     XmNwidth, 680, XmNheight, 560, NULL);
    app_shell = shell;

    Widget mainw = XmCreateMainWindow(shell, "main", NULL, 0);
    XtManageChild(mainw);

    Widget form = XmCreateForm(mainw, "form", NULL, 0);

    Widget ctl = XmCreateRowColumn(form, "controls", NULL, 0);
    XtVaSetValues(ctl, XmNpacking, XmPACK_TIGHT, XmNorientation, XmHORIZONTAL, NULL);
    Widget lbl = XmCreateLabel(ctl, "Index:", NULL, 0);
    XtManageChild(lbl);
    index_field = XmCreateTextField(ctl, "index", NULL, 0);
    XtVaSetValues(index_field, XmNcolumns, 4, XmNvalue, "1", NULL);
    XtManageChild(index_field);
    Widget lbl2 = XmCreateLabel(ctl, "Media device:", NULL, 0);
    XtManageChild(lbl2);
    media_field = XmCreateTextField(ctl, "media", NULL, 0);
    XtVaSetValues(media_field, XmNcolumns, 14, NULL);
    XtManageChild(media_field);
    Widget sep = XmCreateSeparator(ctl, "sep", NULL, 0);
    XtManageChild(sep);
    Widget refresh = XmCreatePushButton(ctl, "Refresh", NULL, 0);
    XtAddCallback(refresh, XmNactivateCallback, on_refresh, NULL);
    XtManageChild(refresh);
    XtManageChild(ctl);

    Widget sl = XmCreateList(form, "roots", NULL, 0);
    XtVaSetValues(sl, XmNvisibleItemCount, 6, NULL);
    list_w = sl;
    XtAddCallback(sl, XmNbrowseSelectionCallback, on_list_select, NULL);
    XtManageChild(sl);

    Widget btns = XmCreateRowColumn(form, "buttons", NULL, 0);
    XtVaSetValues(btns, XmNpacking, XmPACK_TIGHT, XmNorientation, XmHORIZONTAL, NULL);
    struct { const char *label; XtCallbackProc cb; } buttons[] = {
        { "Init",   on_init },
        { "Mount",  on_mount },
        { "Umount", on_umount },
        { "Enter",  on_enter },
        { "Status", on_status },
        { "Info",   on_info },
        { "Attach", on_attach },
        { "Detach", on_detach },
        { "Snapshot", on_snapshot },
        { "Remove", on_remove },
        { "List",   on_list },
        { "List-media", on_media },
    };
    size_t nb = sizeof(buttons) / sizeof(buttons[0]);
    for (size_t i = 0; i < nb; i++) {
        Widget bb = XmCreatePushButton(btns, (char *)buttons[i].label, NULL, 0);
        XtAddCallback(bb, XmNactivateCallback, buttons[i].cb, NULL);
        XtManageChild(bb);
    }
    XtManageChild(btns);

    Widget clone = XmCreateRowColumn(form, "clone", NULL, 0);
    XtVaSetValues(clone, XmNpacking, XmPACK_TIGHT, XmNorientation, XmHORIZONTAL, NULL);
    Widget cl = XmCreateLabel(clone, "Clone:", NULL, 0);
    XtManageChild(cl);
    src_field = XmCreateTextField(clone, "src", NULL, 0);
    XtVaSetValues(src_field, XmNcolumns, 4, NULL);
    XtManageChild(src_field);
    Widget cl2 = XmCreateLabel(clone, "to", NULL, 0);
    XtManageChild(cl2);
    dst_field = XmCreateTextField(clone, "dst", NULL, 0);
    XtVaSetValues(dst_field, XmNcolumns, 4, NULL);
    XtManageChild(dst_field);
    Widget cbtn = XmCreatePushButton(clone, "Clone", NULL, 0);
    XtAddCallback(cbtn, XmNactivateCallback, on_clone, NULL);
    XtManageChild(cbtn);
    XtManageChild(clone);

    output_text = XmCreateText(form, "output", NULL, 0);
    XtVaSetValues(output_text,
                  XmNeditable, False,
                  XmNeditMode, XmMULTI_LINE_EDIT,
                  XmNrows, 14,
                  XmNcolumns, 100,
                  XmNscrollVertical, True,
                  XmNscrollHorizontal, True,
                  NULL);
    XtManageChild(output_text);

    XtVaSetValues(ctl,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(sl,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, ctl,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(btns,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, sl,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(clone,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, btns,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(output_text,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, clone,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, NULL);
    XtManageChild(form);

    XmMainWindowSetAreas(mainw, NULL, NULL, NULL, NULL, form);

    on_refresh(NULL, NULL, NULL);
    append_output("Welcome to mroot manager");

    XtRealizeWidget(shell);
    XtAppMainLoop(app);
    return 0;
}
