/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "patches.h"
#include "drw.h"
#include "util.h"

#if BAR_FLEXWINTITLE_PATCH
#ifndef FLEXTILE_DELUXE_LAYOUT
#define FLEXTILE_DELUXE_LAYOUT 1
#endif
#endif

#if BAR_PANGO_PATCH
#include <pango/pango.h>
#endif // BAR_PANGO_PATCH

#if SPAWNCMD_PATCH
#include <assert.h>
#include <libgen.h>
#include <sys/stat.h>
#define SPAWN_CWD_DELIM " []{}()<>\"':"
#endif // SPAWNCMD_PATCH

/* macros */
#define Button6                 6
#define Button7                 7
#define Button8                 8
#define Button9                 9
#define NUMTAGS                 9
#define BARRULES                20
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#if BAR_ANYBAR_PATCH
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->mx+(m)->mw) - MAX((x),(m)->mx)) \
                               * MAX(0, MIN((y)+(h),(m)->my+(m)->mh) - MAX((y),(m)->my)))
#else
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#endif // BAR_ANYBAR_PATCH
#if ATTACHASIDE_PATCH && STICKY_PATCH
#define ISVISIBLEONTAG(C, T)    ((C->tags & T) || C->issticky)
#define ISVISIBLE(C)            ISVISIBLEONTAG(C, C->mon->tagset[C->mon->seltags])
#elif ATTACHASIDE_PATCH
#define ISVISIBLEONTAG(C, T)    ((C->tags & T))
#define ISVISIBLE(C)            ISVISIBLEONTAG(C, C->mon->tagset[C->mon->seltags])
#elif STICKY_PATCH
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]) || C->issticky)
#else
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#endif // ATTACHASIDE_PATCH
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define WTYPE                   "_NET_WM_WINDOW_TYPE_"
#if SCRATCHPADS_PATCH
#define TOTALTAGS               (NUMTAGS + LENGTH(scratchpads))
#define TAGMASK                 ((1 << TOTALTAGS) - 1)
#define SPTAG(i)                ((1 << NUMTAGS) << (i))
#define SPTAGMASK               (((1 << LENGTH(scratchpads))-1) << NUMTAGS)
#else
#define TAGMASK                 ((1 << NUMTAGS) - 1)
#endif // SCRATCHPADS_PATCH
#define TEXTWM(X)               (drw_fontset_getwidth(drw, (X), True) + lrpad)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X), False) + lrpad)
#define HIDDEN(C)               ((getstate(C->win) == IconicState))

/* enums */
enum {
	CurResizeBR,
	CurResizeBL,
	CurResizeTR,
	CurResizeTL,
	CurResizeHorzArrow,
	CurResizeVertArrow,
	CurIronCross,
	CurNormal,
	CurResize,
	CurMove,
	CurLast
}; /* cursor */

enum {
	SchemeNorm,
	SchemeSel,
	SchemeTitleNorm,
	SchemeTitleSel,
	SchemeTagsNorm,
	SchemeTagsSel,
	SchemeHid,
	SchemeUrg,
	#if BAR_FLEXWINTITLE_PATCH
	SchemeFlexActTTB,
	SchemeFlexActLTR,
	SchemeFlexActMONO,
	SchemeFlexActGRID,
	SchemeFlexActGRD1,
	SchemeFlexActGRD2,
	SchemeFlexActGRDM,
	SchemeFlexActHGRD,
	SchemeFlexActDWDL,
	SchemeFlexActSPRL,
	SchemeFlexInaTTB,
	SchemeFlexInaLTR,
	SchemeFlexInaMONO,
	SchemeFlexInaGRID,
	SchemeFlexInaGRD1,
	SchemeFlexInaGRD2,
	SchemeFlexInaGRDM,
	SchemeFlexInaHGRD,
	SchemeFlexInaDWDL,
	SchemeFlexInaSPRL,
	SchemeFlexSelTTB,
	SchemeFlexSelLTR,
	SchemeFlexSelMONO,
	SchemeFlexSelGRID,
	SchemeFlexSelGRD1,
	SchemeFlexSelGRD2,
	SchemeFlexSelGRDM,
	SchemeFlexSelHGRD,
	SchemeFlexSelDWDL,
	SchemeFlexSelSPRL,
	SchemeFlexActFloat,
	SchemeFlexInaFloat,
	SchemeFlexSelFloat,
	#endif // BAR_FLEXWINTITLE_PATCH
}; /* color schemes */

enum {
	NetSupported, NetWMName, NetWMState, NetWMCheck,
	NetWMFullscreen, NetActiveWindow, NetWMWindowType,
	#if BAR_SYSTRAY_PATCH
	NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation,
	NetSystemTrayVisual, NetWMWindowTypeDock, NetSystemTrayOrientationHorz,
	#endif // BAR_SYSTRAY_PATCH
	#if BAR_EWMHTAGS_PATCH
	NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop,
	#endif // BAR_EWMHTAGS_PATCH
	NetClientList, NetLast
}; /* EWMH atoms */

enum {
	WMProtocols,
	WMDelete,
	WMState,
	WMTakeFocus,
	#if WINDOWROLERULE_PATCH
	WMWindowRole,
	#endif // WINDOWROLERULE_PATCH
	WMLast
}; /* default atoms */

enum {
	#if BAR_STATUSBUTTON_PATCH
	ClkButton,
	#endif // BAR_STATUSBUTTON_PATCH
	ClkTagBar,
	ClkLtSymbol,
	ClkStatusText,
	ClkWinTitle,
	ClkClientWin,
	ClkRootWin,
	ClkLast
}; /* clicks */

enum {
	BAR_ALIGN_LEFT,
	BAR_ALIGN_CENTER,
	BAR_ALIGN_RIGHT,
	BAR_ALIGN_LEFT_LEFT,
	BAR_ALIGN_LEFT_RIGHT,
	BAR_ALIGN_LEFT_CENTER,
	BAR_ALIGN_NONE,
	BAR_ALIGN_RIGHT_LEFT,
	BAR_ALIGN_RIGHT_RIGHT,
	BAR_ALIGN_RIGHT_CENTER,
	BAR_ALIGN_LAST
}; /* bar alignment */

#if IPC_PATCH
typedef struct TagState TagState;
struct TagState {
       int selected;
       int occupied;
       int urgent;
};

typedef struct ClientState ClientState;
struct ClientState {
       int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
};
#endif // IPC_PATCH

typedef union {
	#if IPC_PATCH
	long i;
	unsigned long ui;
	#else
	int i;
	unsigned int ui;
	#endif // IPC_PATCH
	float f;
	const void *v;
} Arg;

typedef struct Monitor Monitor;
typedef struct Bar Bar;
struct Bar {
	Window win;
	Monitor *mon;
	Bar *next;
	int idx;
	int showbar;
	int topbar;
	int external;
	int borderpx;
	int borderscheme;
	int bx, by, bw, bh; /* bar geometry */
	int w[BARRULES]; // width, array length == barrules, then use r index for lookup purposes
	int x[BARRULES]; // x position, array length == ^
};

typedef struct {
	int x;
	int y;
	int h;
	int w;
} BarArg;

typedef struct {
	int monitor;
	int bar;
	int alignment; // see bar alignment enum
	int (*widthfunc)(Bar *bar, BarArg *a);
	int (*drawfunc)(Bar *bar, BarArg *a);
	int (*clickfunc)(Bar *bar, Arg *arg, BarArg *a);
	char *name; // for debugging
	int x, w; // position, width for internal use
} BarRule;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	#if CFACTS_PATCH
	float cfact;
	#endif // CFACTS_PATCH
	int x, y, w, h;
	#if SAVEFLOATS_PATCH
	int sfx, sfy, sfw, sfh; /* stored float geometry, used on mode revert */
	#endif // SAVEFLOATS_PATCH
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	#if SWITCHTAG_PATCH
	unsigned int switchtag;
	#endif // SWITCHTAG_PATCH
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	int fakefullscreen;
	#if AUTORESIZE_PATCH
	int needresize;
	#endif // AUTORESIZE_PATCH
	#if ISPERMANENT_PATCH
	int ispermanent;
	#endif // ISPERMANENT_PATCH
	#if SWALLOW_PATCH
	int isterminal, noswallow;
	pid_t pid;
	#endif // SWALLOW_PATCH
	#if STICKY_PATCH
	int issticky;
	#endif // STICKY_PATCH
	Client *next;
	Client *snext;
	#if SWALLOW_PATCH
	Client *swallowing;
	#endif // SWALLOW_PATCH
	Monitor *mon;
	Window win;
	#if IPC_PATCH
	ClientState prevstate;
	#endif // IPC_PATCH
	long flags;
	long prevflags;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

#if FLEXTILE_DELUXE_LAYOUT
typedef struct {
	int nmaster;
	int nstack;
	int layout;
	int masteraxis; // master stack area
	int stack1axis; // primary stack area
	int stack2axis; // secondary stack area, e.g. centered master
	void (*symbolfunc)(Monitor *, unsigned int);
} LayoutPreset;
#endif // FLEXTILE_DELUXE_LAYOUT

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
	#if FLEXTILE_DELUXE_LAYOUT
	LayoutPreset preset;
	#endif // FLEXTILE_DELUXE_LAYOUT
} Layout;

#if INSETS_PATCH
typedef struct {
	int x;
	int y;
	int w;
	int h;
} Inset;
#endif // INSETS_PATCH

typedef struct Pertag Pertag;
struct Monitor {
	int index;
	char ltsymbol[16];
	float mfact;
	#if FLEXTILE_DELUXE_LAYOUT
	int ltaxis[4];
	int nstack;
	#endif // FLEXTILE_DELUXE_LAYOUT
	int nmaster;
	int num;
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	#if VANITYGAPS_PATCH
	int gappih;           /* horizontal gap between windows */
	int gappiv;           /* vertical gap between windows */
	int gappoh;           /* horizontal outer gaps */
	int gappov;           /* vertical outer gaps */
	#endif // VANITYGAPS_PATCH
	#if SETBORDERPX_PATCH
	unsigned int borderpx;
	#endif // SETBORDERPX_PATCH
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Bar *bar;
	const Layout *lt[2];
	#if BAR_ALTERNATIVE_TAGS_PATCH
	unsigned int alttag;
	#endif // BAR_ALTERNATIVE_TAGS_PATCH
	Pertag *pertag;
	#if INSETS_PATCH
	Inset inset;
	#endif // INSETS_PATCH
	#if IPC_PATCH
	char lastltsymbol[16];
	TagState tagstate;
	Client *lastsel;
	const Layout *lastlt;
	#endif // IPC_PATCH
};

typedef struct {
	const char *class;
	#if WINDOWROLERULE_PATCH
	const char *role;
	#endif // WINDOWROLERULE_PATCH
	const char *instance;
	const char *title;
	const char *wintype;
	unsigned int tags;
	#if SWITCHTAG_PATCH
	int switchtag;
	#endif // SWITCHTAG_PATCH
	#if CENTER_PATCH
	int iscentered;
	#endif // CENTER_PATCH
	int isfloating;
	#if SELECTIVEFAKEFULLSCREEN_PATCH
	int isfakefullscreen;
	#endif // SELECTIVEFAKEFULLSCREEN_PATCH
	#if ISPERMANENT_PATCH
	int ispermanent;
	#endif // ISPERMANENT_PATCH
	#if SWALLOW_PATCH
	int isterminal;
	int noswallow;
	#endif // SWALLOW_PATCH
	#if FLOATPOS_PATCH
	const char *floatpos;
	#endif // FLOATPOS_PATCH
	int monitor;
} Rule;

#define RULE(...) { .monitor = -1, ##__VA_ARGS__ },

