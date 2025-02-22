/* Unixscr.c
 * This file contains routines for the Unix port of fractint.
 * It uses the current window for text and creates an X window for graphics.
 *
 * This file Copyright 1991 Ken Shirriff.  It may be used according to the
 * fractint license conditions, blah blah blah.
 *
 * Some of the X stuff is based on xloadimage by Jim Frost.
 * The FindWindowRoot routine is from ssetroot by Tom LaStrange.
 * Other root window stuff is based on xmartin, by Ed Kubaitis.
 * Some of the colormap stuff is from Mike Yang (mikey@sgi.com).
 * Some of the zoombox code is from Bill Broadley.
 * David Sanderson straightened out a bunch of include file problems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <signal.h>
#include <sys/types.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/time.h>
#include <sys/ioctl.h>
#ifdef FPUERR
#include <floatingpoint.h>
#endif
#ifdef __hpux
#include <sys/file.h>
#endif
#include <fcntl.h>
#include "helpdefs.h"
#include "port.h"
#include "prototyp.h"
#ifdef LINUX
#define FNDELAY O_NDELAY
#endif
#ifdef __SVR4
# include <sys/filio.h>
# define FNDELAY O_NONBLOCK
#endif

/* Check if there is a character waiting for us.  */
#define input_pending() (ioctl(0,FIONREAD,&iocount),(int)iocount)

/* external variables (set in the FRACTINT.CFG file, but findable here */

extern	int	dotmode;		/* video access method (= 19)	   */
extern	int	sxdots, sydots; 	/* total # of dots on the screen   */
extern	int	sxoffs, syoffs; 	/* offset of drawing area          */
extern	int	colors; 		/* maximum colors available	   */
extern	int	initmode;
extern	int	adapter;
extern	int	gotrealdac;
extern	int	inside_help;
extern  float	finalaspectratio;
extern  float	screenaspect;
extern	int	lookatmouse;

extern struct videoinfo videotable[];

/* the video-palette array (named after the VGA adapter's video-DAC) */

extern unsigned char dacbox[256][3];

extern void drawbox();

extern int text_type;
extern int helpmode;
extern int rotate_hi;

extern void fpe_handler();

extern WINDOW *curwin;

static int onroot = 0;
static int fullscreen = 0;
static int sharecolor = 0;
static int privatecolor = 0;
static int fixcolors = 0;
static int sync = 0; /* Run X events synchronously (debugging) */
int slowdisplay = 0; /* We have a slow display, so don't print too much */
static int simple_input = 0; /* Use simple input (debugging) */
static char *Xdisplay = "";
static char *Xgeometry = NULL;

static int unixDisk = 0; /* Flag if we use the disk video mode */

static int old_fcntl;

static int doesBacking;

/*
 * The pixtab stuff is so we can map from fractint pixel values 0-n to
 * the actual color table entries which may be anything.
 */
static int usepixtab = 0;
static unsigned long pixtab[256];
static int ipixtab[256];

static int fastmode = 0; /* Don't draw pixels 1 at a time */
static int alarmon = 0; /* 1 if the refresh alarm is on */
static int doredraw = 0; /* 1 if we have a redraw waiting */

/* Static routines */
static Window FindRootWindow(void);
static Window pr_dwmroot(Display *dpy, Window pwin);
static int errhand(Display *dp, XErrorEvent *xe);
static int getachar(void);
static int handleesc(void); 
static int translatekey(int ch); 
static int xcmapstuff(void); 
static int xhandleevents(void); 
static void RemoveRootPixmap(void);
static void doneXwindow(void);
static void initdacbox(void);
static void setredrawscreen(void); 
static void clearXwindow(void);
#ifdef FPUERR
static void continue_hdl(int sig, int code, struct sigcontext *scp,
	char *addr);
#endif

static int mousefkey[4][4] /* [button][dir] */ = {
    {RIGHT_ARROW,LEFT_ARROW,DOWN_ARROW,UP_ARROW},
    {0,0,PAGE_DOWN,PAGE_UP},
    {CTL_PLUS,CTL_MINUS,CTL_DEL,CTL_INSERT},
    {CTL_END,CTL_HOME,CTL_PAGE_DOWN,CTL_PAGE_UP}
};

/*
 *----------------------------------------------------------------------
 *
 * unixarg --
 *
 *	See if we want to do something with the argument.
 *
 * Results:
 *	Returns 1 if we parsed the argument.
 *
 * Side effects:
 *	Increments i if we use more than 1 argument.
 *
 *----------------------------------------------------------------------
 */
int
unixarg(argc,argv,i)
int argc;
char **argv;
int *i;
{
    if (strcmp(argv[*i],"-display")==0 && (*i)+1<argc) {
	Xdisplay = argv[(*i)+1];
	(*i)++;
	return 1;
    } else if (strcmp(argv[*i],"-fullscreen")==0) {
	fullscreen = 1;
	return 1;
    } else if (strcmp(argv[*i],"-disk")==0) {
	unixDisk = 1;
	return 1;
    } else if (strcmp(argv[*i],"-onroot")==0) {
	onroot = 1;
	return 1;
    } else if (strcmp(argv[*i],"-share")==0) {
	sharecolor = 1;
	return 1;
    } else if (strcmp(argv[*i],"-fast")==0) {
	fastmode = 1;
	return 1;
    } else if (strcmp(argv[*i],"-simple")==0) {
	simple_input = 1;
	return 1;
    } else if (strcmp(argv[*i],"-slowdisplay")==0) {
	slowdisplay = 1;
	return 1;
    } else if (strcmp(argv[*i],"-sync")==0) {
	sync = 1;
	return 1;
    } else if (strcmp(argv[*i],"-private")==0) {
	privatecolor = 1;
	return 1;
    } else if (strcmp(argv[*i],"-fixcolors")==0 && *i+1<argc) {
	fixcolors = atoi(argv[(*i)+1]);
	(*i)++;
	return 1;
    } else if (strcmp(argv[*i],"-geometry")==0 && *i+1<argc) {
	Xgeometry = argv[(*i)+1];
	(*i)++;
	return 1;
    } else {
	return 0;
    }
}
/*
 *----------------------------------------------------------------------
 *
 * UnixInit --
 *
 *	Initialize the windows and stuff.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes windows.
 *
 *----------------------------------------------------------------------
 */
