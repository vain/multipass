#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

Atom atom_protocols, atom_delete, atom_net_wmname, atom_wm_state;
Display *dpy;
int screen;
Window root, win;
GC gc;
XftColor fg, bg;
XftFont *font;
int font_height, font_baseline, font_horiz_margin;
int win_width = 10, win_height = 10;
bool selecting = true;
Cursor crosshair;
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
            strncpy(buf, slist[0], buf_size);
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
    win_width = w;
    win_height = h;
}

void
create_window(void)
{
    XSetWindowAttributes wa = {
        .background_pixmap = ParentRelative,
        .event_mask = ButtonPressMask | KeyPressMask | KeyReleaseMask | ExposureMask,
    };
    XClassHint ch = {
        .res_class = "Multipass",
        .res_name = "multipass",
    };

    win = XCreateWindow(dpy, root, 0, 0, win_width, win_height, 0,
                        DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWBackPixmap | CWEventMask,
                        &wa);
    window_size(win_width, win_height);
    XSetClassHint(dpy, win, &ch);
    XMapWindow(dpy, win);

    atom_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    atom_net_wmname = XInternAtom(dpy, "_NET_WM_NAME", False);
    atom_wm_state = XInternAtom(dpy, "WM_STATE", False);
    XSetWMProtocols(dpy, win, &atom_delete, 1);

    XChangeProperty(dpy, win, atom_net_wmname,
                    XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
                    (unsigned char *)"multipass", strlen("multipass"));

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
    XGlyphInfo ext;
    size_t i, line;
    char buf[BUFSIZ] = "", title[BUFSIZ] = "";
    int new_w = 0, new_h, tw;

    XSetForeground(dpy, gc, bg.pixel);
    XFillRectangle(dpy, win, gc, 0, 0, win_width, win_height);

    xd = XftDrawCreate(dpy, win, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen));

    for (line = 0, i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] != 0)
        {
            get_window_title(title, BUFSIZ, targets[i]);
            snprintf(buf, BUFSIZ, "%lu: %s", targets[i], title);

            XftTextExtentsUtf8(dpy, font, (XftChar8 *)&buf, strlen(buf), &ext);
            tw = font_horiz_margin + ext.xOff + font_horiz_margin;
            new_w = MAX(new_w, tw);

            XftDrawStringUtf8(xd, &fg, font,
                              font_horiz_margin, line * font_height + font_baseline,
                              (XftChar8 *)buf, strlen(buf));
            line++;
        }
    }

    if (line == 0)
    {
        snprintf(buf, BUFSIZ, "<list empty>");
        XftTextExtentsUtf8(dpy, font, (XftChar8 *)&buf, strlen(buf), &ext);
        new_w = font_horiz_margin + ext.xOff + font_horiz_margin;

        XftDrawStringUtf8(xd, &fg, font,
                          font_horiz_margin, line * font_height + font_baseline,
                          (XftChar8 *)buf, strlen(buf));
        line++;
    }

    XftDrawDestroy(xd);

    new_h = line * font_height;

    if (new_w != win_width || new_h != win_height)
        window_size(new_w, new_h);
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
            XSelectInput(dpy, w, NoEventMask);
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
        return;

    for (i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] == 0)
        {
            targets[i] = w;
            XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
            return;
        }
    }
}

Window
find_window_with_wm_state(Window w)
{
    Atom *atoms = NULL;
    int inum = 0, i;
    unsigned int uinum = 0, ui;
    Window dummy, *wins = NULL, final;

    /* Check if the current window has the property WM_STATE. This
     * property is supposed to be set by any ICCCM complient window
     * manager. If the property is present, then this is (probably) the
     * window that the user meant to select. */
    atoms = XListProperties(dpy, w, &inum);
    if (atoms != NULL)
    {
        for (i = 0; i < inum; i++)
        {
            if (atoms[i] == atom_wm_state)
            {
                XFree(atoms);
                return w;
            }
        }
        XFree(atoms);
    }

    /* Okay, no WM_STATE on the current window. Have a look at all of
     * its child windows (and possibly grandchildren or whatever).
     * Iterate and recurse until you find a window with WM_STATE. */
    if (XQueryTree(dpy, w, &dummy, &dummy, &wins, &uinum))
    {
        for (ui = 0; ui < uinum; ui++)
        {
            final = find_window_with_wm_state(wins[ui]);
            if (final != None)
            {
                XFree(wins);
                return final;
            }
        }
        XFree(wins);
    }

    return None;
}

void
handle_button(XButtonEvent *ev)
{
    Window selected;

    if (ev->window == win || ev->button == Button3)
    {
        selecting = !selecting;
        if (selecting)
            XGrabPointer(dpy, root, False, ButtonPressMask, GrabModeAsync,
		                 GrabModeAsync, None, crosshair, CurrentTime);
        else
            XUngrabPointer(dpy, CurrentTime);
    }
    else
    {
        selected = find_window_with_wm_state(ev->subwindow);
        if (selected == None)
        {
            fprintf(stderr, "multipass: Could not find a client window\n");
            return;
        }

        if (!remove_target(selected))
            add_target(selected);
        redraw();
    }
}

void
handle_key(XKeyEvent *ev)
{
    size_t i;

    for (i = 0; i < MAX_TARGETS; i++)
    {
        if (targets[i] != 0)
        {
            ev->window = targets[i];
            XSendEvent(dpy, targets[i], True, NoEventMask, (XEvent *)ev);
        }
    }
}

void
handle_unmap(XUnmapEvent *ev)
{
    remove_target(ev->window);
    redraw();
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
    if (ee->error_code == BadWindow)
        return 0;

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

    crosshair = XCreateFontCursor(dpy, XC_crosshair);
    XGrabPointer(dpy, root, False, ButtonPressMask, GrabModeAsync,
                 GrabModeAsync, None, crosshair, CurrentTime);

    for (;;)
    {
        XNextEvent(dpy, &ev);
        switch (ev.type)
        {
            case ButtonPress:
                handle_button(&ev.xbutton);
                break;
            case KeyPress:
            case KeyRelease:
                handle_key(&ev.xkey);
                break;
            case Expose:
            case PropertyNotify:
                redraw();
                break;
            case UnmapNotify:
                handle_unmap(&ev.xunmap);
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