/* Cross patch compatibility rule macro helper macros */
#define FLOATING , .isfloating = 1
#if CENTER_PATCH
#define CENTERED , .iscentered = 1
#else
#define CENTERED
#endif // CENTER_PATCH
#if ISPERMANENT_PATCH
#define PERMANENT , .ispermanent = 1
#else
#define PERMANENT
#endif // ISPERMANENT_PATCH
#if SELECTIVEFAKEFULLSCREEN_PATCH
#define FAKEFULLSCREEN , .isfakefullscreen = 1
#else
#define FAKEFULLSCREEN
#endif // SELECTIVEFAKEFULLSCREEN_PATCH
#if SWALLOW_PATCH
#define NOSWALLOW , .noswallow = 1
#define TERMINAL , .isterminal = 1
#else
#define NOSWALLOW
#define TERMINAL
#endif // SWALLOW_PATCH
#if SWITCHTAG_PATCH
#define SWITCHTAG , .switchtag = 1
#else
#define SWITCHTAG
#endif // SWITCHTAG_PATCH

#if MONITOR_RULES_PATCH
typedef struct {
	int monitor;
	int tag;
	int layout;
	float mfact;
	int nmaster;
	int showbar;
	int topbar;
} MonitorRule;
#endif // MONITOR_RULES_PATCH

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawbarwin(Bar *bar);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
#if !STACKER_PATCH
static void focusstack(const Arg *arg);
#endif // STACKER_PATCH
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
#if KEYMODES_PATCH
static void grabdefkeys(void);
#else
static void grabkeys(void);
#endif // KEYMODES_PATCH
static void incnmaster(const Arg *arg);
#if KEYMODES_PATCH
static void keydefpress(XEvent *e);
#else
static void keypress(XEvent *e);
#endif // KEYMODES_PATCH
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
#if !ZOOMSWAP_PATCH || TAGINTOSTACK_ALLMASTER_PATCH || TAGINTOSTACK_ONEMASTER_PATCH
static void pop(Client *);
#endif // !ZOOMSWAP_PATCH / TAGINTOSTACK_ALLMASTER_PATCH / TAGINTOSTACK_ONEMASTER_PATCH
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
#if BAR_SYSTRAY_PATCH
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
#else
static int sendevent(Client *c, Atom proto);
#endif // BAR_SYSTRAY_PATCH
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus, Client *nextfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* bar functions */

#include "patch/include.h"

/* variables */
static const char broken[] = "broken";
#if BAR_PANGO_PATCH || BAR_STATUS2D_PATCH && !BAR_STATUSCOLORS_PATCH
static char stext[1024];
#else
static char stext[512];
#endif // BAR_PANGO_PATCH | BAR_STATUS2D_PATCH
#if BAR_EXTRASTATUS_PATCH || BAR_STATUSCMD_PATCH
#if BAR_STATUS2D_PATCH
static char rawstext[1024];
#else
static char rawstext[512];
#endif // BAR_STATUS2D_PATCH
#endif // BAR_EXTRASTATUS_PATCH | BAR_STATUSCMD_PATCH
#if BAR_EXTRASTATUS_PATCH
#if BAR_STATUS2D_PATCH && !BAR_STATUSCOLORS_PATCH
static char estext[1024];
#else
static char estext[512];
#endif // BAR_STATUS2D_PATCH
#if BAR_STATUSCMD_PATCH
static char rawestext[1024];
#endif // BAR_STATUS2D_PATCH | BAR_STATUSCMD_PATCH
#endif // BAR_EXTRASTATUS_PATCH

static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
/* Some clients (e.g. alacritty) helpfully send configure requests with a new size or position
 * when they detect that they have been moved to another monitor. This can cause visual glitches
 * when moving (or resizing) client windows from one monitor to another. This variable is used
 * internally to ignore such configure requests while movemouse or resizemouse are being used. */
static int ignoreconfigurerequests = 0;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	#if COMBO_PATCH || BAR_HOLDBAR_PATCH
	[ButtonRelease] = keyrelease,
	#endif // COMBO_PATCH / BAR_HOLDBAR_PATCH
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	#if COMBO_PATCH || BAR_HOLDBAR_PATCH
	[KeyRelease] = keyrelease,
	#endif // COMBO_PATCH / BAR_HOLDBAR_PATCH
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	#if BAR_SYSTRAY_PATCH
	[ResizeRequest] = resizerequest,
	#endif // BAR_SYSTRAY_PATCH
	[UnmapNotify] = unmapnotify
};
#if BAR_SYSTRAY_PATCH
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
#else
static Atom wmatom[WMLast], netatom[NetLast];
#endif // BAR_SYSTRAY_PATCH
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

#include "patch/include.c"

/* compile-time check if all tags fit into an unsigned int bit array. */
#if SCRATCHPAD_ALT_1_PATCH
struct NumTags { char limitexceeded[NUMTAGS > 30 ? -1 : 1]; };
#else
struct NumTags { char limitexceeded[NUMTAGS > 31 ? -1 : 1]; };
#endif // SCRATCHPAD_ALT_1_PATCH

/* function implementations */
void
applyrules(Client *c)
{
	const char *class, *instance;
	Atom wintype;
	#if WINDOWROLERULE_PATCH
	char role[64];
	#endif // WINDOWROLERULE_PATCH
	unsigned int i;
	#if SWITCHTAG_PATCH
	unsigned int newtagset;
	#endif // SWITCHTAG_PATCH
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	#if SWALLOW_PATCH
	c->noswallow = -1;
	#endif // SWALLOW_PATCH
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;
	wintype  = getatomprop(c, netatom[NetWMWindowType]);
	#if WINDOWROLERULE_PATCH
	gettextprop(c->win, wmatom[WMWindowRole], role, sizeof(role));
	#endif // WINDOWROLERULE_PATCH
	// long flags;

	#if STEAM_PATCH
	if (strstr(class, "Steam") || strstr(class, "steam_app_"))
		// flags |= 1 << IsSteam;
		addflag(c, IsSteam);
	#endif // STEAM_PATCH

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		#if WINDOWROLERULE_PATCH
		&& (!r->role || strstr(role, r->role))
		#endif // WINDOWROLERULE_PATCH
		&& (!r->instance || strstr(instance, r->instance))
		&& (!r->wintype || wintype == XInternAtom(dpy, r->wintype, False)))
		{
			// flags = flags
			// 	| r->iscentered << IsCentered
			// 	| r-ispermanent << IsPermanent
			// 	| r->isfakefullscreen << IsFakeFullScreen;
			#if CENTER_PATCH
			if (r->iscentered)
				addflag(c, IsCentered);
			#endif // CENTER_PATCH
			#if ISPERMANENT_PATCH
			if (r->ispermanent)
				addflag(c, IsPermanent);
			// c->ispermanent = r->ispermanent;
			#endif // ISPERMANENT_PATCH
			#if SELECTIVEFAKEFULLSCREEN_PATCH
			c->fakefullscreen = r->isfakefullscreen;
			#endif // SELECTIVEFAKEFULLSCREEN_PATCH
			#if SWALLOW_PATCH
			c->isterminal = r->isterminal;
			c->noswallow = r->noswallow;
			#endif // SWALLOW_PATCH
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			#if SCRATCHPADS_PATCH
			if ((r->tags & SPTAGMASK) && r->isfloating) {
				c->x = c->mon->wx + (c->mon->ww / 2 - WIDTH(c) / 2);
				c->y = c->mon->wy + (c->mon->wh / 2 - HEIGHT(c) / 2);
			}
			#endif // SCRATCHPADS_PATCH
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
			#if FLOATPOS_PATCH
			if (c->isfloating && r->floatpos)
				setfloatpos(c, r->floatpos);
			#endif // FLOATPOS_PATCH

			#if SWITCHTAG_PATCH
			#if SWALLOW_PATCH
			if (r->switchtag && (
				c->noswallow > 0 ||
				!termforwin(c) ||
				!(c->isfloating && swallowfloating && c->noswallow < 0)))
			#else
			if (r->switchtag)
			#endif // SWALLOW_PATCH
			{
				selmon = c->mon;
				if (r->switchtag == 2 || r->switchtag == 4)
					newtagset = c->mon->tagset[c->mon->seltags] ^ c->tags;
				else
					newtagset = c->tags;

				/* Switch to the client's tag, but only if that tag is not already shown */
				if (newtagset && !(c->tags & c->mon->tagset[c->mon->seltags])) {
					if (r->switchtag == 3 || r->switchtag == 4)
						c->switchtag = c->mon->tagset[c->mon->seltags];
					if (r->switchtag == 1 || r->switchtag == 3) {
						pertagview(&((Arg) { .ui = newtagset }));
						arrange(c->mon);
					} else {
						c->mon->tagset[c->mon->seltags] = newtagset;
						arrange(c->mon);
					}
				}
			}
			#endif // SWITCHTAG_PATCH
			#if ONLY_ONE_RULE_MATCH_PATCH
			break;
			#endif // ONLY_ONE_RULE_MATCH_PATCH
		}
	}
	// setflags(c, flags);
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	#if SCRATCHPADS_PATCH
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : (c->mon->tagset[c->mon->seltags] & ~SPTAGMASK);
	#elif SCRATCHPAD_ALT_1_PATCH
	if (c->tags != SCRATCHPAD_MASK)
		c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
	#else
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
	#endif // SCRATCHPADS_PATCH
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	#if ROUNDED_CORNERS_PATCH
	Client *c;
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		drawroundedcorners(c);
	#endif // ROUNDED_CORNERS_PATCH
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	int click, i, r;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	Bar *bar;
	XButtonPressedEvent *ev = &e->xbutton;
	const BarRule *br;
	BarArg carg = { 0, 0, 0, 0 };
	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1, NULL);
		selmon = m;
		focus(NULL);
	}

	for (bar = selmon->bar; bar; bar = bar->next) {
		if (ev->window == bar->win) {
			for (r = 0; r < LENGTH(barrules); r++) {
				br = &barrules[r];
				if (br->bar != bar->idx || (br->monitor == 'A' && m != selmon) || br->clickfunc == NULL)
					continue;
				if (br->monitor != 'A' && br->monitor != -1 && br->monitor != bar->mon->index)
					continue;
				if (bar->x[r] <= ev->x && ev->x <= bar->x[r] + bar->w[r]) {
					carg.x = ev->x - bar->x[r];
					carg.y = ev->y - bar->borderpx;
					carg.w = bar->w[r];
					carg.h = bar->bh - 2 * bar->borderpx;
					click = br->clickfunc(bar, &arg, &carg);
					if (click < 0)
						return;
					break;
				}
			}
			break;
		}
	}

	if (click == ClkRootWin && (c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}

	for (i = 0; i < LENGTH(buttons); i++) {
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
				&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
			#if BAR_WINTITLEACTIONS_PATCH
			buttons[i].func((click == ClkTagBar || click == ClkWinTitle) && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
			#else
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
			#endif // BAR_WINTITLEACTIONS_PATCH
		}
	}
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;
	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	#if BAR_SYSTRAY_PATCH
	if (showsystray && systray) {
		if (systray->win) {
			XUnmapWindow(dpy, systray->win);
			XDestroyWindow(dpy, systray->win);
		}
		free(systray);
	}
	#endif // BAR_SYSTRAY_PATCH
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	#if BAR_STATUS2D_PATCH && !BAR_STATUSCOLORS_PATCH
	for (i = 0; i < LENGTH(colors) + 1; i++)
	#else
	for (i = 0; i < LENGTH(colors); i++)
	#endif // BAR_STATUS2D_PATCH
		free(scheme[i]);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);

	#if IPC_PATCH
	ipc_cleanup();

	if (close(epoll_fd) < 0)
		fprintf(stderr, "Failed to close epoll file descriptor\n");
	#endif // IPC_PATCH
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;
	Bar *bar;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	for (bar = mon->bar; bar; bar = mon->bar) {
		if (!bar->external) {
			XUnmapWindow(dpy, bar->win);
			XDestroyWindow(dpy, bar->win);
		}
		mon->bar = bar->next;
		free(bar);
	}
	free(mon);
}

