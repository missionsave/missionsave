#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/x.H>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <csignal>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>
#include "general.hpp"

Display* dpy;
Window root;
Atom net_client_list, net_active_window, net_supported;
Atom net_wm_name, net_wm_state, net_wm_window_type, net_wm_window_type_normal;
bool running = true;
std::unordered_set<Window> managed_clients;

struct ManagedWindow {
    Window client;
    Fl_Window* frame;
    Fl_Box* title;
    Fl_Button *close_btn, *minimize_btn, *maximize_btn;
    bool maximized = false;
    int orig_x, orig_y, orig_w, orig_h;
};

std::unordered_map<Window, ManagedWindow> managed_windows;

void update_client_list() {
    std::vector<Window> clients;
    for (auto& [win, mw] : managed_windows) {
        clients.push_back(win);
        Atom type = net_wm_window_type_normal;
        XChangeProperty(dpy, win, net_wm_window_type, XA_ATOM, 32,
                       PropModeReplace, (unsigned char*)&type, 1);
    }
    XChangeProperty(dpy, root, net_client_list, XA_WINDOW, 32,
                   PropModeReplace, (unsigned char*)clients.data(), clients.size());
}

void set_active_window(Window client) {
    if (client == None) return;
    
    XChangeProperty(dpy, root, net_active_window, XA_WINDOW, 32,
                   PropModeReplace, (unsigned char*)&client, 1);

    for (auto& [win, mw] : managed_windows) {
        bool is_active = (win == client);
        mw.frame->color(is_active ? FL_YELLOW : FL_WHITE);
        mw.title->color(is_active ? FL_YELLOW : FL_LIGHT2);
        mw.frame->redraw();
    }
    
    // Only set focus if window is visible
    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy, client, &attr)) {
        if (attr.map_state == IsViewable) {
            XSetInputFocus(dpy, client, RevertToParent, CurrentTime);
        }
    }
}

void update_title(Window win) {
    auto it = managed_windows.find(win);
    if (it != managed_windows.end()) {
        XTextProperty tp;
        if (XGetWMName(dpy, win, &tp)) {
            if (tp.value) {
                it->second.title->label((char*)tp.value);
                // XChangeProperty(dpy, win, net_wm_name, XA_STRING, 8,
                            //    PropModeReplace, tp.value, tp.nitems); //infinit loop
            }
            if (tp.value) XFree(tp.value);
        }
    }
}

void handle_signal(int) {
    running = false;
    Fl::awake();
}

void close_cb(Fl_Widget*, void* data) {
    auto* mw = (ManagedWindow*)data;
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", True);
    ev.xclient.format = 32;
    ev.xclient.window = mw->client;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, mw->client, False, NoEventMask, &ev);
}

void minimize_cb(Fl_Widget*, void* data) {
    auto* mw = (ManagedWindow*)data;
    XUnmapWindow(dpy, mw->client);
    mw->frame->hide();
    update_client_list();
}

void maximize_cb(Fl_Widget*, void* data) {
    auto* mw = (ManagedWindow*)data;
    if (mw->maximized) {
        mw->frame->resize(mw->orig_x, mw->orig_y, mw->orig_w, mw->orig_h);
        XResizeWindow(dpy, mw->client, mw->orig_w - 4, mw->orig_h - 30);
    } else {
        XWindowAttributes attr;
        XGetWindowAttributes(dpy, root, &attr);
        mw->orig_x = mw->frame->x();
        mw->orig_y = mw->frame->y();
        mw->orig_w = mw->frame->w();
        mw->orig_h = mw->frame->h();
        mw->frame->resize(0, 0, attr.width, attr.height);
        XResizeWindow(dpy, mw->client, attr.width - 4, attr.height - 30);
    }
    mw->maximized = !mw->maximized;
}

bool should_manage_window(Window win) {
    if (win == root) return false;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, win, &attr)) return false;

    if (attr.override_redirect) return false;
    if (attr.map_state == IsViewable) return false; // já visível, provavelmente já tem moldura

    if (managed_clients.count(win)) return false;   // já está gerenciado

    // return true;



    Atom net_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom actual_type;
    int format;
    unsigned long nitems, bytes;
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, win, net_type, 0, 1, False, XA_ATOM,
                          &actual_type, &format, &nitems, &bytes, &data) == Success) {
        if (data && nitems > 0) {
            Atom type = *(Atom*)data;
            Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
            Atom desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
            Atom splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
            Atom menu = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
            Atom tooltip = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);

            if (type == dock || type == desktop || type == splash ||
                type == menu || type == tooltip) {
                XFree(data);
                return false;
            }
        }
        XFree(data);
    }

    return true;
}


