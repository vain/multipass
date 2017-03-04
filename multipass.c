#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef DEBUG
#define D if (true)
#else
#define D if (false)
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

Atom atom_protocols, atom_delete, atom_net_wmname;
Display *dpy;
int screen;
Window root, win;
GC gc;
XftColor fg, bg;
XftFont *font;
int font_height, font_baseline, font_horiz_margin;
int (*xerrorxlib)(Display *, XErrorEvent *);

#define MAX_TARGETS 128
Window targets[MAX_TARGETS] = {0};

#include "config.h"

void
get_window_title(char *buf, size_t buf_size, Window w)
{
    XTextProperty tp;
    char **slist = NULL;
    int count;

    /* Taken from katriawm */

    buf[0] = 0;

    if (!XGetTextProperty(dpy, w, &tp, atom_net_wmname))
    {
        if (!XGetTextProperty(dpy, w, &tp, XA_WM_NAME))
        {
            strncpy(buf, "<?>", buf_size);
            return;
        }
    }

    if (tp.nitems == 0)
    {
        strncpy(buf, "<?>", buf_size);
        return;
    }

    if (tp.encoding == XA_STRING)
        strncpy(buf, (char *)tp.value, buf_size);
    else
    {
        if (XmbTextPropertyToTextList(dpy, &tp, &slist, &count) >= Success &&
            count > 0 && *slist)
        {
            strncpy(buf, slist[0], buf_size - 1);
            XFreeStringList(slist);
        }
    }

    buf[buf_size - 1] = 0;
    XFree(tp.value);
}

void
window_size(int w, int h)
{
    XSizeHints sh = {
        .flags = PMinSize | PMaxSize,
        .min_width = w,
        .max_width = w,
        .min_height = h,
        .max_height = h,
    };
    XResizeWindow(dpy, win, w, h);
    XSetWMNormalHints(dpy, win, &sh);
}

void
create_window(void)
{
    XSetWindowAttributes wa = {
        .background_pixmap = ParentRelative,
        .event_mask = KeyPressMask | KeyReleaseMask | ExposureMask,
    };
    XClassHint ch = {
        .res_class = "Multipass",
        .res_name = "multipass",
    };

    win = XCreateWindow(dpy, root, 0, 0, win_width, 10, 0,
                        DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWBackPixmap | CWEventMask,
                        &wa);
    window_size(win_width, 10);
    XSetClassHint(dpy, win, &ch);
    XMapWindow(dpy, win);

    atom_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    atom_net_wmname = XInternAtom(dpy, "_NET_WM_NAME", False);
    XSetWMProtocols(dpy, win, &atom_delete, 1);

    gc = XCreateGC(dpy, win, 0, NULL);
}

bool
init_font_colors(void)
{
    font = XftFontOpenName(dpy, screen, font_str);
    if (!font)
    {
        fprintf(stderr, "multipass: Cannot open font '%s'\n", font_str);
        return false;
    }

    /* See bevelbar.c :-) */
    font_height = MAX(font->ascent + font->descent, font->height);
    font_baseline = font_height - font->descent;
    font_baseline += (int)(0.25 * font_height);
    font_height += (int)(0.5 * font_height);
    font_horiz_margin = 0.25 * font_height;

    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                           DefaultColormap(dpy, screen), bg_str, &bg))
    {
        fprintf(stderr, "multipass: Cannot alloc color '%s'\n", bg_str);
        return false;
    }

    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                           DefaultColormap(dpy, screen), fg_str, &fg))
    {
        fprintf(stderr, "multipass: Cannot alloc color '%s'\n", fg_str);
        return false;
    }

    return true;
}

void
redraw(void)
{
    XftDraw *xd;
    size_t i, c = 0, vis_c, line = 0;
    char buf[BUFSIZ] = "", title[BUFSIZ] = "";

    for (i = 0; i < MAX_TARGETS; i++)
        if (targets[i] != 0)
            c++;

    vis_c = c == 0 ? 1 : c;

    window_size(win_width, vis_c * font_height);

    XSetForeground(dpy, gc, bg.pixel);
    XFillRectangle(dpy, win, gc, 0, 0, win_width, vis_c * font_height);

    xd = XftDrawCreate(dpy, win, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen));
    if (c == 0)
    {
        XftDrawStringUtf8(xd, &fg, font,
                          font_horiz_margin, line * font_height + font_baseline,
                          (XftChar8 *)"<list empty>", strlen("<list empty>"));
    }
    else
    {
        for (i = 0; i < MAX_TARGETS; i++)
        {
            if (targets[i] != 0)
            {
                get_window_title(title, BUFSIZ, targets[i]);
                snprintf(buf, BUFSIZ, "%lu: %s", targets[i], title);
                XftDrawStringUtf8(xd, &fg, font,
                                  font_horiz_margin, line * font_height + font_baseline,
                                  (XftChar8 *)buf, strlen(buf));
                line++;
            }
        }
    }
    XftDrawDestroy(xd);
}

bool
remove_target(Window w)
{
    size_t i;

    for (i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] == w)
        {
            targets[i] = 0;
            D fprintf(stderr, "multipass: Removed targed %lu\n", w);
            return true;
        }
    }
    return false;
}

void
add_target(Window w)
{
    size_t i;

    if (w == win)
    {
        D fprintf(stderr, "multipass: Refusing to add myself\n");
        return;
    }

    for (i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] == 0)
        {
            targets[i] = w;
            D fprintf(stderr, "multipass: Added targed %lu\n", w);
            return;
        }
    }
}

void
handle(XKeyEvent *ev)
{
    Window dummy, target;
    int di;
    unsigned int dui;
    size_t i;

    if (ev->type == KeyPress && ev->keycode == selection_keycode)
    {
        XQueryPointer(dpy, root, &dummy, &target, &di, &di, &di, &di, &dui);
        if (!remove_target(target))
            add_target(target);
        redraw();
        return;
    }

    for (i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] != 0)
        {
            ev->window = targets[i];
            XSendEvent(dpy, targets[i], True, NoEventMask, (XEvent *)ev);
        }
    }
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
    if (ee->error_code == BadWindow)
    {
        remove_target(ee->resourceid);
        redraw();
        return 0;
    }
    return xerrorxlib(dpy, ee); /* may call exit */
}

int
main()
{
    XEvent ev;
    XClientMessageEvent *cm;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "multipass: Could not open X display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    create_window();

    if (!init_font_colors())
        exit(EXIT_FAILURE);

    xerrorxlib = XSetErrorHandler(xerror);

    for (;;)
    {
        XNextEvent(dpy, &ev);
        switch (ev.type)
        {
            case KeyPress:
            case KeyRelease:
                handle(&ev.xkey);
                break;
            case Expose:
                redraw();
                break;
            case ClientMessage:
                cm = &ev.xclient;
                if (cm->message_type == atom_protocols &&
                    (Atom)cm->data.l[0] == atom_delete)
                {
                    exit(EXIT_SUCCESS);
                }
                break;
        }
    }
}