void
clientmessage(XEvent *e)
{
	#if BAR_SYSTRAY_PATCH
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	#endif // BAR_SYSTRAY_PATCH
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);
	#if FOCUSONNETACTIVE_PATCH
	unsigned int i;
	#endif // FOCUSONNETACTIVE_PATCH

	#if BAR_SYSTRAY_PATCH
	if (showsystray && systray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		/* add systray icons */
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			if (!(c->win = cme->data.l[2])) {
				free(c);
				return;
			}

			c->mon = selmon;
			c->next = systray->icons;
			systray->icons = c;
			XGetWindowAttributes(dpy, c->win, &wa);
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			/* reuse tags field as mapped status */
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XClassHint ch = {"dwmsystray", "dwmsystray"};
			XSetClassHint(dpy, c->win, &ch);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			/* use parents background color */
			swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			XSync(dpy, False);
			setclientstate(c, NormalState);
		}
		return;
	}
	#endif // BAR_SYSTRAY_PATCH

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen]) {
			if (c->fakefullscreen == 2 && c->isfullscreen)
				c->fakefullscreen = 3;
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */
				&& !c->isfullscreen
			)));
		}
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		#if FOCUSONNETACTIVE_PATCH
		if (c->tags & c->mon->tagset[c->mon->seltags]) {
			selmon = c->mon;
			focus(c);
		} else {
			for (i = 0; i < NUMTAGS && !((1 << i) & c->tags); i++);
			if (i < NUMTAGS) {
				selmon = c->mon;
				if (((1 << i) & TAGMASK) != selmon->tagset[selmon->seltags])
					view(&((Arg) { .ui = 1 << i }));
				focus(c);
				restack(selmon);
			}
		}
		#else
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
		#endif // FOCUSONNETACTIVE_PATCH
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Bar *bar;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;
	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen && c->fakefullscreen != 1)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				for (bar = m->bar; bar; bar = bar->next)
					XMoveResizeWindow(dpy, bar->win, bar->bx, bar->by, bar->bw, bar->bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	#if BAR_ANYBAR_PATCH
	Bar *bar;
	#endif // BAR_ANYBAR_PATCH
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if (ignoreconfigurerequests)
		return;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			#if STEAM_PATCH
			if (!ISSTEAM(c)) {
				if (ev->value_mask & CWX) {
					c->oldx = c->x;
					c->x = m->mx + ev->x;
				}
				if (ev->value_mask & CWY) {
					c->oldy = c->y;
					c->y = m->my + ev->y;
				}
			}
			#else
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			#endif // STEAM_PATCH
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2);  /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
			#if AUTORESIZE_PATCH
			else
				c->needresize = 1;
			#endif // AUTORESIZE_PATCH
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		#if BAR_ANYBAR_PATCH
		m = wintomon(ev->window);
		for (bar = m->bar; bar; bar = bar->next) {
			if (bar->win == ev->window) {
				wc.y = bar->by;
				wc.x = bar->bx;
			}
		}
		#endif // BAR_ANYBAR_PATCH
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m, *mon;
	int i, n, mi, max_bars = 2, istopbar = topbar;
	#if MONITOR_RULES_PATCH
	int layout;
	#endif // MONITOR_RULES_PATCH

	const BarRule *br;
	Bar *bar;
	#if MONITOR_RULES_PATCH
	int j;
	const MonitorRule *mr;
	#endif // MONITOR_RULES_PATCH

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	#if FLEXTILE_DELUXE_LAYOUT
	m->nstack = nstack;
	#endif // FLEXTILE_DELUXE_LAYOUT
	m->showbar = showbar;
	#if SETBORDERPX_PATCH
	m->borderpx = borderpx;
	#endif // SETBORDERPX_PATCH
	#if VANITYGAPS_PATCH
	m->gappih = gappih;
	m->gappiv = gappiv;
	m->gappoh = gappoh;
	m->gappov = gappov;
	#endif // VANITYGAPS_PATCH
	for (mi = 0, mon = mons; mon; mon = mon->next, mi++); // monitor index
	m->index = mi;
	#if MONITOR_RULES_PATCH
	for (j = 0; j < LENGTH(monrules); j++) {
		mr = &monrules[j];
		if ((mr->monitor == -1 || mr->monitor == mi)
				&& (mr->tag <= 0 || (m->tagset[0] & (1 << (mr->tag - 1))))) {
			layout = MAX(mr->layout, 0);
			layout = MIN(layout, LENGTH(layouts) - 1);
			m->lt[0] = &layouts[layout];
			m->lt[1] = &layouts[1 % LENGTH(layouts)];
			strncpy(m->ltsymbol, layouts[layout].symbol, sizeof m->ltsymbol);

			if (mr->mfact > -1)
				m->mfact = mr->mfact;
			if (mr->nmaster > -1)
				m->nmaster = mr->nmaster;
			if (mr->showbar > -1)
				m->showbar = mr->showbar;
			if (mr->topbar > -1)
				istopbar = mr->topbar;
			break;
		}
	}
	#else
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	#endif // MONITOR_RULES_PATCH

	/* Derive the number of bars for this monitor based on bar rules */
	for (n = -1, i = 0; i < LENGTH(barrules); i++) {
		br = &barrules[i];
		if (br->monitor == 'A' || br->monitor == -1 || br->monitor == mi)
			n = MAX(br->bar, n);
	}

	for (i = 0; i <= n && i < max_bars; i++) {
		bar = ecalloc(1, sizeof(Bar));
		bar->mon = m;
		bar->idx = i;
		bar->next = m->bar;
		bar->topbar = istopbar;
		m->bar = bar;
		istopbar = !istopbar;
		bar->showbar = 1;
		bar->external = 0;
		#if BAR_BORDER_PATCH
		bar->borderpx = borderpx;
		#else
		bar->borderpx = 0;
		#endif // BAR_BORDER_PATCH
		bar->bh = bh + bar->borderpx * 2;
		bar->borderscheme = SchemeNorm;
	}

	#if FLEXTILE_DELUXE_LAYOUT
	m->ltaxis[LAYOUT] = m->lt[0]->preset.layout;
	m->ltaxis[MASTER] = m->lt[0]->preset.masteraxis;
	m->ltaxis[STACK]  = m->lt[0]->preset.stack1axis;
	m->ltaxis[STACK2] = m->lt[0]->preset.stack2axis;
	#endif // FLEXTILE_DELUXE_LAYOUT

	if (!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;
	for (i = 0; i <= NUMTAGS; i++) {
		#if FLEXTILE_DELUXE_LAYOUT
		m->pertag->nstacks[i] = m->nstack;
		#endif // FLEXTILE_DELUXE_LAYOUT

		#if !MONITOR_RULES_PATCH
		/* init nmaster */
		m->pertag->nmasters[i] = m->nmaster;

		/* init mfacts */
		m->pertag->mfacts[i] = m->mfact;

		#if PERTAGBAR_PATCH
		/* init showbar */
		m->pertag->showbars[i] = m->showbar;
		#endif // PERTAGBAR_PATCH
		#endif // MONITOR_RULES_PATCH

		#if ZOOMSWAP_PATCH
		m->pertag->prevzooms[i] = NULL;
		#endif // ZOOMSWAP_PATCH

		/* init layouts */
		#if MONITOR_RULES_PATCH
		for (j = 0; j < LENGTH(monrules); j++) {
			mr = &monrules[j];
			if ((mr->monitor == -1 || mr->monitor == mi) && (mr->tag == -1 || mr->tag == i)) {
				layout = MAX(mr->layout, 0);
				layout = MIN(layout, LENGTH(layouts) - 1);
				m->pertag->ltidxs[i][0] = &layouts[layout];
				m->pertag->ltidxs[i][1] = m->lt[0];
				m->pertag->nmasters[i] = (mr->nmaster > -1 ? mr->nmaster : m->nmaster);
				m->pertag->mfacts[i] = (mr->mfact > -1 ? mr->mfact : m->mfact);
				#if PERTAGBAR_PATCH
				m->pertag->showbars[i] = (mr->showbar > -1 ? mr->showbar : m->showbar);
				#endif // PERTAGBAR_PATCH
				#if FLEXTILE_DELUXE_LAYOUT
				m->pertag->ltaxis[i][LAYOUT] = m->pertag->ltidxs[i][0]->preset.layout;
				m->pertag->ltaxis[i][MASTER] = m->pertag->ltidxs[i][0]->preset.masteraxis;
				m->pertag->ltaxis[i][STACK]  = m->pertag->ltidxs[i][0]->preset.stack1axis;
				m->pertag->ltaxis[i][STACK2] = m->pertag->ltidxs[i][0]->preset.stack2axis;
				#endif // FLEXTILE_DELUXE_LAYOUT
				break;
			}
		}
		#else
		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		#if FLEXTILE_DELUXE_LAYOUT
		/* init flextile axes */
		m->pertag->ltaxis[i][LAYOUT] = m->ltaxis[LAYOUT];
		m->pertag->ltaxis[i][MASTER] = m->ltaxis[MASTER];
		m->pertag->ltaxis[i][STACK]  = m->ltaxis[STACK];
		m->pertag->ltaxis[i][STACK2] = m->ltaxis[STACK2];
		#endif // FLEXTILE_DELUXE_LAYOUT
		#endif // MONITOR_RULES_PATCH
		m->pertag->sellts[i] = m->sellt;

		#if VANITYGAPS_PATCH
		m->pertag->enablegaps[i] = 1;
		#endif // VANITYGAPS_PATCH
	}
	#if INSETS_PATCH
	m->inset = default_inset;
	#endif // INSETS_PATCH
	return m;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	#if BAR_ANYBAR_PATCH
	Monitor *m;
	Bar *bar;
	#endif // BAR_ANYBAR_PATCH
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
	#if SWALLOW_PATCH
	else if ((c = swallowingclient(ev->window)))
		unmanage(c->swallowing, 1);
	#endif // SWALLOW_PATCH
	#if BAR_SYSTRAY_PATCH
	else if (showsystray && (c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		drawbarwin(systray->bar);
	}
	#endif // BAR_SYSTRAY_PATCH
	#if BAR_ANYBAR_PATCH
	else {
		 m = wintomon(ev->window);
		 for (bar = m->bar; bar; bar = bar->next) {
		 	if (bar->win == ev->window) {
				unmanagealtbar(ev->window);
				break;
			}
		}
	}
	#endif // BAR_ANYBAR_PATCH
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

void
drawbar(Monitor *m)
{
	Bar *bar;
	for (bar = m->bar; bar; bar = bar->next)
		drawbarwin(bar);
}

void
drawbars(void)
{
	Monitor *m;
	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
drawbarwin(Bar *bar)
{
	if (!bar->win || bar->external)
		return;
	int r, w, total_drawn = 0;
	int rx, lx, rw, lw; // bar size, split between left and right if a center module is added
	const BarRule *br;

	if (bar->borderpx) {
		XSetForeground(drw->dpy, drw->gc, scheme[bar->borderscheme][ColBorder].pixel);
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, bar->bw, bar->bh);
	}

	BarArg warg = { 0 };
	BarArg darg  = { 0 };
	warg.h = bar->bh - 2 * bar->borderpx;

	rw = lw = bar->bw - 2 * bar->borderpx;
	rx = lx = bar->borderpx;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, lx, bar->borderpx, lw, bar->bh - 2 * bar->borderpx, 1, 1);
	for (r = 0; r < LENGTH(barrules); r++) {
		br = &barrules[r];
		if (br->bar != bar->idx || !br->widthfunc || (br->monitor == 'A' && bar->mon != selmon))
			continue;
		if (br->monitor != 'A' && br->monitor != -1 && br->monitor != bar->mon->index)
			continue;
		drw_setscheme(drw, scheme[SchemeNorm]);
		warg.w = (br->alignment < BAR_ALIGN_RIGHT_LEFT ? lw : rw);

		w = br->widthfunc(bar, &warg);
		w = MIN(warg.w, w);

		if (lw <= 0) { // if left is exhausted then switch to right side, and vice versa
			lw = rw;
			lx = rx;
		} else if (rw <= 0) {
			rw = lw;
			rx = lx;
		}

		switch(br->alignment) {
		default:
		case BAR_ALIGN_NONE:
		case BAR_ALIGN_LEFT_LEFT:
		case BAR_ALIGN_LEFT:
			bar->x[r] = lx;
			if (lx == rx) {
				rx += w;
				rw -= w;
			}
			lx += w;
			lw -= w;
			break;
		case BAR_ALIGN_LEFT_RIGHT:
		case BAR_ALIGN_RIGHT:
			bar->x[r] = lx + lw - w;
			if (lx == rx)
				rw -= w;
			lw -= w;
			break;
		case BAR_ALIGN_LEFT_CENTER:
		case BAR_ALIGN_CENTER:
			bar->x[r] = lx + lw / 2 - w / 2;
			if (lx == rx) {
				rw = rx + rw - bar->x[r] - w;
				rx = bar->x[r] + w;
			}
			lw = bar->x[r] - lx;
			break;
		case BAR_ALIGN_RIGHT_LEFT:
			bar->x[r] = rx;
			if (lx == rx) {
				lx += w;
				lw -= w;
			}
			rx += w;
			rw -= w;
			break;
		case BAR_ALIGN_RIGHT_RIGHT:
			bar->x[r] = rx + rw - w;
			if (lx == rx)
				lw -= w;
			rw -= w;
			break;
		case BAR_ALIGN_RIGHT_CENTER:
			bar->x[r] = rx + rw / 2 - w / 2;
			if (lx == rx) {
				lw = lx + lw - bar->x[r] + w;
				lx = bar->x[r] + w;
			}
			rw = bar->x[r] - rx;
			break;
		}
		bar->w[r] = w;
		darg.x = bar->x[r];
		darg.y = bar->borderpx;
		darg.h = bar->bh - 2 * bar->borderpx;
		darg.w = bar->w[r];
		if (br->drawfunc)
			total_drawn += br->drawfunc(bar, &darg);
	}

	if (total_drawn == 0 && bar->showbar) {
		bar->showbar = 0;
		updatebarpos(bar->mon);
		XMoveResizeWindow(dpy, bar->win, bar->bx, bar->by, bar->bw, bar->bh);
		arrange(bar->mon);
	}
	else if (total_drawn > 0 && !bar->showbar) {
		bar->showbar = 1;
		updatebarpos(bar->mon);
		XMoveResizeWindow(dpy, bar->win, bar->bx, bar->by, bar->bw, bar->bh);
		drw_map(drw, bar->win, 0, 0, bar->bw, bar->bh);
		arrange(bar->mon);
	} else
		drw_map(drw, bar->win, 0, 0, bar->bw, bar->bh);
}

void
enternotify(XEvent *e)
{
	Client *c, *sel;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		sel = selmon->sel;
		selmon = m;
		unfocus(sel, 1, c);
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0, c);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		#if !BAR_FLEXWINTITLE_PATCH
		if (c->isfloating)
			XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColFloat].pixel);
		else
			XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		#endif // BAR_FLEXWINTITLE_PATCH
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;
	Client *sel;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	sel = selmon->sel;
	selmon = m;
	unfocus(sel, 0, NULL);
	focus(NULL);
	#if WARP_PATCH
	warp(selmon->sel);
	#endif // WARP_PATCH
}