void manage_window(Window client) {
    if (!should_manage_window(client)) return;

    managed_clients.insert(client);

    XWindowAttributes attr;
    XGetWindowAttributes(dpy, client, &attr);

    const int title_height = 30;
    const int border = 2;

    int total_w = attr.width + 2 * border;
    int total_h = attr.height + title_height + border;

    // Janela FLTK (a moldura)
    Fl_Window* frame = new Fl_Window(attr.x, attr.y, total_w, total_h);
    frame->border(0);        // Sem borda do sistema
    frame->override();       // Importante! Não deixa o X11 gerar MapRequest para esta moldura
    frame->begin();

    Fl_Box* title = new Fl_Box(border, border, total_w - 2 * border, title_height, "Janela");
    title->box(FL_FLAT_BOX);
    title->color(FL_DARK_RED);
    title->labelfont(FL_BOLD);

    frame->end();
    frame->show();

    Window frame_id = fl_xid(frame);

    XSelectInput(dpy, client, StructureNotifyMask | PropertyChangeMask);

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    XChangeWindowAttributes(dpy, frame_id, CWOverrideRedirect, &swa);

    XReparentWindow(dpy, client, frame_id, border, title_height);
    XMapWindow(dpy, client);
    XMapRaised(dpy, frame_id);

    XSync(dpy, False);

    std::cout << "Managed client: " << client << " inside frame: " << frame_id << std::endl;
}

void unmanage_window(Window client) {
    auto it = managed_windows.find(client);
    if (it != managed_windows.end()) {
        // Reparent back to root before deleting
        XReparentWindow(dpy, client, root, it->second.frame->x(), it->second.frame->y());
        delete it->second.frame;
        managed_windows.erase(it);
        update_client_list();
    }
}

void run_autostart() {
    const char* apps[] = {"/home/super/taskbar","xterm", nullptr};
    for (int i = 0; apps[i]; ++i) {
        if (fork() == 0) {
            // Child process
            setsid();
            close(ConnectionNumber(dpy));
            execlp(apps[i], apps[i], nullptr);
            std::cerr << "Failed to launch " << apps[i] << std::endl;
            _exit(1);
        }
        usleep(100000); // Small delay between launches
    }
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    dpy = XOpenDisplay(nullptr);
    if (!dpy) return 1;
    root = DefaultRootWindow(dpy);

    // Initialize EWMH atoms
    net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_normal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);

    // Set supported EWMH properties
    Atom supported[] = {
        net_client_list,
        net_active_window,
        net_supported,
        net_wm_name,
        net_wm_state,
        net_wm_window_type,
        net_wm_window_type_normal,
        XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False),
        XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False)
    };
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32,
                   PropModeReplace, (unsigned char*)supported, sizeof(supported)/sizeof(Atom));

    // Set error handler and select events
    XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });

// XSelectInput(dpy, root,
//     SubstructureNotifyMask     |  // For UnmapNotify, DestroyNotify
//     SubstructureRedirectMask      // For MapRequest, ConfigureRequest
// );

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask |
                          StructureNotifyMask | PropertyChangeMask);
    XSetWindowBackground(dpy, root, WhitePixel(dpy, DefaultScreen(dpy)));
    XClearWindow(dpy, root);

    // Take control of window management
    XGrabServer(dpy);
    Window root_return, parent_return;
    Window* children;
    unsigned int nchildren;
    XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren);
    for (unsigned int i = 0; i < nchildren; i++) {
        XWindowAttributes attr;
        if (XGetWindowAttributes(dpy, children[i], &attr)) {
            if (attr.map_state == IsViewable) {
                manage_window(children[i]);
            }
        }
    }
    if (children) XFree(children);
    XUngrabServer(dpy);

    run_autostart();


// XSelectInput(dpy, root,
//     SubstructureNotifyMask     |  // For UnmapNotify, DestroyNotify
//     SubstructureRedirectMask   |  // For MapRequest, ConfigureRequest
//     PropertyChangeMask            // For PropertyNotify
// );


while (running) {
    XEvent ev;
    XNextEvent(dpy, &ev);  // Blocks efficiently, 0% CPU idle

		cotm(PropertyNotify,ev.type);
		cotmup;
    switch (ev.type) {

        case MapRequest:
            manage_window(ev.xmaprequest.window);
            break;

        case UnmapNotify:
            if (ev.xunmap.window != root)
                unmanage_window(ev.xunmap.window);
            break;

        case DestroyNotify:
            unmanage_window(ev.xdestroywindow.window);
            break;

        case ConfigureRequest: {
            auto* c = &ev.xconfigurerequest;
            XWindowChanges wc = { c->x, c->y, c->width, c->height,
                                  c->border_width, c->above, c->detail };
            XConfigureWindow(dpy, c->window, c->value_mask, &wc);
            break;
        }

        case PropertyNotify:
    if (ev.xproperty.atom == net_wm_name || ev.xproperty.atom == XA_WM_NAME) {
        update_title(ev.xproperty.window);
    }
    break;
    }
}

    XCloseDisplay(dpy);
    return 0;
}