void
UnixInit()
{
    /*
     * Check a bunch of important conditions
     */
    if (sizeof(short) != 2) {
	fprintf(stderr,"Error: need short to be 2 bytes\n");
	exit(-1);
    }
    if (sizeof(long) < sizeof(FLOAT4)) {
	fprintf(stderr,"Error: need sizeof(long)>=sizeof(FLOAT4)\n");
	exit(-1);
    }

    initscr();
    curwin = stdscr;
    cbreak();
    noecho();

    if (standout()) {
	text_type = 1;
	standend();
    } else {
	text_type = 1;
    }

    initdacbox();

    if (!simple_input) {
	  signal(SIGINT,(__sighandler_t)goodbye);
    }
    signal(SIGFPE, fpe_handler);
    /*
    signal(SIGTSTP,goodbye);
    */
#ifdef FPUERR
    signal(SIGABRT,SIG_IGN);
    /*
		setup the IEEE-handler to forget all common ( invalid,
		divide by zero, overflow ) signals. Here we test, if 
		such ieee trapping is supported.
    */
    if (ieee_handler("set","common",continue_hdl) != 0 )
        printf("ieee trapping not supported here \n");
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * UnixDone --
 *
 *	Cleanup windows and stuff.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cleans up.
 *
 *----------------------------------------------------------------------
 */

void
UnixDone()
{
    if (!unixDisk) {
	doneXwindow();
    }
    if (!simple_input) {
	fcntl(0,F_SETFL,old_fcntl);
    }
    mvcur(0,COLS-1, LINES-1,0);
    nocbreak();
    echo();
    endwin();
}

/*
 *----------------------------------------------------------------------
 *
 * errhand --
 *
 *	Called on an X server error.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints the error message.
 *
 *----------------------------------------------------------------------
 */
static int errhand(dp,xe)
Display *dp;
XErrorEvent *xe;
{
        char buf[200];
        fflush(stdout);
        printf("X Error: %d %d %d %d\n",xe->type,xe->error_code,
		xe->request_code, xe->minor_code);
        XGetErrorText(dp,xe->error_code,buf,200);
        printf("%s\n",buf);
}

#ifdef FPUERR
/*
 *----------------------------------------------------------------------
 *
 * continue_hdl --
 *
 *	Handle an IEEE fpu error.
 *	This routine courtesy of Ulrich Hermes
 *	<hermes@olymp.informatik.uni-dortmund.de>
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears flag.
 *
 *----------------------------------------------------------------------
 */
static void
continue_hdl(sig,code,scp,addr)
int sig, code;

struct sigcontext *scp;
char *addr;

{
    int i;
    char out[20];
    /*		if you want to get all messages enable this statement.    */
    /*  printf("ieee exception code %x occurred at pc %X\n",code,scp->sc_pc); */
    /*	clear all excaption flags					  */
    i = ieee_flags("clear","exception","all",out);
}
#endif

#define DEFX 640
#define DEFY 480
#define DEFXY "640x480+0+0"

static Display *Xdp = NULL;
static Window Xw;
static GC Xgc = NULL;
static Visual *Xvi;
static Screen *Xsc;
static Colormap	Xcmap;
static int Xdepth;
static XImage *Ximage =NULL;
static char *Xdata;
static int Xdscreen;
static Pixmap	Xpixmap = 0;
static int Xwinwidth=DEFX,Xwinheight=DEFY;
static Window Xroot;
static int xlastcolor = -1;
static int xlastfcn = GXcopy;
static BYTE *pixbuf = NULL;

/*
 *----------------------------------------------------------------------
 *
 * initUnixWindow --
 *
 *	Make the X window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes window.
 *
 *----------------------------------------------------------------------
 */

void
initUnixWindow()
{
    XSetWindowAttributes Xwatt;
    XGCValues Xgcvals;
    int Xwinx=0,Xwiny=0;
    int i;

    if (Xdp != NULL) {
       /* We are already initialized */
       return;
    }

    if (!simple_input) {
	old_fcntl = fcntl(0,F_GETFL);
	fcntl(0,F_SETFL,FNDELAY);
    }

    /*
    initmode = 0;
    */
    adapter = 0;

    /* We have to do some X stuff even for disk video, to parse the geometry
     * string */

    if (unixDisk) {
	int offx, offy;
	fastmode = 0;
	gotrealdac=1;
	colors = 256;
	for (i=0;i<colors;i++) {
	    pixtab[i] = i;
	    ipixtab[i] = i;
	}
	if (fixcolors>0) {
	    colors = fixcolors;
	}
	if (Xgeometry) {
	    XParseGeometry(Xgeometry, &offx, &offy, (unsigned int *)&Xwinwidth,
		    (unsigned int *)&Xwinheight);
	}
	sxdots = Xwinwidth;
	sydots = Xwinheight;
    } else {
	Xdp = XOpenDisplay(Xdisplay);
	if (Xdp==NULL) {
	    fprintf(stderr,"Could not open display %s\n",Xdisplay);
	    fprintf(stderr,"Note: xfractint can run without X in -disk mode\n");
	    UnixDone();
	    exit(-1);
	}
	Xdscreen = XDefaultScreen(Xdp);
	if (Xgeometry && !onroot) {
	    XGeometry(Xdp, Xdscreen, Xgeometry, DEFXY, 0, 1, 1, 0, 0,
		    &Xwinx, &Xwiny, &Xwinwidth, &Xwinheight);
	}
	if (sync) {
	    XSynchronize(Xdp,True);
	}
	XSetErrorHandler(errhand);
	Xsc = ScreenOfDisplay(Xdp,Xdscreen);
	Xvi = XDefaultVisualOfScreen(Xsc);
	Xdepth = DefaultDepth(Xdp,Xdscreen);
	colors = 1<<Xdepth;
	if (colors>256) colors = 256;
	if (fixcolors>0) {
	    colors = fixcolors;
	}
	switch (Xvi->class) {
	    case DirectColor:
	    case PseudoColor:
	    case GrayScale:
		gotrealdac=1;
		break;
	    case TrueColor:
	    case StaticColor:
	    case StaticGray:
	    default:
		gotrealdac=0;
		break;
	}

	if (fullscreen || onroot) {
	    Xwinwidth = DisplayWidth(Xdp,Xdscreen);
	    Xwinheight = DisplayHeight(Xdp,Xdscreen);
	}
	sxdots = Xwinwidth;
	sydots = Xwinheight;

	Xwatt.background_pixel = BlackPixelOfScreen(Xsc);
	Xwatt.bit_gravity = StaticGravity;
	doesBacking = DoesBackingStore(Xsc);
	if (doesBacking) {
	    Xwatt.backing_store = Always;
	} else {
	    Xwatt.backing_store = NotUseful;
	}
	if (onroot) {
	    Xroot = FindRootWindow();
	    RemoveRootPixmap();
	    /*
	    XSetWindowBackground(Xdp,Xroot);
	    */
	    Xgc = XCreateGC(Xdp,Xroot,0,&Xgcvals);
	    Xpixmap = XCreatePixmap(Xdp,Xroot,Xwinwidth,Xwinheight,Xdepth);
	    Xw = Xroot;
	    XFillRectangle(Xdp,Xpixmap,Xgc,0,0,Xwinwidth,Xwinheight);
	    XSetWindowBackgroundPixmap(Xdp,Xroot,Xpixmap);
	} else {
	    Xroot = DefaultRootWindow(Xdp);
	    Xw = XCreateWindow(Xdp, Xroot, Xwinx, Xwiny, Xwinwidth,
		    Xwinheight, 0, Xdepth, InputOutput, CopyFromParent,
		    CWBackPixel|CWBitGravity|CWBackingStore, &Xwatt);
	    XStoreName(Xdp,Xw,"xfractint");
	    Xgc = XCreateGC(Xdp,Xw,0,&Xgcvals);
	}
	colors = xcmapstuff();
	if (rotate_hi==255) rotate_hi = colors-1;
	if (!onroot) {
	    XSetBackground(Xdp,Xgc,pixtab[0]);
	    XSetForeground(Xdp,Xgc,pixtab[1]);
	    Xwatt.background_pixel = pixtab[0];
	    XChangeWindowAttributes(Xdp,Xw,CWBackPixel,&Xwatt);
	    XMapWindow(Xdp,Xw);
	}
	if (onroot) {
	    XSelectInput(Xdp,Xw,KeyPressMask|KeyReleaseMask|ExposureMask);
	} else {
	    XSelectInput(Xdp,Xw,KeyPressMask|KeyReleaseMask|ExposureMask|
		    ButtonPressMask|ButtonReleaseMask|PointerMotionMask);
	}

	resizeWindow();
	xsync();
    }

    writevideopalette();

    videotable[0].xdots = sxdots;
    videotable[0].ydots = sydots;
    videotable[0].colors = colors;
    videotable[0].dotmode = (unixDisk) ? 11 : 19;
}
/*
 *----------------------------------------------------------------------
 *
 * doneXwindow --
 *
 *	Clean up the X stuff.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees window, etc.
 *
 *----------------------------------------------------------------------
 */
static void
doneXwindow()
{
    if (Xdp==NULL) {
	return;
    }
    if (Xgc) {
	XFreeGC(Xdp,Xgc);
    }
    if (Xpixmap) {
	XFreePixmap(Xdp,Xpixmap);
	Xpixmap = (Pixmap)NULL;
    }
    XFlush(Xdp);
    /*
    XCloseDisplay(Xdp);
    */
    Xdp = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * clearXwindow --
 *
 *	Clears X window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears window.
 *
 *----------------------------------------------------------------------
 */
static void
clearXwindow()
{
    char *ptr;
    int i,len;
    if (pixtab[0] != 0) {
	/*
	 * Initialize image to pixtab[0].
	 */
	if (colors==2) {
	    for (i=0;i<Ximage->bytes_per_line;i++) {
		Ximage->data[i] = 0xff;
	    }
	} else {
	    for (i=0;i<Ximage->bytes_per_line;i++) {
		Ximage->data[i] = pixtab[0];
	    }
	}
	for (i=1;i<Ximage->height;i++) {
	    bcopy(Ximage->data,Ximage->data+i*Ximage->bytes_per_line,
		Ximage->bytes_per_line);
	}
    } else {
	/*
	 * Initialize image to 0's.
	 */
	bzero(Ximage->data,Ximage->bytes_per_line*Ximage->height);
    }
    xlastcolor = -1;
    XSetForeground(Xdp, Xgc, pixtab[0]);
    if (onroot) {
	XFillRectangle(Xdp,Xpixmap,Xgc,0,0,Xwinwidth,Xwinheight);
    }
    XFillRectangle(Xdp,Xw,Xgc,0,0,Xwinwidth,Xwinheight);
    xsync();
}

/*
 *----------------------------------------------------------------------
 *
 * initdacbox --
 *
 * Put something nice in the dac.
 *
 * The conditions are:
 *	Colors 1 and 2 should be bright so ifs fractals show up.
 *	Color 15 should be bright for lsystem.
 *	Color 1 should be bright for bifurcation.
 *	Colors 1,2,3 should be distinct for periodicity.
 *	The color map should look good for mandelbrot.
 *	The color map should be good if only 128 colors are used.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads the dac.
 *
 *----------------------------------------------------------------------
 */
static void
initdacbox()
{
    int i;
    for (i=0;i<256;i++) {
	dacbox[i][0] = (i>>5)*8+7;
	dacbox[i][1] = (((i+16)&28)>>2)*8+7;
	dacbox[i][2] = (((i+2)&3))*16+15;
    }
    dacbox[0][0] = dacbox[0][1] = dacbox[0][2] = 0;
    dacbox[1][0] = dacbox[1][1] = dacbox[1][2] = 63;
    dacbox[2][0] = 47; dacbox[2][1] = dacbox[2][2] = 63;
}


int startvideo()
{
    clearXwindow();
    return(0);
}

int endvideo()
{
    return(0);				/* set flag: video ended */

}

/*
 *----------------------------------------------------------------------
 *
 * resizeWindow --
 *
 *	Look after resizing the window if necessary.
 *
 * Results:
 *	Returns 1 for resize, 0 for no resize.
 *
 * Side effects:
 *	May reallocate data structures.
 *
 *----------------------------------------------------------------------
 */
int
resizeWindow()
{
    static int oldx = -1, oldy = -1;
    int junki;
    unsigned int junkui;
    Window junkw;
    unsigned int width, height;
    int Xmwidth;
    Status status;

    if (unixDisk) return 0;

    XGetGeometry(Xdp,Xw,&junkw,&junki, &junki, &width, &height,
	    &junkui, &junkui);

    if (oldx != width || oldy != height) {
	sxdots = width;
	sydots = height;
	videotable[0].xdots = sxdots;
	videotable[0].ydots = sydots;
	oldx = sxdots;
	oldy = sydots;
	Xwinwidth = sxdots;
	Xwinheight = sydots;
	screenaspect = sydots/(float)sxdots;
	finalaspectratio = screenaspect;
	Xmwidth = (Xdepth > 1)? sxdots: (1 + sxdots/8);
	if (pixbuf != NULL) {
	    free(pixbuf);
	}
	pixbuf = (BYTE *) malloc(Xwinwidth *sizeof(BYTE));
	if (Ximage != NULL) {
	    free(Ximage->data);
	    XDestroyImage(Ximage);
	}
	Ximage = XCreateImage(Xdp,Xvi,Xdepth, ZPixmap, 0, NULL, sxdots,
	    sydots,8, Xmwidth);
	if (Ximage == NULL) {
	    printf("XCreateImage failed\n");
	    UnixDone();
	    exit(-1);
	}
	Ximage->data = malloc(Ximage->bytes_per_line * Ximage->height);
	if (Ximage->data==NULL) {
	    fprintf(stderr,"Malloc failed: %d\n", Ximage->bytes_per_line *
		    Ximage->height);
	    exit(-1);
	}
	clearXwindow();
	return 1;
    } else {
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * xcmapstuff --
 *
 *	Set up the colormap appropriately
 *
 * Results:
 *	Number of colors.
 *
 * Side effects:
 *	Sets colormap.
 *
 *----------------------------------------------------------------------
 */
static int
xcmapstuff()
{
    int ncells,i,powr;

    if (onroot) {
	privatecolor = 0;
    }
    for (i=0;i<colors;i++) {
	pixtab[i] = i;
	ipixtab[i] = 999;
    }
    if (!gotrealdac) {
    } else if (sharecolor) {
	gotrealdac = 0;
    } else if (privatecolor) {
	Xcmap = XCreateColormap(Xdp,Xw,Xvi,AllocAll);
	XSetWindowColormap(Xdp,Xw,Xcmap);
    } else {
	Xcmap = DefaultColormap(Xdp,Xdscreen);
	for (powr=Xdepth;powr>=1;powr--) {
	    ncells = 1<<powr;
	    if (ncells>colors) continue;
	    if (XAllocColorCells(Xdp,Xcmap,False,NULL,0,pixtab,
		    (unsigned int)ncells)) {
		colors = ncells;
		printf("%d colors\n",colors);
		usepixtab = 1;
		break;
	    }
	}
	if (!usepixtab) {
	    printf("Couldn't allocate any colors\n");
	    gotrealdac = 0;
	}
    }
    for (i=0;i<colors;i++) {
	ipixtab[pixtab[i]] = i;
    }
    /* We must make sure if any color uses position 0, that it is 0.
     * This is so we can clear the image with bzero.
     * So, suppose fractint 0 = cmap 42, cmap 0 = fractint 55.
     * Then want fractint 0 = cmap 0, cmap 42 = fractint 55.
     * I.e. pixtab[55] = 42, ipixtab[42] = 55.
     */
    if (ipixtab[0] == 999) {
	ipixtab[0] = 0;
    } else if (ipixtab[0] != 0) {
	int other;
	other = ipixtab[0];
	pixtab[other] = pixtab[0];
	ipixtab[pixtab[other]] = other;
	pixtab[0] = 0;
	ipixtab[0] = 0;
    }

    if (!gotrealdac && colors==2 && BlackPixelOfScreen(Xsc)!=0) {
	pixtab[0] = ipixtab[0] = 1;
	pixtab[1] = ipixtab[1] = 0;
	usepixtab = 1;
    }

    return colors;
}
/*
 *----------------------------------------------------------------------
 *
 * writevideoline --
 *
 *	Write a line of pixels to the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws pixels.
 *
 *----------------------------------------------------------------------
 */
void
writevideoline(y,x,lastx,pixels)
int x,y,lastx;
BYTE *pixels;
{
    int width;
    int i;
    BYTE *pixline;

#if 1
    if (x==lastx) {
	writevideo(x,y,pixels[0]);
	return;
    }
    width = lastx-x+1;
    if (usepixtab) {
	for (i=0;i<width;i++) {
	    pixbuf[i] = pixtab[pixels[i]];
	}
	pixline = pixbuf;
    } else {
	pixline = pixels;
    }
    for (i=0;i<width;i++) {
	XPutPixel(Ximage,x+i,y,pixline[i]);
    }
    if (fastmode==1 && helpmode != HELPXHAIR) {
	if (!alarmon) {
	    schedulealarm(0);
	}
    } else {
	XPutImage(Xdp,Xw,Xgc,Ximage,x,y,x,y,width,1);
	if (onroot) {
	    XPutImage(Xdp,Xpixmap,Xgc,Ximage,x,y,x,y,width,1);
	}
    }
#else
    width = lastx-x+1;
    for (i=0;i<width;i++) {
	writevideo(x+i,y,pixels[i]);
    }
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * readvideoline --
 *
 *	Reads a line of pixels from the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Gets pixels
 *
 *----------------------------------------------------------------------
 */
void
readvideoline(y,x,lastx,pixels)
int x,y,lastx;
BYTE *pixels;
{
    int i,width;
    width = lastx-x+1;
    for (i=0;i<width;i++) {
	pixels[i] = readvideo(x+i,y);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * writevideo --
 *
 *	Write a point to the screen
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws point.
 *
 *----------------------------------------------------------------------
 */
void writevideo(x,y,color)
int x,y,color;
{
#ifdef DEBUG /* Debugging checks */
    if (color>=colors || color < 0) {
	printf("Color %d too big %d\n", color, colors);
    }
    if (x>=sxdots || x<0 || y>=sydots || y<0) {
	printf("Bad coord %d %d\n", x,y);
    }
#endif
    if (xlastcolor != color) {
	XSetForeground(Xdp,Xgc,pixtab[color]);
	xlastcolor = color;
    }
    XPutPixel(Ximage,x,y,pixtab[color]);
    if (fastmode==1 && helpmode != HELPXHAIR) {
	if (!alarmon) {
	    schedulealarm(0);
	}
    } else {
	XDrawPoint(Xdp,Xw,Xgc,x,y);
	if (onroot) {
	    XDrawPoint(Xdp,Xpixmap,Xgc,x,y);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * readvideo --
 *
 *	Read a point from the screen
 *
 * Results:
 *	Value of point.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int readvideo(int x, int y)
{
    return ipixtab[XGetPixel(Ximage,x,y)];
}

XColor cols[256];

/*
 *----------------------------------------------------------------------
 *
 * readvideopalette --
 *	Reads the current video palette into dacbox.
 *	
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in dacbox.
 *
 *----------------------------------------------------------------------
 */
int readvideopalette()
{

    int i;
    if (gotrealdac==0) return -1;
    for (i=0;i<256;i++) {
	dacbox[i][0] = cols[i].red/1024;
	dacbox[i][1] = cols[i].green/1024;
	dacbox[i][2] = cols[i].blue/1024;
    }
    return 0;

}

/*
 *----------------------------------------------------------------------
 *
 * writevideopalette --
 *	Writes dacbox into the video palette.
 *	
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the displayed colors.
 *
 *----------------------------------------------------------------------
 */
int writevideopalette()
{
    int i;

    if (!gotrealdac) return -1;
    for(i=0;i<256;i++) {
	cols[i].pixel = pixtab[i];
	cols[i].flags = DoRed | DoGreen | DoBlue;
	cols[i].red = dacbox[i][0]*1024;
	cols[i].green = dacbox[i][1]*1024;
	cols[i].blue = dacbox[i][2]*1024;
    }
    if (!unixDisk) {
	XStoreColors(Xdp,Xcmap,cols,colors);
	XFlush(Xdp);
    }
    return 0;

}
/*
 *----------------------------------------------------------------------
 *
 * setlinemode --
 *
 *	Set line mode to 0=draw or 1=xor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets mode.
 *
 *----------------------------------------------------------------------
 */
void
setlinemode(mode)
int mode;
{
    if (unixDisk) return;
    xlastcolor = -1;
    if (mode==0) {
	XSetFunction(Xdp, Xgc, GXcopy);
	xlastfcn = GXcopy;
    } else {
	XSetForeground(Xdp, Xgc, colors-1);
	xlastcolor = -1;
	XSetFunction(Xdp, Xgc, GXxor);
	xlastfcn = GXxor;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * drawline --
 *
 *	Draw a line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies window.
 *
 *----------------------------------------------------------------------
 */
void
drawline(x1,y1,x2,y2)
int x1,y1,x2,y2;
{
    if (!unixDisk) {
	XDrawLine(Xdp,Xw,Xgc,x1,y1,x2,y2);
    }
}
/*
 *----------------------------------------------------------------------
 *
 * xsync --
 *
 *	Sync the x server
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies window.
 *
 *----------------------------------------------------------------------
 */
void
xsync()
{
    if (!unixDisk) {
	XSync(Xdp,False);
    }
}
/*
 *----------------------------------------------------------------------
 *
 * getachar --
 *
 *	Gets a character.
 *
 * Results:
 *	Key.
 *
 * Side effects:
 *	Reads key.
 *
 *----------------------------------------------------------------------
 */
static int
getachar()
{
    if (simple_input) {
	return getchar();
    } else {
	char ch;
	int status;
	status = read(0,&ch,1);
	if (status<0) {
	    return -1;
	} else {
	    return ch;
	}
    }
}

static int xbufkey = 0;		/* Buffered X key */
/*
 *----------------------------------------------------------------------
 *
 * xgetkey --
 *
 *	Get a key from the keyboard or the X server.
 *	Blocks if block = 1.
 *
 * Results:
 *	Key, or 0 if no key and not blocking.
 *	Times out after .5 second.
 *
 * Side effects:
 *	Processes X events.
 *
 *----------------------------------------------------------------------
 */
int
xgetkey(block)
int block;
{
    static int skipcount = 0;
    int ch,ch2;
    fd_set reads;
    int status;
    static struct timeval tout;
    tout.tv_sec = 0;
    tout.tv_usec = 500000;
    while (1) {
	if (input_pending()) {
	    ch = getachar();
	    if (ch == ESC) {
		return handleesc();
	    } else {
		return translatekey(ch);
	    }
	}

	/* Don't check X events every time, since that is expensive */
	skipcount++;
	if (block==0 && skipcount<25) break;
	skipcount = 0;

	if (!unixDisk) {
	    xhandleevents();
	}
	if (xbufkey) {
	    ch = xbufkey;
	    xbufkey = 0;
	    skipcount = 9999; /* If we got a key, check right away next time */
	    return translatekey(ch);
	}
	if (!block) break;
	FD_ZERO(&reads);
	FD_SET(0,&reads);
	if (unixDisk) {
	    status = select(1,&reads,NULL,NULL,&tout);
	} else {
	    FD_SET(ConnectionNumber(Xdp),&reads);
	    status = select(ConnectionNumber(Xdp)+1,&reads,NULL,NULL,&tout);
	}
	if (status<=0) {
	    return 0;
	}
    }
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * translatekey --
 *
 *	Translate an input key into MSDOS format.  The purpose of this
 *	routine is to do the mappings like U -> PAGE_UP.
 *
 * Results:
 *	New character;
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
translatekey(ch)
int ch;
{
    if (ch>='a' && ch<='z') {
	return ch;
    } else {
	switch (ch) {
	    case 'I':
		return INSERT;
	    case 'D':
		return DELETE;
	    case 'U':
		return PAGE_UP;
	    case 'N':
		return PAGE_DOWN;
	    case CTL('O'):
		return CTL_HOME;
	    case CTL('E'):
		return CTL_END;
	    case 'H':
		return LEFT_ARROW;
	    case 'L':
		return RIGHT_ARROW;
	    case 'K':
		return UP_ARROW;
	    case 'J':
		return DOWN_ARROW;
	    case 1115:
		return LEFT_ARROW_2;
	    case 1116:
		return RIGHT_ARROW_2;
	    case 1141:
		return UP_ARROW_2;
	    case 1145:
		return DOWN_ARROW_2;
	    case 'O':
		return HOME;
	    case 'E':
		return END;
	    case '\n':
		return ENTER;
	    case CTL('T'):
		return CTL_ENTER;
	    case -2:
		return CTL_ENTER_2;
	    case CTL('U'):
		return CTL_PAGE_UP;
	    case CTL('N'):
		return CTL_PAGE_DOWN;
	    case '{':
		return CTL_MINUS;
	    case '}':
		return CTL_PLUS;
	    /* we need ^I for tab */
#if 0
	    case CTL('I'):
		return CTL_INSERT;
#endif
	    case CTL('D'):
		return CTL_DEL;
	    case '!':
		return F1;
	    case '@':
		return F2;
	    case '#':
		return F3;
	    case '$':
		return F4;
	    case '%':
		return F5;
	    case '^':
		return F6;
	    case '&':
		return F7;
	    case '*':
		return F8;
	    case '(':
		return F9;
	    case ')':
		return F10;
	    default:
		return ch;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * handleesc --
 *
 *	Handle an escape key.  This may be an escape key sequence
 *	indicating a function key was pressed.
 *
 * Results:
 *	Key.
 *
 * Side effects:
 *	Reads keys.
 *
 *----------------------------------------------------------------------
 */
static int
handleesc()
{
    int ch1,ch2,ch3;
    if (simple_input) {
	return ESC;
    }
#ifdef __hpux
    /* HP escape key sequences. */
    ch1 = getachar();
    if (ch1==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch1 = getachar();
    }
    if (ch1==-1) {
	return ESC;
    }
    switch (ch1) {
	case 'A':
	    return UP_ARROW;
	case 'B':
	    return DOWN_ARROW;
	case 'D':
	    return LEFT_ARROW;
	case 'C':
	    return RIGHT_ARROW;
	case 'd':
	    return HOME;
    }
    if (ch1 != '[') return ESC;
    ch1 = getachar();
    if (ch1==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch1 = getachar();
    }
    if (ch1==-1 || !isdigit(ch1)) return ESC;
    ch2 = getachar();
    if (ch2==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch2 = getachar();
    }
    if (ch2==-1) return ESC;
    if (isdigit(ch2)) {
	ch3 = getachar();
	if (ch3==-1) {
	    delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	    ch3 = getachar();
	}
	if (ch3 != '~') return ESC;
	ch2 = (ch2-'0')*10+ch3-'0';
    } else if (ch3 != '~') {
	return ESC;
    } else {
	ch2 = ch2-'0';
    }
    switch (ch2) {
	case 5:
	    return PAGE_UP;
	case 6:
	    return PAGE_DOWN;
	case 29:
	    return F1; /* help */
	case 11:
	    return F1;
	case 12:
	    return F2;
	case 13:
	    return F3;
	case 14:
	    return F4;
	case 15:
	    return F5;
	case 17:
	    return F6;
	case 18:
	    return F7;
	case 19:
	    return F8;
	default:
	    return ESC;
    }
#else
    /* SUN escape key sequences */
    ch1 = getachar();
    if (ch1==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch1 = getachar();
    }
    if (ch1 != '[') {		/* See if we have esc [ */
	return ESC;
    }
    ch1 = getachar();
    if (ch1==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch1 = getachar();
    }
    if (ch1==-1) {
	return ESC;
    }
    switch (ch1) {
	case 'A':		/* esc [ A */
	    return UP_ARROW;
	case 'B':		/* esc [ B */
	    return DOWN_ARROW;
	case 'C':		/* esc [ C */
	    return RIGHT_ARROW;
	case 'D':		/* esc [ D */
	    return LEFT_ARROW;
	default:
	    break;
    }
    ch2 = getachar();
    if (ch2==-1) {
	delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	ch2 = getachar();
    }
    if (ch2 == '~') {		/* esc [ ch1 ~ */
	switch (ch1) {
	    case '2':		/* esc [ 2 ~ */
		return INSERT;
	    case '3':		/* esc [ 3 ~ */
		return DELETE;
	    case '5':		/* esc [ 5 ~ */
		return PAGE_UP;
	    case '6':		/* esc [ 6 ~ */
		return PAGE_DOWN;
	    default:
		return ESC;
	}
    } else if (ch2==-1) {
	return ESC;
    } else {
	ch3 = getachar();
	if (ch3==-1) {
	    delay(250); /* Wait 1/4 sec to see if a control sequence follows */
	    ch3 = getachar();
	}
	if (ch3 != '~') {	/* esc [ ch1 ch2 ~ */
	    return ESC;
	}
	if (ch1=='1') {
	    switch (ch2) {
		case '1':	/* esc [ 1 1 ~ */
		    return F1;
		case '2':	/* esc [ 1 2 ~ */
		    return F2;
		case '3':	/* esc [ 1 3 ~ */
		    return F3;
		case '4':	/* esc [ 1 4 ~ */
		    return F4;
		case '5':	/* esc [ 1 5 ~ */
		    return F5;
		case '6':	/* esc [ 1 6 ~ */
		    return F6;
		case '7':	/* esc [ 1 7 ~ */
		    return F7;
		case '8':	/* esc [ 1 8 ~ */
		    return F8;
		case '9':	/* esc [ 1 9 ~ */
		    return F9;
		default:
		    return ESC;
	    }
	} else if (ch1=='2') {
	    switch (ch2) {
		case '0':	/* esc [ 2 0 ~ */
		    return F10;
		case '8':	/* esc [ 2 8 ~ */
		    return F1;  /* HELP */
		default:
		    return ESC;
	    }
	} else {
	    return ESC;
	}
    }
#endif
}

extern int editpal_cursor;
extern void Cursor_SetPos();

int XZoomWaiting = 0;

#define SENS 1
#define ABS(x) ((x)>0?(x):-(x))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define SIGN(x) ((x)>0?1:-1)

/*
 *----------------------------------------------------------------------
 *
 * xhandleevents --
 *
 *	Handles X events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Does event action.
 *
 *----------------------------------------------------------------------
 */
static int
xhandleevents()
{
    XEvent xevent;
    static ctl_mode = 0;
    static shift_mode = 0;
    int bandx0,bandy0,bandx1,bandy1;
    static int bnum=0;
    static int lastx,lasty;
    static int dx,dy;

    if (doredraw) {
	redrawscreen();
    }

    while (XPending(Xdp) && !xbufkey) {
	XNextEvent(Xdp,&xevent);
	switch (((XAnyEvent *)&xevent)->type) {
	    case KeyRelease:
		{
		char buffer[1];
		KeySym keysym;
		(void) XLookupString(&xevent,buffer,1,&keysym,NULL);
		switch (keysym) {
		    case XK_Control_L:
		    case XK_Control_R:
			ctl_mode = 0;
			break;
		    case XK_Shift_L:
		    case XK_Shift_R:
			shift_mode = 0;
			break;
		}
		}
		break;
	    case KeyPress:
		{
		int charcount;
		char buffer[1];
		KeySym keysym;
		int compose;
		charcount = XLookupString(&xevent,buffer,1,&keysym,NULL);
		switch (keysym) {
		    case XK_Control_L:
		    case XK_Control_R:
			ctl_mode = 1;
			return;
		    case XK_Shift_L:
		    case XK_Shift_R:
			shift_mode = 1;
			break;
		    case XK_Home:
		    case XK_R7:
			xbufkey = ctl_mode ? CTL_HOME : HOME;
			return;
		    case XK_Left:
		    case XK_R10:
			xbufkey = ctl_mode ? LEFT_ARROW_2 : LEFT_ARROW;
			return;
		    case XK_Right:
		    case XK_R12:
			xbufkey = ctl_mode ? RIGHT_ARROW_2 : RIGHT_ARROW;
			return;
		    case XK_Down:
		    case XK_R14:
			xbufkey = ctl_mode ? DOWN_ARROW_2 : DOWN_ARROW;
			return;
		    case XK_Up:
		    case XK_R8:
			xbufkey = ctl_mode ? UP_ARROW_2 : UP_ARROW;
			return;
		    case XK_Insert:
			xbufkey = ctl_mode ? CTL_INSERT : INSERT;
			return;
		    case XK_Delete:
			xbufkey = ctl_mode ? CTL_DEL : DELETE;
			return;
		    case XK_End:
		    case XK_R13:
			xbufkey = ctl_mode ? CTL_END : END;
			return;
		    case XK_Help:
			xbufkey = F1;
			return;
		    case XK_Prior:
		    case XK_R9:
			xbufkey = ctl_mode ? CTL_PAGE_UP : PAGE_UP;
			 return;
		    case XK_Next:
		    case XK_R15:
			xbufkey = ctl_mode ? CTL_PAGE_DOWN : PAGE_DOWN;
			 return;
		    case XK_F1:
		    case XK_L1:
			xbufkey = shift_mode ? SF1: F1;
			return;
		    case XK_F2:
		    case XK_L2:
			xbufkey = shift_mode ? SF2: F2;
			return;
		    case XK_F3:
		    case XK_L3:
			xbufkey = shift_mode ? SF3: F3;
			return;
		    case XK_F4:
		    case XK_L4:
			xbufkey = shift_mode ? SF4: F4;
			return;
		    case XK_F5:
		    case XK_L5:
			xbufkey = shift_mode ? SF5: F5;
			return;
		    case XK_F6:
		    case XK_L6:
			xbufkey = shift_mode ? SF6: F6;
			return;
		    case XK_F7:
		    case XK_L7:
			xbufkey = shift_mode ? SF7: F7;
			return;
		    case XK_F8:
		    case XK_L8:
			xbufkey = shift_mode ? SF8: F8;
			return;
		    case XK_F9:
		    case XK_L9:
			xbufkey = shift_mode ? SF9: F9;
			return;
		    case XK_F10:
		    case XK_L10:
			xbufkey = shift_mode ? SF10: F10;
			return;
		    case '+':
			 xbufkey = ctl_mode ? CTL_PLUS : '+';
			 return;
		    case '-':
			 xbufkey = ctl_mode ? CTL_MINUS : '-';
			 return;
			 break;
		    case XK_Return:
		    case XK_KP_Enter:
			 xbufkey = ctl_mode ? CTL('T') : '\n';
			 return;
		}
		if (charcount==1) {
		    xbufkey = buffer[0];
		    if (xbufkey=='\003') {
			goodbye();
		    }
		}
		}
		break;
	    case MotionNotify:
		{
		if (editpal_cursor && !inside_help) {
		    while ( XCheckWindowEvent(Xdp,Xw,PointerMotionMask,
			&xevent)) {}

		    if (xevent.xmotion.state&Button2Mask ||
			    (xevent.xmotion.state&Button1Mask &&
			    xevent.xmotion.state&Button3Mask)) {
			bnum = 3;
		    } else if (xevent.xmotion.state&Button1Mask) {
			bnum = 1;
		    } else if (xevent.xmotion.state&Button3Mask) {
			bnum = 2;
		    } else {
			bnum = 0;
		    }

#define MSCALE 1

		    if (lookatmouse==3 && bnum != 0) {
			dx += (xevent.xmotion.x-lastx)/MSCALE;
			dy += (xevent.xmotion.y-lasty)/MSCALE;
			lastx = xevent.xmotion.x;
			lasty = xevent.xmotion.y;
		    } else {
			Cursor_SetPos(xevent.xmotion.x, xevent.xmotion.y);
			xbufkey = ENTER;
		    }

		}
		}
		break;
	    case ButtonPress:
		{
		int done = 0;
		int banding = 0;
		if (lookatmouse==3 || zoomoff == 0) {
		    lastx = xevent.xbutton.x;
		    lasty = xevent.xbutton.y;
		    break;
		}
		bandx1 = bandx0 = xevent.xbutton.x;
		bandy1 = bandy0 = xevent.xbutton.y;
		while (!done) {
		    XNextEvent(Xdp,&xevent);
		    switch (xevent.type) {
		    case MotionNotify:
			while ( XCheckWindowEvent(Xdp,Xw,PointerMotionMask,
			    &xevent)) {}
			if (banding) {
			    XDrawRectangle(Xdp,Xw,Xgc,MIN(bandx0,bandx1),
				MIN(bandy0,bandy1), ABS(bandx1-bandx0),
				ABS(bandy1-bandy0));
			}
			bandx1 = xevent.xmotion.x;
			bandy1 = xevent.xmotion.y;
			if (ABS(bandx1-bandx0)*finalaspectratio >
				ABS(bandy1-bandy0)) {
			    bandy1 = SIGN(bandy1-bandy0)*ABS(bandx1-bandx0)*
				finalaspectratio + bandy0;
			} else {
			    bandx1 = SIGN(bandx1-bandx0)*ABS(bandy1-bandy0)/
				finalaspectratio + bandx0;
			}
			if (!banding) {
			    /* Don't start rubber-banding until the mouse
			       gets moved.  Otherwise a click messes up the
			       window */
			    if (ABS(bandx1-bandx0)>10 ||
				    ABS(bandy1-bandy0)>10) {
				banding = 1;
				XSetForeground(Xdp, Xgc, colors-1);
				XSetFunction(Xdp, Xgc, GXxor);
			    }
			}
			if (banding) {
			    XDrawRectangle(Xdp,Xw,Xgc,MIN(bandx0,bandx1),
				MIN(bandy0,bandy1), ABS(bandx1-bandx0),
				ABS(bandy1-bandy0));
			}
			XFlush(Xdp);
			break;
		    case ButtonRelease:
			done = 1;
			break;
		    }
		}
		if (!banding) {
		    break;
		}
		XDrawRectangle(Xdp,Xw,Xgc,MIN(bandx0,bandx1),
		    MIN(bandy0,bandy1), ABS(bandx1-bandx0),
		    ABS(bandy1-bandy0));
		if (bandx1==bandx0) {
		    bandx1 = bandx0+1;
		}
		if (bandy1==bandy0) {
		    bandy1 = bandy0+1;
		}
		zrotate = 0;
		zskew = 0;
		zbx = (MIN(bandx0,bandx1)-sxoffs)/dxsize;
		zby = (MIN(bandy0,bandy1)-syoffs)/dysize;
		zwidth = ABS(bandx1-bandx0)/dxsize;
		zdepth = zwidth;
		if (!inside_help) {
		    xbufkey = ENTER;
		}
		if (xlastcolor != -1) {
		    XSetForeground(Xdp, Xgc, xlastcolor);
		}
		XSetFunction(Xdp, Xgc, xlastfcn);
		XZoomWaiting = 1;
		drawbox(0);
		}
		break;
	    case Expose:
		if (!doesBacking) {
		int x,y,w,h;
		x = xevent.xexpose.x;
		y = xevent.xexpose.y;
		w = xevent.xexpose.width;
		h = xevent.xexpose.height;
		if (x+w>sxdots) {
		    w = sxdots-x;
		}
		if (y+h>sydots) {
		    h = sydots-y;
		}
		if (x<sxdots && y<sydots && w>0 && h>0) {

		    XPutImage(Xdp,Xw,Xgc,Ximage,xevent.xexpose.x,
			    xevent.xexpose.y, xevent.xexpose.x,
			    xevent.xexpose.y, xevent.xexpose.width,
			    xevent.xexpose.height);
		}
		}
		break;
	}
    }

    if (!xbufkey && editpal_cursor && !inside_help && lookatmouse == 3 &&
	    (dx != 0 || dy != 0)) {
	if (ABS(dx)>ABS(dy)) {
	    if (dx>0) {
		xbufkey = mousefkey[bnum][0]; /* right */
		dx--;
	    } else if (dx<0) {
		xbufkey = mousefkey[bnum][1]; /* left */
		dx++;
	    }
	} else {
	    if (dy>0) {
		xbufkey = mousefkey[bnum][2]; /* down */
		dy--;
	    } else if (dy<0) {
		xbufkey = mousefkey[bnum][3]; /* up */
		dy++;
	    }
	}
    }

}

#define w_root Xroot
#define dpy Xdp
#define scr Xdscreen
/*
 *----------------------------------------------------------------------
 *
 * pr_dwmroot --
 *
 *	Search for a dec window manager root window.
 *
 * Results:
 *	Returns the root window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static Window
pr_dwmroot(dpy, pwin)
Display *dpy;
Window  pwin;
{
   /* search for DEC Window Manager root */
   XWindowAttributes pxwa,cxwa;
   Window  root,parent,*child;
   unsigned int     i,nchild;

    if (!XGetWindowAttributes(dpy,pwin,&pxwa)) {
	printf("Search for root: XGetWindowAttributes failed\n");
	return RootWindow(dpy, scr);
    }
    if (XQueryTree(dpy,pwin,&root,&parent,&child,&nchild)) {
	for (i = 0; i < nchild; i++) {
	    if (!XGetWindowAttributes(dpy,child[i],&cxwa)) {
		printf("Search for root: XGetWindowAttributes failed\n");
		return RootWindow(dpy, scr);
	    }
	    if (pxwa.width == cxwa.width && pxwa.height == cxwa.height) {
		return(pr_dwmroot(dpy, child[i]));
	    }
	}
	return(pwin);
    } else {
	printf("xfractint: failed to find root window\n");
	return RootWindow(dpy, scr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FindRootWindow --
 *
 *	Find the root or virtual root window.
 *
 * Results:
 *	Returns the root window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static Window
FindRootWindow()
{
   int i;
   w_root = RootWindow(dpy,scr);
   w_root = pr_dwmroot(dpy, w_root); /* search for DEC wm root */

{  /* search for swm/tvtwm root (from ssetroot by Tom LaStrange) */
   Atom __SWM_VROOT = None;
   Window rootReturn, parentReturn, *children;
   unsigned int numChildren;

   __SWM_VROOT = XInternAtom(dpy, "__SWM_VROOT", False);
   XQueryTree(dpy, w_root, &rootReturn, &parentReturn, &children, &numChildren);
   for (i = 0; i < numChildren; i++) {
      Atom actual_type;
      int actual_format;
      unsigned long nitems, bytesafter;
      Window *newRoot = NULL;

      if (XGetWindowProperty (dpy, children[i], __SWM_VROOT,(long)0,(long)1,
         False, XA_WINDOW, &actual_type, &actual_format, &nitems, &bytesafter,
         (unsigned char **) &newRoot) == Success && newRoot) {
         w_root = *newRoot; break;
         }
      }
   }
   return w_root;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveRootPixmap --
 *
 *	Clean up old pixmap on the root window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pixmap is cleaned up.
 *
 *----------------------------------------------------------------------
 */
static void
RemoveRootPixmap()
{
    Atom prop,type;
    int format;
    unsigned long nitems,after;
    Pixmap *pm;

    prop = XInternAtom(Xdp,"_XSETROOT_ID",False);
    if (XGetWindowProperty(Xdp,Xroot,prop,(long)0,(long)1,1,AnyPropertyType,
	    &type, &format, &nitems, &after, (unsigned char **)&pm) ==
	    Success && nitems == 1) {
	if (type==XA_PIXMAP && format==32 && after==0) {
	    XKillClient(Xdp,(XID)*pm);
	    XFree((char *)pm);
	}
    }
}
static unsigned char *fontPtr = NULL;
/*
 *----------------------------------------------------------------------
 *
 * xgetfont --
 *
 *	Get an 8x8 font.
 *
 * Results:
 *	Returns a pointer to the bits.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
unsigned char *
xgetfont()
{
    XFontStruct *font_info;
    Pixmap font_pixmap;
    XImage *font_image;
    char str[8];
    int i,j,k,l;
    int width;
    fontPtr = (unsigned char *)malloc(128*8);
    bzero(fontPtr,128*8);
    XSetBackground(Xdp,Xgc,0);
    xlastcolor = -1;
    XSetForeground(Xdp,Xgc,1);
#define FONT "-*-*-medium-r-*-*-9-*-*-*-*-*-iso8859-*"
    font_info = XLoadQueryFont(Xdp,FONT);
    if (font_info==NULL) {
	printf("No %s\n", FONT);
    }
    if (font_info==NULL || font_info->max_bounds.width>8 ||
	    font_info->max_bounds.width != font_info->min_bounds.width) {
	printf("Bad font: %s\n", FONT);
	sleep(2);
	font_info = XLoadQueryFont(Xdp,"6x12");
    }
    if (font_info==NULL) return NULL;
    width = font_info->max_bounds.width;
    if (font_info->max_bounds.width>8 ||
	    font_info->max_bounds.width != font_info->min_bounds.width) {
	printf("Bad font\n");
	return NULL;
    }
    font_pixmap = XCreatePixmap(Xdp,Xw,64,8,Xdepth);

    for (i=0;i<128;i+=8) {
	for (j=0;j<8;j++) {
	    str[j] = i+j;
	}
	XDrawImageString(Xdp,font_pixmap,Xgc,0,8,str,8);
	font_image = XGetImage(Xdp,font_pixmap,0,0,64,8,AllPlanes,XYPixmap);
	for (j=0;j<8;j++) {
	    for (k=0;k<8;k++) {
		for (l=0;l<width;l++) {
		    if (XGetPixel(font_image,j*width+l,k)) {
			fontPtr[(i+j)*8+k] = (fontPtr[(i+j)*8+k]<<1)|1;
		    } else {
			fontPtr[(i+j)*8+k] = (fontPtr[(i+j)*8+k]<<1);
		    }
		}
	    }
	}
	XDestroyImage(font_image);
    }
    return fontPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * shell_to_dos --
 *
 *	Exit to a unix shell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Goes to shell
 *
 *----------------------------------------------------------------------
 */
#define SHELL "/bin/csh"
void
shell_to_dos()
{
    __sighandler_t sigint;
    char *shell;
    char *argv[2];
    int pid, donepid;

    sigint = (__sighandler_t)signal(SIGINT, SIG_IGN);
    shell = getenv("SHELL");
    if (shell==NULL) {
	shell = SHELL;
    }
    argv[0] = shell;
    argv[1] = NULL;

    /* Clean up the window */

    if (!simple_input) {
	fcntl(0,F_SETFL,old_fcntl);
    }
    mvcur(0,COLS-1, LINES-1,0);
    nocbreak();
    echo();
    endwin();

    /* Fork the shell */

    pid = fork();
    if (pid < 0) {
	perror("fork to shell");
    }
    if (pid==0) {
	execvp(shell, argv);
	perror("fork to shell");
	exit(1);
    }

    /* Wait for the shell to finish */

    while (1) {
	donepid = wait(0);
	if (donepid<0 || donepid==pid) break;
    }

    /* Go back to curses mode */

    initscr();
    curwin = stdscr;
    cbreak();
    noecho();
    if (!simple_input) {
	old_fcntl = fcntl(0,F_GETFL);
	fcntl(0,F_SETFL,FNDELAY);
    }

    signal(SIGINT, (__sighandler_t)sigint);
    putchar('\n');
}

/*
 *----------------------------------------------------------------------
 *
 * schedulealarm --
 *
 *	Start the refresh alarm
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Starts the alarm.
 *
 *----------------------------------------------------------------------
 */
#define DRAW_INTERVAL 6
void
schedulealarm(soon)
int soon;
{
    if (!fastmode) return;
    signal(SIGALRM, (__sighandler_t)setredrawscreen);
    if (soon) {
	alarm(1);
    } else {
	alarm(DRAW_INTERVAL);
    }
    alarmon = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * setredrawscreen --
 *
 *	Set the screen refresh flag
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the flag.
 *
 *----------------------------------------------------------------------
 */
static void
setredrawscreen()
{
    doredraw = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * redrawscreen --
 *
 *	Refresh the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redraws the screen.
 *
 *----------------------------------------------------------------------
 */
void
redrawscreen()
{
    if (alarmon) {
	XPutImage(Xdp,Xw,Xgc,Ximage,0,0,0,0,sxdots,sydots);
	if (onroot) {
	    XPutImage(Xdp,Xpixmap,Xgc,Ximage,0,0,0,0,sxdots,sydots);
	}
	alarmon = 0;
    }
    doredraw = 0;
}