#if !STACKER_PATCH
void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel)
		return;
	#if ALWAYSFULLSCREEN_PATCH
	if (selmon->sel->isfullscreen)
		return;
	#endif // ALWAYSFULLSCREEN_PATCH
	#if BAR_WINTITLEACTIONS_PATCH
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && (!ISVISIBLE(c) || (arg->i == 1 && HIDDEN(c))); c = c->next);
		if (!c)
			for (c = selmon->clients; c && (!ISVISIBLE(c) || (arg->i == 1 && HIDDEN(c))); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i) && !(arg->i == -1 && HIDDEN(i)))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i) && !(arg->i == -1 && HIDDEN(i)))
					c = i;
	}
	#else
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	#endif // BAR_WINTITLEACTIONS_PATCH
	if (c) {
		focus(c);
		restack(selmon);
	}
}
#endif // STACKER_PATCH

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	#if BAR_SYSTRAY_PATCH
	/* FIXME getatomprop should return the number of items and a pointer to
	 * the stored data instead of this workaround */
	Atom req = XA_ATOM;
	if (prop == xatom[XembedInfo])
		req = xatom[XembedInfo];

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		if (da == xatom[XembedInfo] && dl == 2)
			atom = ((Atom *)p)[1];
		XFree(p);
	}
	#else
	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	#endif // BAR_SYSTRAY_PATCH
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin
			#if NO_MOD_BUTTONS_PATCH
				&& (nomodbuttons || buttons[i].mask != 0)
			#endif // NO_MOD_BUTTONS_PATCH
			)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
#if KEYMODES_PATCH
grabdefkeys(void)
#else
grabkeys(void)
#endif // KEYMODES_PATCH
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for (i = 0; i < LENGTH(keys); i++)
			if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						True, GrabModeAsync, GrabModeAsync);
	}
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
#if KEYMODES_PATCH
keydefpress(XEvent *e)
#else
keypress(XEvent *e)
#endif // KEYMODES_PATCH
{
	unsigned int i;
	int keysyms_return;
	KeySym* keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XGetKeyboardMapping(dpy, (KeyCode)ev->keycode, 1, &keysyms_return);
	for (i = 0; i < LENGTH(keys); i++)
		if (*keysym == keys[i].keysym
				&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
				&& keys[i].func)
			keys[i].func(&(keys[i].arg));
	XFree(keysym);
}

void
killclient(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c || ISPERMANENT(c))
		return;
	#if BAR_SYSTRAY_PATCH
	if (!sendevent(c->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0, 0, 0)) {
	#else
	if (!sendevent(c, wmatom[WMDelete])) {
	#endif // BAR_SYSTRAY_PATCH
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, c->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	#if SWAPFOCUS_PATCH
	c->mon->pertag->prevclient[c->mon->pertag->curtag] = NULL;
	#endif // SWAPFOCUS_PATCH
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	#if SWALLOW_PATCH
	Client *term = NULL;
	#endif // SWALLOW_PATCH
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	#if SWALLOW_PATCH
	c->pid = winpid(w);
	#endif // SWALLOW_PATCH
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;
	#if CFACTS_PATCH
	c->cfact = 1.0;
	#endif // CFACTS_PATCH

	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
		#if FLOATPOS_PATCH
		#if SETBORDERPX_PATCH
		c->bw = c->mon->borderpx;
		#else
		c->bw = borderpx;
		#endif // SETBORDERPX_PATCH
		#endif // FLOATPOS_PATCH
		#if CENTER_TRANSIENT_WINDOWS_BY_PARENT_PATCH
		c->x = t->x + WIDTH(t) / 2 - WIDTH(c) / 2;
		c->y = t->y + HEIGHT(t) / 2 - HEIGHT(c) / 2;
		#elif CENTER_TRANSIENT_WINDOWS_PATCH
		c->x = c->mon->wx + (c->mon->ww - WIDTH(c)) / 2;
		c->y = c->mon->wy + (c->mon->wh - HEIGHT(c)) / 2;
		#endif // CENTER_TRANSIENT_WINDOWS_PATCH | CENTER_TRANSIENT_WINDOWS_BY_PARENT_PATCH
	} else {
		c->mon = selmon;
		#if FLOATPOS_PATCH
		#if SETBORDERPX_PATCH
		c->bw = c->mon->borderpx;
		#else
		c->bw = borderpx;
		#endif // SETBORDERPX_PATCH
		#endif // FLOATPOS_PATCH
		applyrules(c);
		#if SWALLOW_PATCH
		term = termforwin(c);
		if (term)
			c->mon = term->mon;
		#endif // SWALLOW_PATCH
	}

	if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->mx);
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = MAX(c->y, ((c->mon->bar->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
		&& (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);
	#if !FLOATPOS_PATCH
	#if SETBORDERPX_PATCH
	c->bw = c->mon->borderpx;
	#else
	c->bw = borderpx;
	#endif // SETBORDERPX_PATCH
	#endif // FLOATPOS_PATCH

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	#if !BAR_FLEXWINTITLE_PATCH
	if (c->isfloating)
		XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColFloat].pixel);
	else
		XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	#endif // BAR_FLEXWINTITLE_PATCH
	configure(c); /* propagates border_width, if size doesn't change */
	#if !FLOATPOS_PATCH
	updatesizehints(c);
	#endif // FLOATPOS_PATCH
	if (getatomprop(c, netatom[NetWMState]) == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	updatewmhints(c);
	#if DECORATION_HINTS_PATCH
	updatemotifhints(c);
	#endif // DECORATION_HINTS_PATCH
	#if CENTER_PATCH
	if (ISCENTERED(c)) {
		c->x = c->mon->wx + (c->mon->ww - WIDTH(c)) / 2;
		c->y = c->mon->wy + (c->mon->wh - HEIGHT(c)) / 2;
	}
	#endif // CENTER_PATCH
	#if SAVEFLOATS_PATCH
	c->sfx = -9999;
	c->sfy = -9999;
	c->sfw = c->w;
	c->sfh = c->h;
	#endif // SAVEFLOATS_PATCH

	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);

	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating) {
		XRaiseWindow(dpy, c->win);
		XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColFloat].pixel);
	}
	#if ATTACHABOVE_PATCH || ATTACHASIDE_PATCH || ATTACHBELOW_PATCH || ATTACHBOTTOM_PATCH
	attachx(c);
	#else
	attach(c);
	#endif
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */

	#if BAR_WINTITLEACTIONS_PATCH
	if (!HIDDEN(c))
		setclientstate(c, NormalState);
	#else
	setclientstate(c, NormalState);
	#endif // BAR_WINTITLEACTIONS_PATCH
	if (c->mon == selmon)
		unfocus(selmon->sel, 0, c);
	c->mon->sel = c;
	#if BAR_WINTITLEACTIONS_PATCH
	if (!HIDDEN(c))
		XMapWindow(dpy, c->win);
	#else
	XMapWindow(dpy, c->win);
	#endif // BAR_WINTITLEACTIONS_PATCH
	#if SWALLOW_PATCH
	if (!(term && swallow(term, c)))
		arrange(c->mon);
	#else
	arrange(c->mon);
	#endif // SWALLOW_PATCH
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	#if BAR_SYSTRAY_PATCH
	Client *i;
	if (showsystray && systray && (i = wintosystrayicon(ev->window))) {
		sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
		drawbarwin(systray->bar);
	}
	#endif // BAR_SYSTRAY_PATCH

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	#if BAR_ANYBAR_PATCH
	if (wmclasscontains(ev->window, altbarclass, ""))
		managealtbar(ev->window, &wa);
	else
	#endif // BAR_ANYBAR_PATCH
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	Client *sel;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		sel = selmon->sel;
		selmon = m;
		unfocus(sel, 1, NULL);
		focus(NULL);
	}
	mon = m;
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	ignoreconfigurerequests = 1;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating) {
			#if SAVEFLOATS_PATCH
				resize(c, nx, ny, c->w, c->h, 1);
				/* save last known float coordinates */
				c->sfx = nx;
				c->sfy = ny;
			#else
				resize(c, nx, ny, c->w, c->h, 1);
			#endif // SAVEFLOATS_PATCH
			}
			#if ROUNDED_CORNERS_PATCH
			drawroundedcorners(c);
			#endif // ROUNDED_CORNERS_PATCH
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		#if SCRATCHPADS_PATCH
		if (c->tags & SPTAGMASK) {
			c->mon->tagset[c->mon->seltags] ^= (c->tags & SPTAGMASK);
			m->tagset[m->seltags] |= (c->tags & SPTAGMASK);
		}
		#endif // SCRATCHPADS_PATCH
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
	#if ROUNDED_CORNERS_PATCH
	drawroundedcorners(c);
	#endif // ROUNDED_CORNERS_PATCH
	ignoreconfigurerequests = 0;
}

Client *
nexttiled(Client *c)
{
	#if BAR_WINTITLEACTIONS_PATCH
	for (; c && (c->isfloating || !ISVISIBLE(c) || HIDDEN(c)); c = c->next);
	#else
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	#endif // BAR_WINTITLEACTIONS_PATCH
	return c;
}

#if !ZOOMSWAP_PATCH || TAGINTOSTACK_ALLMASTER_PATCH || TAGINTOSTACK_ONEMASTER_PATCH
void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}
#endif // !ZOOMSWAP_PATCH / TAGINTOSTACK_ALLMASTER_PATCH / TAGINTOSTACK_ONEMASTER_PATCH

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	#if BAR_SYSTRAY_PATCH
	if (showsystray && (c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		drawbarwin(systray->bar);
	}
	#endif // BAR_SYSTRAY_PATCH

	if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
		updatestatus();
	} else if (ev->state == PropertyDelete) {
		return; /* ignore */
	} else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			if (c->isurgent)
				drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		#if DECORATION_HINTS_PATCH
		if (ev->atom == motifatom)
			updatemotifhints(c);
		#endif // DECORATION_HINTS_PATCH
	}
}

void
quit(const Arg *arg)
{
	#if COOL_AUTOSTART_PATCH
	size_t i;
	#endif // COOL_AUTOSTART_PATCH
	#if ONLYQUITONEMPTY_PATCH
	unsigned int n;
	Window *junk = malloc(1);

	XQueryTree(dpy, root, junk, junk, &junk, &n);

	#if COOL_AUTOSTART_PATCH
	if (n - autostart_len <= quit_empty_window_count)
	#else
	if (n <= quit_empty_window_count)
	#endif // COOL_AUTOSTART_PATCH
	{
		#if RESTARTSIG_PATCH
		if (arg->i)
			restart = 1;
		#endif // RESTARTSIG_PATCH
		running = 0;
	}
	else
		printf("[dwm] not exiting (n=%d)\n", n);

	free(junk);
	#else
	#if RESTARTSIG_PATCH
	if (arg->i)
		restart = 1;
	#endif // RESTARTSIG_PATCH
	running = 0;
	#endif // ONLYQUITONEMPTY_PATCH

	#if COOL_AUTOSTART_PATCH
	/* kill child processes */
	for (i = 0; i < autostart_len; i++) {
		if (0 < autostart_pids[i]) {
			kill(autostart_pids[i], SIGTERM);
			waitpid(autostart_pids[i], NULL, 0);
		}
	}
	#endif // COOL_AUTOSTART_PATCH
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	#if NOBORDER_PATCH
	if (((nexttiled(c->mon->clients) == c && !nexttiled(c->next)))
		&& (c->fakefullscreen == 1 || !c->isfullscreen)
		&& !c->isfloating
		&& c->mon->lt[c->mon->sellt]->arrange) {
		c->w = wc.width += c->bw * 2;
		c->h = wc.height += c->bw * 2;
		wc.border_width = 0;
	}
	#endif // NOBORDER_PATCH
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	if (c->fakefullscreen == 1)
		/* Exception: if the client was in actual fullscreen and we exit out to fake fullscreen
		 * mode, then the focus would drift to whichever window is under the mouse cursor at the
		 * time. To avoid this we pass True to XSync which will make the X server disregard any
		 * other events in the queue thus cancelling the EnterNotify event that would otherwise
		 * have changed focus. */
		XSync(dpy, True);
	else
		XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	int opx, opy, och, ocw, nx, ny;
	int horizcorner, vertcorner;
	unsigned int dui;
	Window dummy;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	och = c->h;
	ocw = c->w;
	if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &opx, &opy, &nx, &ny, &dui))
		return;
	horizcorner = nx < c->w / 2;
	vertcorner  = ny < c->h / 2;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[horizcorner | (vertcorner << 1)]->cursor, CurrentTime) != GrabSuccess)
		return;
	ignoreconfigurerequests = 1;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = horizcorner ? (ocx + ev.xmotion.x - opx) : c->x;
			ny = vertcorner ? (ocy + ev.xmotion.y - opy) : c->y;
			nw = MAX(horizcorner ? (ocx + ocw - nx) : (ocw + (ev.xmotion.x - opx)), 1);
			nh = MAX(vertcorner ? (ocy + och - ny) : (och + (ev.xmotion.y - opy)), 1);

			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating) {
				resizeclient(c, nx, ny, nw, nh);
				#if SAVEFLOATS_PATCH
				/* save last known float dimensions */
				c->sfx = nx;
				c->sfy = ny;
				c->sfw = nw;
				c->sfh = nh;
				#endif // SAVEFLOATS_PATCH
				#if ROUNDED_CORNERS_PATCH
				drawroundedcorners(c);
				#endif // ROUNDED_CORNERS_PATCH
			}
			break;
		}
	} while (ev.type != ButtonRelease);

	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		#if SCRATCHPADS_PATCH
		if (c->tags & SPTAGMASK) {
			c->mon->tagset[c->mon->seltags] ^= (c->tags & SPTAGMASK);
			m->tagset[m->seltags] |= (c->tags & SPTAGMASK);
		}
		#endif // SCRATCHPADS_PATCH
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
	ignoreconfigurerequests = 0;
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;
	#if WARP_PATCH && FLEXTILE_DELUXE_LAYOUT
	int n;
	#endif // WARP_PATCH

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->bar->win;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	#if WARP_PATCH
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (m == selmon && (m->tagset[m->seltags] & m->sel->tags) && (
		!(m->ltaxis[MASTER] == MONOCLE && (abs(m->ltaxis[LAYOUT] == NO_SPLIT || !m->nmaster || n <= m->nmaster)))
		|| m->sel->isfloating)
	)
		warp(m->sel);
	#endif // WARP_PATCH
}

#if IPC_PATCH
void
run(void)
{
	int event_count = 0;
	const int MAX_EVENTS = 10;
	struct epoll_event events[MAX_EVENTS];

	XSync(dpy, False);

	/* main event loop */
	while (running) {
		event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

		for (int i = 0; i < event_count; i++) {
			int event_fd = events[i].data.fd;
			DEBUG("Got event from fd %d\n", event_fd);

			if (event_fd == dpy_fd) {
				// -1 means EPOLLHUP
				if (handlexevent(events + i) == -1)
					return;
			} else if (event_fd == ipc_get_sock_fd()) {
				ipc_handle_socket_epoll_event(events + i);
			} else if (ipc_is_client_registered(event_fd)) {
				if (ipc_handle_client_epoll_event(events + i, mons, &lastselmon, selmon,
						NUMTAGS, layouts, LENGTH(layouts)) < 0) {
					fprintf(stderr, "Error handling IPC event on fd %d\n", event_fd);
				}
			} else {
				fprintf(stderr, "Got event from unknown fd %d, ptr %p, u32 %d, u64 %lu",
				event_fd, events[i].data.ptr, events[i].data.u32,
				events[i].data.u64);
				fprintf(stderr, " with events %d\n", events[i].events);
				return;
			}
		}
	}
}
#else
void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}
#endif // IPC_PATCH

void
scan(void)
{
	#if SWALLOW_PATCH
	scanner = 1;
	char swin[256];
	#endif // SWALLOW_PATCH
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			#if BAR_ANYBAR_PATCH
			if (wmclasscontains(wins[i], altbarclass, ""))
				managealtbar(wins[i], &wa);
			else
			#endif // BAR_ANYBAR_PATCH
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
			#if SWALLOW_PATCH
			else if (gettextprop(wins[i], netatom[NetClientList], swin, sizeof swin))
				manage(wins[i], &wa);
			#endif // SWALLOW_PATCH
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		XFree(wins);
	}
	#if SWALLOW_PATCH
	scanner = 0;
	#endif // SWALLOW_PATCH
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	#if SENDMON_KEEPFOCUS_PATCH
	int hadfocus = (c == selmon->sel);
	#endif // SENDMON_KEEPFOCUS_PATCH
	unfocus(c, 1, NULL);
	detach(c);
	detachstack(c);
	#if SENDMON_KEEPFOCUS_PATCH
	arrange(c->mon);
	#endif // SENDMON_KEEPFOCUS_PATCH
	c->mon = m;
	#if SCRATCHPADS_PATCH
	if (!(c->tags & SPTAGMASK))
	#endif // SCRATCHPADS_PATCH
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	#if ATTACHABOVE_PATCH || ATTACHASIDE_PATCH || ATTACHBELOW_PATCH || ATTACHBOTTOM_PATCH
	attachx(c);
	#else
	attach(c);
	#endif
	attachstack(c);
	#if SENDMON_KEEPFOCUS_PATCH
	arrange(m);
	if (hadfocus) {
		focus(c);
		restack(m);
	} else
		focus(NULL);
	#else
	focus(NULL);
	arrange(NULL);
	#endif // SENDMON_KEEPFOCUS_PATCH
	#if SWITCHTAG_PATCH
	if (c->switchtag)
		c->switchtag = 0;
	#endif // SWITCHTAG_PATCH
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
#if BAR_SYSTRAY_PATCH
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
#else
sendevent(Client *c, Atom proto)
#endif // BAR_SYSTRAY_PATCH
{
	int n;
	Atom *protocols;
	#if BAR_SYSTRAY_PATCH
	Atom mt;
	#endif // BAR_SYSTRAY_PATCH
	int exists = 0;
	XEvent ev;

	#if BAR_SYSTRAY_PATCH
	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	} else {
		exists = True;
		mt = proto;
	}
	#else
	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	#endif // BAR_SYSTRAY_PATCH

	if (exists) {
		#if BAR_SYSTRAY_PATCH
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
		#else
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
		#endif // BAR_SYSTRAY_PATCH
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	#if BAR_SYSTRAY_PATCH
	sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
	#else
	sendevent(c, wmatom[WMTakeFocus]);
	#endif // BAR_SYSTRAY_PATCH
}

void
setfullscreen(Client *c, int fullscreen)
{
	int savestate = 0, restorestate = 0;

	if ((c->fakefullscreen == 0 && fullscreen && !c->isfullscreen) // normal fullscreen
			|| (c->fakefullscreen == 2 && fullscreen)) // fake fullscreen --> actual fullscreen
		savestate = 1; // go actual fullscreen
	else if ((c->fakefullscreen == 0 && !fullscreen && c->isfullscreen) // normal fullscreen exit
			|| (c->fakefullscreen >= 2 && !fullscreen)) // fullscreen exit --> fake fullscreen
		restorestate = 1; // go back into tiled

	/* If leaving fullscreen and the window was previously fake fullscreen (2), then restore
	 * that while staying in fullscreen. The exception to this is if we are in said state, but
	 * the client itself disables fullscreen (3) then we let the client go out of fullscreen
	 * while keeping fake fullscreen enabled (as otherwise there will be a mismatch between the
	 * client and the window manager's perception of the client's fullscreen state). */
	if (c->fakefullscreen == 2 && !fullscreen && c->isfullscreen) {
		c->fakefullscreen = 1;
		c->isfullscreen = 1;
		fullscreen = 1;
	} else if (c->fakefullscreen == 3) // client exiting actual fullscreen
		c->fakefullscreen = 1;

	if (fullscreen != c->isfullscreen) { // only send property change if necessary
		if (fullscreen)
			XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
				PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		else
			XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
				PropModeReplace, (unsigned char*)0, 0);
	}

	c->isfullscreen = fullscreen;

	/* Some clients, e.g. firefox, will send a client message informing the window manager
	 * that it is going into fullscreen after receiving the above signal. This has the side
	 * effect of this function (setfullscreen) sometimes being called twice when toggling
	 * fullscreen on and off via the window manager as opposed to the application itself.
	 * To protect against obscure issues where the client settings are stored or restored
	 * when they are not supposed to we add an additional bit-lock on the old state so that
	 * settings can only be stored and restored in that precise order. */
	if (savestate && !(c->oldstate & (1 << 1))) {
		c->oldbw = c->bw;
		c->oldstate = c->isfloating | (1 << 1);
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (restorestate && (c->oldstate & (1 << 1))) {
		c->bw = c->oldbw;
		c->isfloating = c->oldstate = c->oldstate & 1;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		restack(c->mon);
	}
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) {
		selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	}
	if (arg && arg->v)
		selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];

	#if FLEXTILE_DELUXE_LAYOUT
	if (selmon->lt[selmon->sellt]->preset.nmaster && selmon->lt[selmon->sellt]->preset.nmaster != -1)
		selmon->nmaster = selmon->lt[selmon->sellt]->preset.nmaster;
	if (selmon->lt[selmon->sellt]->preset.nstack && selmon->lt[selmon->sellt]->preset.nstack != -1)
		selmon->nstack = selmon->lt[selmon->sellt]->preset.nstack;

	selmon->ltaxis[LAYOUT] = selmon->lt[selmon->sellt]->preset.layout;
	selmon->ltaxis[MASTER] = selmon->lt[selmon->sellt]->preset.masteraxis;
	selmon->ltaxis[STACK]  = selmon->lt[selmon->sellt]->preset.stack1axis;
	selmon->ltaxis[STACK2] = selmon->lt[selmon->sellt]->preset.stack2axis;

	selmon->pertag->ltaxis[selmon->pertag->curtag][LAYOUT] = selmon->ltaxis[LAYOUT];
	selmon->pertag->ltaxis[selmon->pertag->curtag][MASTER] = selmon->ltaxis[MASTER];
	selmon->pertag->ltaxis[selmon->pertag->curtag][STACK]  = selmon->ltaxis[STACK];
	selmon->pertag->ltaxis[selmon->pertag->curtag][STACK2] = selmon->ltaxis[STACK2];
	#endif // FLEXTILE_DELUXE_LAYOUT
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;

	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	arrange(selmon);
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;

	/* clean up any zombies immediately */
	sigchld(0);

	#if RESTARTSIG_PATCH
	signal(SIGHUP, sighup);
	signal(SIGTERM, sigterm);
	#endif // RESTARTSIG_PATCH

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	#if BAR_ALPHA_PATCH
	xinitvisual();
	drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
	#else
	drw = drw_create(dpy, screen, root, sw, sh);
	#endif // BAR_ALPHA_PATCH
	#if BAR_PANGO_PATCH
	if (!drw_font_create(drw, font))
	#else
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
	#endif // BAR_PANGO_PATCH
		die("no fonts could be loaded.");
	#if BAR_STATUSPADDING_PATCH
	lrpad = drw->fonts->h + horizpadbar;
	bh = drw->fonts->h + vertpadbar;
	#else
	lrpad = drw->fonts->h;
	#if BAR_HEIGHT_PATCH
	bh = bar_height ? bar_height : drw->fonts->h + 2;
	#else
	bh = drw->fonts->h + 2;
	#endif // BAR_HEIGHT_PATCH
	#endif // BAR_STATUSPADDING_PATCH
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	#if WINDOWROLERULE_PATCH
	wmatom[WMWindowRole] = XInternAtom(dpy, "WM_WINDOW_ROLE", False);
	#endif // WINDOWROLERULE_PATCH
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	#if BAR_SYSTRAY_PATCH
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
	netatom[NetSystemTrayVisual] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	#endif // BAR_SYSTRAY_PATCH
	#if BAR_EWMHTAGS_PATCH
	netatom[NetDesktopViewport] = XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	#endif // BAR_EWMHTAGS_PATCH
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	#if DECORATION_HINTS_PATCH
	motifatom = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	#endif // DECORATION_HINTS_PATCH
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurResizeBR] = drw_cur_create(drw, XC_bottom_right_corner);
	cursor[CurResizeBL] = drw_cur_create(drw, XC_bottom_left_corner);
	cursor[CurResizeTR] = drw_cur_create(drw, XC_top_right_corner);
	cursor[CurResizeTL] = drw_cur_create(drw, XC_top_left_corner);
	cursor[CurResizeHorzArrow] = drw_cur_create(drw, XC_sb_h_double_arrow);
	cursor[CurResizeVertArrow] = drw_cur_create(drw, XC_sb_v_double_arrow);
	cursor[CurIronCross] = drw_cur_create(drw, XC_iron_cross);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	#if BAR_VTCOLORS_PATCH
	get_vt_colors();
	if (get_luminance(colors[SchemeTagsNorm][ColBg]) > 50) {
		strcpy(colors[SchemeTitleNorm][ColBg], title_bg_light);
		strcpy(colors[SchemeTitleSel][ColBg], title_bg_light);
	} else {
		strcpy(colors[SchemeTitleNorm][ColBg], title_bg_dark);
		strcpy(colors[SchemeTitleSel][ColBg], title_bg_dark);
	}
	#endif // BAR_VTCOLORS_PATCH
	#if BAR_STATUS2D_PATCH && !BAR_STATUSCOLORS_PATCH
	scheme = ecalloc(LENGTH(colors) + 1, sizeof(Clr *));
	#if BAR_ALPHA_PATCH
	scheme[LENGTH(colors)] = drw_scm_create(drw, colors[0], alphas[0], ColCount);
	#else
	scheme[LENGTH(colors)] = drw_scm_create(drw, colors[0], ColCount);
	#endif // BAR_ALPHA_PATCH
	#else
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	#endif // BAR_STATUS2D_PATCH
	for (i = 0; i < LENGTH(colors); i++)
		#if BAR_ALPHA_PATCH
		scheme[i] = drw_scm_create(drw, colors[i], alphas[i], ColCount);
		#else
		scheme[i] = drw_scm_create(drw, colors[i], ColCount);
		#endif // BAR_ALPHA_PATCH
	#if BAR_POWERLINE_STATUS_PATCH
	statusscheme = ecalloc(LENGTH(statuscolors), sizeof(Clr *));
	for (i = 0; i < LENGTH(statuscolors); i++)
		#if BAR_ALPHA_PATCH
		statusscheme[i] = drw_scm_create(drw, statuscolors[i], alphas[0], ColCount);
		#else
		statusscheme[i] = drw_scm_create(drw, statuscolors[i], ColCount);
		#endif // BAR_ALPHA_PATCH
	#endif // BAR_POWERLINE_STATUS_PATCH

	updatebars();
	updatestatus();

	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dawn", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	#if BAR_EWMHTAGS_PATCH
	setnumdesktops();
	setcurrentdesktop();
	setdesktopnames();
	setviewport();
	#endif // BAR_EWMHTAGS_PATCH
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
	#if IPC_PATCH
	setupepoll();
	#endif // IPC_PATCH
	#if BAR_ANYBAR_PATCH
	if (usealtbar)
		spawnbar();
	#endif // BAR_ANYBAR_PATCH
}


void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		#if SCRATCHPADS_PATCH && SCRATCHPADS_KEEP_POSITION_AND_SIZE_PATCH
		if (
			(c->tags & SPTAGMASK) &&
			c->isfloating &&
			(
				c->x < c->mon->mx ||
				c->x > c->mon->mx + c->mon->mw ||
				c->y < c->mon->my ||
				c->y > c->mon->my + c->mon->mh
			)
		) {
			c->x = c->mon->wx + (c->mon->ww / 2 - WIDTH(c) / 2);
			c->y = c->mon->wy + (c->mon->wh / 2 - HEIGHT(c) / 2);
		}
		#elif SCRATCHPADS_PATCH
		if ((c->tags & SPTAGMASK) && c->isfloating) {
			c->x = c->mon->wx + (c->mon->ww / 2 - WIDTH(c) / 2);
			c->y = c->mon->wy + (c->mon->wh / 2 - HEIGHT(c) / 2);
		}
		#endif // SCRATCHPADS_KEEP_POSITION_AND_SIZE_PATCH | SCRATCHPADS_PATCH
		/* show clients top down */
		#if SAVEFLOATS_PATCH
		if (!c->mon->lt[c->mon->sellt]->arrange && c->sfx != -9999 && !c->isfullscreen) {
			XMoveWindow(dpy, c->win, c->sfx, c->sfy);
			resize(c, c->sfx, c->sfy, c->sfw, c->sfh, 0);
			showhide(c->snext);
			return;
		}
		#endif // SAVEFLOATS_PATCH
		#if AUTORESIZE_PATCH
		if (c->needresize) {
			c->needresize = 0;
			XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else {
			XMoveWindow(dpy, c->win, c->x, c->y);
		}
		#else
		XMoveWindow(dpy, c->win, c->x, c->y);
		#endif // AUTORESIZE_PATCH
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sigchld(int unused)
{
	#if COOL_AUTOSTART_PATCH
	pid_t pid;
	#endif // COOL_AUTOSTART_PATCH
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	#if COOL_AUTOSTART_PATCH
	while (0 < (pid = waitpid(-1, NULL, WNOHANG))) {
		pid_t *p, *lim;

		if (!(p = autostart_pids))
			continue;
		lim = &p[autostart_len];

		for (; p < lim; p++) {
			if (*p == pid) {
				*p = -1;
				break;
			}
		}
	}
	#else
	while (0 < waitpid(-1, NULL, WNOHANG));
	#endif // COOL_AUTOSTART_PATCH
}

void
spawn(const Arg *arg)
{
	#if BAR_STATUSCMD_PATCH && !BAR_DWMBLOCKS_PATCH
	char *cmd = NULL;
	#endif // BAR_STATUSCMD_PATCH | BAR_DWMBLOCKS_PATCH
	#if !NODMENU_PATCH
	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	#endif // NODMENU_PATCH
	#if BAR_STATUSCMD_PATCH && !BAR_DWMBLOCKS_PATCH
	#if !NODMENU_PATCH
	else if (arg->v == statuscmd)
	#else
	if (arg->v == statuscmd)
	#endif // NODMENU_PATCH
	{
		int len = strlen(statuscmds[statuscmdn]) + 1;
		if (!(cmd = malloc(sizeof(char)*len + sizeof(statusexport))))
			die("malloc:");
		strcpy(cmd, statusexport);
		strcat(cmd, statuscmds[statuscmdn]);
		cmd[LENGTH(statusexport)-3] = '0' + lastbutton;
		statuscmd[2] = cmd;
	}
	#endif // BAR_STATUSCMD_PATCH | BAR_DWMBLOCKS_PATCH

	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		#if SPAWNCMD_PATCH
		if (selmon->sel) {
			const char* const home = getenv("HOME");
			assert(home && strchr(home, '/'));
			const size_t homelen = strlen(home);
			char *cwd, *pathbuf = NULL;
			struct stat statbuf;

			cwd = strtok(selmon->sel->name, SPAWN_CWD_DELIM);
			/* NOTE: strtok() alters selmon->sel->name in-place,
			 * but that does not matter because we are going to
			 * exec() below anyway; nothing else will use it */
			while (cwd) {
				if (*cwd == '~') { /* replace ~ with $HOME */
					if (!(pathbuf = malloc(homelen + strlen(cwd)))) /* ~ counts for NULL term */
						die("fatal: could not malloc() %u bytes\n", homelen + strlen(cwd));
					strcpy(strcpy(pathbuf, home) + homelen, cwd + 1);
					cwd = pathbuf;
				}

				if (strchr(cwd, '/') && !stat(cwd, &statbuf)) {
					if (!S_ISDIR(statbuf.st_mode))
						cwd = dirname(cwd);

					if (!chdir(cwd))
						break;
				}

				cwd = strtok(NULL, SPAWN_CWD_DELIM);
			}

			free(pathbuf);
		}
		#endif // SPAWNCMD_PATCH
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
	#if BAR_STATUSCMD_PATCH && !BAR_DWMBLOCKS_PATCH
	free(cmd);
	#endif // BAR_STATUSCMD_PATCH | BAR_DWMBLOCKS_PATCH
}

void
tag(const Arg *arg)
{
	#if SWAPFOCUS_PATCH
	unsigned int tagmask, tagindex;
	#endif // SWAPFOCUS_PATCH

	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		#if SWITCHTAG_PATCH
		if (selmon->sel->switchtag)
			selmon->sel->switchtag = 0;
		#endif // SWITCHTAG_PATCH
		focus(NULL);
		#if SWAPFOCUS_PATCH
		selmon->pertag->prevclient[selmon->pertag->curtag] = NULL;
		for (tagmask = arg->ui & TAGMASK, tagindex = 1; tagmask!=0; tagmask >>= 1, tagindex++)
			if (tagmask & 1)
				selmon->pertag->prevclient[tagindex] = NULL;
		#endif // SWAPFOCUS_PATCH
		arrange(selmon);
		#if VIEWONTAG_PATCH
		if ((arg->ui & TAGMASK) != selmon->tagset[selmon->seltags])
			view(arg);
		#endif // VIEWONTAG_PATCH
	}
}

void
tagmon(const Arg *arg)
{
	#if TAGMONFIXFS_PATCH
	Client *c = selmon->sel;
	if (!c || !mons->next)
		return;
	if (c->isfullscreen) {
		c->isfullscreen = 0;
		sendmon(c, dirtomon(arg->i));
		c->isfullscreen = 1;
		if (c->fakefullscreen != 1) {
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
			XRaiseWindow(dpy, c->win);
		}
	} else
		sendmon(c, dirtomon(arg->i));
	#else
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
	#endif // TAGMONFIXFS_PATCH
}

void
togglebar(const Arg *arg)
{
	Bar *bar;
	#if BAR_HOLDBAR_PATCH && PERTAGBAR_PATCH
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = (selmon->showbar == 2 ? 1 : !selmon->showbar);
	#elif BAR_HOLDBAR_PATCH
	selmon->showbar = (selmon->showbar == 2 ? 1 : !selmon->showbar);
	#elif PERTAGBAR_PATCH
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
	#else
	selmon->showbar = !selmon->showbar;
	#endif // BAR_HOLDBAR_PATCH
	updatebarpos(selmon);
	for (bar = selmon->bar; bar; bar = bar->next)
		XMoveResizeWindow(dpy, bar->win, bar->bx, bar->by, bar->bw, bar->bh);
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	Client *c = selmon->sel;
	if (arg && arg->v)
		c = (Client*)arg->v;
	if (!c)
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support for fullscreen windows */
		return;
	c->isfloating = !c->isfloating || c->isfixed;
	#if !BAR_FLEXWINTITLE_PATCH
	if (c->isfloating)
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColFloat].pixel);
	else
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
	#endif // BAR_FLEXWINTITLE_PATCH
	if (c->isfloating) {
		#if SAVEFLOATS_PATCH
		if (c->sfx != -9999) {
			/* restore last known float dimensions */
			resize(c, c->sfx, c->sfy, c->sfw, c->sfh, 0);
			arrange(c->mon);
			return;
		}
		#endif // SAVEFLOATS_PATCH
		resize(c, c->x, c->y, c->w, c->h, 0);
	#if SAVEFLOATS_PATCH
	} else {
		/* save last known float dimensions */
		c->sfx = c->x;
		c->sfy = c->y;
		c->sfw = c->w;
		c->sfh = c->h;
	#endif // SAVEFLOATS_PATCH
	}
	arrange(c->mon);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;
	#if SWAPFOCUS_PATCH
	unsigned int tagmask, tagindex;
	#endif // SWAPFOCUS_PATCH

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		#if SWAPFOCUS_PATCH
		for (tagmask = arg->ui & TAGMASK, tagindex = 1; tagmask!=0; tagmask >>= 1, tagindex++)
			if (tagmask & 1)
				selmon->pertag->prevclient[tagindex] = NULL;
		#endif // SWAPFOCUS_PATCH
		arrange(selmon);
	}
	#if BAR_EWMHTAGS_PATCH
	updatecurrentdesktop();
	#endif // BAR_EWMHTAGS_PATCH
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	int i;

	#if TAGINTOSTACK_ALLMASTER_PATCH
	Client *const selected = selmon->sel;

	// clients in the master area should be the same after we add a new tag
	Client **const masters = calloc(selmon->nmaster, sizeof(Client *));
	if (!masters) {
		die("fatal: could not calloc() %u bytes \n", selmon->nmaster * sizeof(Client *));
	}
	// collect (from last to first) references to all clients in the master area
	Client *c;
	size_t j;
	for (c = nexttiled(selmon->clients), j = 0; c && j < selmon->nmaster; c = nexttiled(c->next), ++j)
		masters[selmon->nmaster - (j + 1)] = c;
	// put the master clients at the front of the list
	// > go from the 'last' master to the 'first'
	for (j = 0; j < selmon->nmaster; ++j)
		if (masters[j])
			pop(masters[j]);
	free(masters);

	// we also want to be sure not to mutate the focus
	focus(selected);
	#elif TAGINTOSTACK_ONEMASTER_PATCH
	// the first visible client should be the same after we add a new tag
	// we also want to be sure not to mutate the focus
	Client *const c = nexttiled(selmon->clients);
	if (c) {
		Client * const selected = selmon->sel;
		pop(c);
		focus(selected);
	}
	#endif // TAGINTOSTACK_ALLMASTER_PATCH / TAGINTOSTACK_ONEMASTER_PATCH

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;

		if (newtagset == ~0) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			selmon->pertag->curtag = 0;
		}
		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			for (i=0; !(newtagset & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}

		/* apply settings for this view */
		selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
		selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
		selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
		#if PERTAGBAR_PATCH
		if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
			togglebar(NULL);
		#endif // PERTAGBAR_PATCH
		focus(NULL);
		arrange(selmon);
	}
	#if BAR_EWMHTAGS_PATCH
	updatecurrentdesktop();
	#endif // BAR_EWMHTAGS_PATCH
}

void
unfocus(Client *c, int setfocus, Client *nextfocus)
{
	if (!c)
		return;
	#if SWAPFOCUS_PATCH
	selmon->pertag->prevclient[selmon->pertag->curtag] = c;
	#endif // SWAPFOCUS_PATCH
	if (c->isfullscreen && ISVISIBLE(c) && c->mon == selmon && nextfocus && !nextfocus->isfloating)
		if (c->fakefullscreen != 1)
			setfullscreen(c, 0);
	grabbuttons(c, 0);
	#if !BAR_FLEXWINTITLE_PATCH
	if (c->isfloating)
		XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColFloat].pixel);
	else
		XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	#endif // BAR_FLEXWINTITLE_PATCH
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	#if SWITCHTAG_PATCH
	unsigned int switchtag = c->switchtag;
	#endif // SWITCHTAG_PATCH
	XWindowChanges wc;

	#if SWALLOW_PATCH
	if (c->swallowing) {
		unswallow(c);
		return;
	}

	Client *s = swallowingclient(c->win);
	if (s) {
		free(s->swallowing);
		s->swallowing = NULL;
		arrange(m);
		focus(NULL);
		return;
	}
	#endif // SWALLOW_PATCH

	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	#if SWALLOW_PATCH
	if (s)
		return;
	#endif // SWALLOW_PATCH
	focus(NULL);
	updateclientlist();
	arrange(m);
	#if SWITCHTAG_PATCH
	if (switchtag && ((switchtag & TAGMASK) != selmon->tagset[selmon->seltags]))
		view(&((Arg) { .ui = switchtag }));
	#endif // SWITCHTAG_PATCH
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	#if BAR_ANYBAR_PATCH
	Monitor *m;
	Bar *bar;
	#endif // BAR_ANYBAR_PATCH
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	#if BAR_SYSTRAY_PATCH
	} else if (showsystray && (c = wintosystrayicon(ev->window))) {
		/* KLUDGE! sometimes icons occasionally unmap their windows, but do
		 * _not_ destroy them. We map those windows back */
		XMapRaised(dpy, c->win);
		removesystrayicon(c);
		drawbarwin(systray->bar);
	#endif // BAR_SYSTRAY_PATCH
	}
	#if BAR_ANYBAR_PATCH
	else {
		 m = wintomon(ev->window);
		 for (bar = m->bar; bar; bar = bar->next) {
		 	if (bar->win == ev->window) {
				unmanagealtbar(ev->window);
				break;
			}
		}
	}
	#endif // BAR_ANYBAR_PATCH
}

void
updatebars(void)
{
	Bar *bar;
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		#if BAR_ALPHA_PATCH
		.background_pixel = 0,
		.border_pixel = 0,
		.colormap = cmap,
		#else
		.background_pixmap = ParentRelative,
		#endif // BAR_ALPHA_PATCH
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		for (bar = m->bar; bar; bar = bar->next) {
			if (bar->external)
				continue;
			if (!bar->win) {
				#if BAR_ALPHA_PATCH
				bar->win = XCreateWindow(dpy, root, bar->bx, bar->by, bar->bw, bar->bh, 0, depth,
				                          InputOutput, visual,
				                          CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
				#else
				bar->win = XCreateWindow(dpy, root, bar->bx, bar->by, bar->bw, bar->bh, 0, DefaultDepth(dpy, screen),
						CopyFromParent, DefaultVisual(dpy, screen),
						CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
				#endif // BAR_ALPHA_PATCH
				XDefineCursor(dpy, bar->win, cursor[CurNormal]->cursor);
				XMapRaised(dpy, bar->win);
				XSetClassHint(dpy, bar->win, &ch);
			}
		}
	}
}

void
updatebarpos(Monitor *m)
{
	m->wx = m->mx;
	m->wy = m->my;
	m->ww = m->mw;
	m->wh = m->mh;
	Bar *bar;
	#if BAR_PADDING_PATCH
	int y_pad = vertpad;
	int x_pad = sidepad;
	#else
	int y_pad = 0;
	int x_pad = 0;
	#endif // BAR_PADDING_PATCH

	#if INSETS_PATCH
	// Custom insets
	Inset inset = m->inset;
	m->wx += inset.x;
	m->wy += inset.y;
	m->ww -= inset.w + inset.x;
	m->wh -= inset.h + inset.y;
	#endif // INSETS_PATCH

	for (bar = m->bar; bar; bar = bar->next) {
		bar->bx = m->wx + x_pad;
		bar->bw = m->ww - 2 * x_pad;
	}

	for (bar = m->bar; bar; bar = bar->next)
		if (!m->showbar || !bar->showbar)
			bar->by = -bar->bh - y_pad;
	if (!m->showbar)
		return;
	for (bar = m->bar; bar; bar = bar->next) {
		if (!bar->showbar)
			continue;
		if (bar->topbar)
			m->wy = m->wy + bar->bh + y_pad;
		m->wh -= y_pad + bar->bh;
	}
	for (bar = m->bar; bar; bar = bar->next)
		bar->by = (bar->topbar ? m->wy - bar->bh : m->wy + m->wh);
}

void
updateclientlist()
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		#if SORTSCREENS_PATCH
		sortscreens(unique, nn);
		#endif // SORTSCREENS_PATCH
		if (n <= nn) { /* new monitors available */
			for (i = 0; i < (nn - n); i++) {
				for (m = mons; m && m->next; m = m->next);
				if (m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for (i = 0, m = mons; i < nn && m; m = m->next, i++) {
				if (i >= n
				|| unique[i].x_org != m->mx || unique[i].y_org != m->my
				|| unique[i].width != m->mw || unique[i].height != m->mh)
				{
					dirty = 1;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
					updatebarpos(m);
				}
			}
		} else { /* less monitors available nn < n */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next);
				while ((c = m->clients)) {
					dirty = 1;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons;
					attach(c);
					attachstack(c);
				}
				if (m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		for (i = 0, m = mons; m; m = m->next, i++)
			m->index = i;
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		#if SIZEHINTS_PATCH || SIZEHINTS_RULED_PATCH
		size.flags = 0;
		#else
		size.flags = PSize;
		#endif // SIZEHINTS_PATCH | SIZEHINTS_RULED_PATCH
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	#if SIZEHINTS_PATCH || SIZEHINTS_RULED_PATCH
	if (size.flags & PSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
		c->isfloating = 1;
	}
	#if SIZEHINTS_RULED_PATCH
	checkfloatingrules(c);
	#endif // SIZEHINTS_RULED_PATCH
	#endif // SIZEHINTS_PATCH
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatestatus(void)
{
	Monitor *m;
	#if BAR_EXTRASTATUS_PATCH
	if (!gettextprop(root, XA_WM_NAME, rawstext, sizeof(rawstext))) {
		strcpy(stext, "dwm-"VERSION);
		estext[0] = '\0';
	} else {
		char *e = strchr(rawstext, statussep);
		if (e) {
			*e = '\0'; e++;
			#if BAR_STATUSCMD_PATCH
			strncpy(rawestext, e, sizeof(estext) - 1);
			copyvalidchars(estext, rawestext);
			#else
			strncpy(estext, e, sizeof(estext) - 1);
			#endif // BAR_STATUSCMD_PATCH
		} else {
			estext[0] = '\0';
		}
		#if BAR_STATUSCMD_PATCH
		copyvalidchars(stext, rawstext);
		#else
		strncpy(stext, rawstext, sizeof(stext) - 1);
		#endif // BAR_STATUSCMD_PATCH
	}
	#elif BAR_STATUSCMD_PATCH
	if (!gettextprop(root, XA_WM_NAME, rawstext, sizeof(rawstext)))
		strcpy(stext, "dwm-"VERSION);
	else
		copyvalidchars(stext, rawstext);
	#else
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "dwm-"VERSION);
	#endif // BAR_EXTRASTATUS_PATCH | BAR_STATUSCMD_PATCH
	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
updatetitle(Client *c)
{
	#if IPC_PATCH
	char oldname[sizeof(c->name)];
	strcpy(oldname, c->name);
	#endif // IPC_PATCH

	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);

	#if IPC_PATCH
	for (Monitor *m = mons; m; m = m->next) {
		if (m->sel == c && strcmp(oldname, c->name) != 0)
			ipc_focused_title_change_event(m->num, c->win, oldname, c->name);
	}
	#endif // IPC_PATCH
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (c->isurgent) {
			if (c->isfloating)
				XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColFloat].pixel);
			else
				XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
		}
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	if (arg->ui && (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
	{
		#if VIEW_SAME_TAG_GIVES_PREVIOUS_TAG_PATCH
		view(&((Arg) { .ui = 0 }));
		#endif // VIEW_SAME_TAG_GIVES_PREVIOUS_TAG_PATCH
		return;
    }
	selmon->seltags ^= 1; /* toggle sel tagset */
	pertagview(arg);
	#if SWAPFOCUS_PATCH
	Client *unmodified = selmon->pertag->prevclient[selmon->pertag->curtag];
	#endif // SWAPFOCUS_PATCH
	focus(NULL);
	#if SWAPFOCUS_PATCH
	selmon->pertag->prevclient[selmon->pertag->curtag] = unmodified;
	#endif // SWAPFOCUS_PATCH
	arrange(selmon);
	#if BAR_EWMHTAGS_PATCH
	updatecurrentdesktop();
	#endif // BAR_EWMHTAGS_PATCH
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;
	Bar *bar;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		for (bar = m->bar; bar; bar = bar->next)
			if (w == bar->win)
				return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;
	if (arg && arg->v)
		c = (Client*)arg->v;
	if (!c)
		return;
	#if ZOOMSWAP_PATCH
	Client *at = NULL, *cold, *cprevious = NULL, *p;
	#endif // ZOOMSWAP_PATCH

	#if ZOOMFLOATING_PATCH
	if (c && c->isfloating)
		togglefloating(&((Arg) { .v = c }));
	#endif // ZOOMFLOATING_PATCH

	#if SWAPFOCUS_PATCH
	c->mon->pertag->prevclient[c->mon->pertag->curtag] = nexttiled(c->mon->clients);
	#endif // SWAPFOCUS_PATCH

	if (!c->mon->lt[c->mon->sellt]->arrange
	|| (c && c->isfloating)
	#if ZOOMSWAP_PATCH
	|| !c
	#endif // ZOOMSWAP_PATCH
	)
		return;

	#if ZOOMSWAP_PATCH
	if (c == nexttiled(c->mon->clients)) {
		p = c->mon->pertag->prevzooms[c->mon->pertag->curtag];
		at = findbefore(p);
		if (at)
			cprevious = nexttiled(at->next);
		if (!cprevious || cprevious != p) {
			c->mon->pertag->prevzooms[c->mon->pertag->curtag] = NULL;
			#if SWAPFOCUS_PATCH
			if (!c || !(c = c->mon->pertag->prevclient[c->mon->pertag->curtag] = nexttiled(c->next)))
			#else
			if (!c || !(c = nexttiled(c->next)))
			#endif // SWAPFOCUS_PATCH
				return;
		} else
			#if SWAPFOCUS_PATCH
			c = c->mon->pertag->prevclient[c->mon->pertag->curtag] = cprevious;
			#else
			c = cprevious;
			#endif // SWAPFOCUS_PATCH
	}

	cold = nexttiled(c->mon->clients);
	if (c != cold && !at)
		at = findbefore(c);
	detach(c);
	attach(c);
	/* swap windows instead of pushing the previous one down */
	if (c != cold && at) {
		c->mon->pertag->prevzooms[c->mon->pertag->curtag] = cold;
		if (cold && at != cold) {
			detach(cold);
			cold->next = at->next;
			at->next = cold;
		}
	}
	focus(c);
	arrange(c->mon);
	#else
	if (c == nexttiled(c->mon->clients))
		#if SWAPFOCUS_PATCH
		if (!c || !(c = c->mon->pertag->prevclient[c->mon->pertag->curtag] = nexttiled(c->next)))
		#else
		if (!c || !(c = nexttiled(c->next)))
		#endif // SWAPFOCUS_PATCH
			return;
	pop(c);
	#endif // ZOOMSWAP_PATCH
}

int
main(int argc, char *argv[])
{
	#if CMDCUSTOMIZE_PATCH
	for (int i=1;i<argc;i+=1)
		if (!strcmp("-v", argv[i]))
			die("dwm-"VERSION);
		else if (!strcmp("-h", argv[i]) || !strcmp("--help", argv[i]))
			die(help());
		else if (!strcmp("-fn", argv[i])) /* font set */
		#if BAR_PANGO_PATCH
			strcpy(font, argv[++i]);
		#else
			fonts[0] = argv[++i];
		#endif // BAR_PANGO_PATCH
		#if !BAR_VTCOLORS_PATCH
		else if (!strcmp("-nb", argv[i])) /* normal background color */
			colors[SchemeNorm][1] = argv[++i];
		else if (!strcmp("-nf", argv[i])) /* normal foreground color */
			colors[SchemeNorm][0] = argv[++i];
		else if (!strcmp("-sb", argv[i])) /* selected background color */
			colors[SchemeSel][1] = argv[++i];
		else if (!strcmp("-sf", argv[i])) /* selected foreground color */
			colors[SchemeSel][0] = argv[++i];
		#endif // !BAR_VTCOLORS_PATCH
		#if NODMENU_PATCH
		else if (!strcmp("-df", argv[i])) /* dmenu font */
			dmenucmd[2] = argv[++i];
		else if (!strcmp("-dnb", argv[i])) /* dmenu normal background color */
			dmenucmd[4] = argv[++i];
		else if (!strcmp("-dnf", argv[i])) /* dmenu normal foreground color */
			dmenucmd[6] = argv[++i];
		else if (!strcmp("-dsb", argv[i])) /* dmenu selected background color */
			dmenucmd[8] = argv[++i];
		else if (!strcmp("-dsf", argv[i])) /* dmenu selected foreground color */
			dmenucmd[10] = argv[++i];
		#else
		else if (!strcmp("-df", argv[i])) /* dmenu font */
			dmenucmd[4] = argv[++i];
		else if (!strcmp("-dnb", argv[i])) /* dmenu normal background color */
			dmenucmd[6] = argv[++i];
		else if (!strcmp("-dnf", argv[i])) /* dmenu normal foreground color */
			dmenucmd[8] = argv[++i];
		else if (!strcmp("-dsb", argv[i])) /* dmenu selected background color */
			dmenucmd[10] = argv[++i];
		else if (!strcmp("-dsf", argv[i])) /* dmenu selected foreground color */
			dmenucmd[12] = argv[++i];
		#endif // NODMENU_PATCH
		else die(help());
	#else
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	#endif // CMDCUSTOMIZE_PATCH
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	#if SWALLOW_PATCH
	if (!(xcon = XGetXCBConnection(dpy)))
		die("dwm: cannot get xcb connection\n");
	#endif // SWALLOW_PATCH
	checkotherwm();
	#if XRDB_PATCH && !BAR_VTCOLORS_PATCH
	XrmInitialize();
	loadxrdb();
	#endif // XRDB_PATCH && !BAR_VTCOLORS_PATCH
	#if COOL_AUTOSTART_PATCH
	autostart_exec();
	#endif // COOL_AUTOSTART_PATCH
	setup();
#ifdef __OpenBSD__
	#if SWALLOW_PATCH
	if (pledge("stdio rpath proc exec ps", NULL) == -1)
	#else
	if (pledge("stdio rpath proc exec", NULL) == -1)
	#endif // SWALLOW_PATCH
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	#if AUTOSTART_PATCH
	runautostart();
	#endif
	run();
	#if RESTARTSIG_PATCH
	if (restart)
		execvp(argv[0], argv);
	#endif // RESTARTSIG_PATCH
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
