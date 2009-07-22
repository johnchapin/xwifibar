/*  xwifibar - an unobtrusive wireless signal strength meter
 *  Copyright (C) 2009 John Chapin <john.chapin@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <sys/time.h>
#include <signal.h>

#define BW_DEF 2
#define PI_DEF 1

#define TOP 0
#define LEFT 1
#define BOTTOM 2
#define RIGHT 3

/* global variables */
const char *PROGNAME = "xwifibar";
const char *PROGVERS = "0.1.7";
const char *PROGAUTH = "John Chapin <john.chapin@gmail.com>";
const char *PROCPATH = "/proc/net/wireless";
const float LQMAX = 92.0;

char *ifname = "eth1";

int bar_edge = RIGHT;
int bar_width = 2;
int poll_interval = 1;
int on_top = 0;
int link_quality = 0;

/* global X11 variables */
Display *display;
Window win_bar, win_stat;
GC bar_gc;
XWindowAttributes root_attrib, bar_attrib;
char *s_on = "green";
char *s_off = "red";
unsigned long c_on, c_off;
int win_stat_exists = 0;

/* function prototypes */
void show_version();
void show_usage();
void init_display();
Status alloc_color(char *, unsigned long *);
void redraw_bar();
void draw_stat();
void kill_stat();
void sig_handle();
void stat_poll();

int
main(int argc, char **argv) {
	extern char *optarg;
	extern int optind;
	char *tmp;
	int c;
	char dir;
	XEvent xevent;
	struct itimerval timer;

	/* parse command line options */
	while (1) {
		c = getopt(argc,argv,"vhw:p:ai:");
		if (c < -0)
			break;
		switch(c) {
			case 'v':
				show_version();
				exit(0);
			break;
			case 'h':
				show_usage();
				exit(0);
			break;
			case 'w':
				bar_width = (atoi(optarg) < 1) ? bar_width : atoi(optarg);
			break;
			case 'p':
				poll_interval = (atoi(optarg) < 1) ? poll_interval : atoi(optarg);
			break;
			case 'a':
				on_top = 1;
			break;
			case 'i':
				tmp = rindex(optarg,'/') ? rindex(optarg,'/')+1 : optarg;
				ifname = (char *)malloc(strlen(tmp));
				strncpy(ifname,tmp,strlen(tmp));
			break;
			
		}

	}
	
	/* get screen edge */
	if (optind < argc) {
		dir = argv[optind][0];
		switch (dir) {	
			case 't':
				bar_edge = TOP;
			break;
			case 'b':
				bar_edge = BOTTOM;
			break;
			case 'l':
				bar_edge = LEFT;
			break;
			case 'r':
				bar_edge = RIGHT;
			break;
		}
	}

	init_display();

	/* set up timer */
	timer.it_interval.tv_sec = poll_interval;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = poll_interval;
	timer.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL,&timer,NULL);
	signal(SIGALRM,sig_handle);

	/* loop forever, deal with X events */
	stat_poll();
	redraw_bar();
	XSelectInput(display,win_bar,VisibilityChangeMask|ExposureMask|EnterWindowMask|LeaveWindowMask);
	while(1) {
		XNextEvent(display,&xevent);
		switch(xevent.type) {
			case Expose:
				redraw_bar();
			break;
			case VisibilityNotify:
				if (on_top) {
					XRaiseWindow(display,win_bar);
				}
			break;
			case EnterNotify:
				draw_stat();
			break;
			case LeaveNotify:
				kill_stat();
			break;
		}
	}
}

void
show_version() {
	printf("\n%s, version %s, by %s\n",PROGNAME,PROGVERS,PROGAUTH);
	printf("This program is Free Software, released under the GPL License.\n\n");
}

void
show_usage() {
	printf("\n%s [-a] [-w width] [-p interval] [-i interface] [top | bottom | left | right]\n",PROGNAME);
	printf("\t-a\talways on top (default NO)\n");
	printf("\t-p <P>\tproc polling interval (default 1 second)\n");
	printf("\t-w <W>\tbar width (default 2 pixels)\n");
	printf("\t-i <I>\twireless interface name (default 'eth1')\n");
	printf("\t-v\tshow version\n");
	printf("\t-h\tshow help\n\n");
}

void
init_display() {
	Window root;
	XSetWindowAttributes bar_swattrib;
	
	if ((display = XOpenDisplay(NULL)) == NULL) {
		fprintf(stderr,"%s: couldn't open display.\n",PROGNAME);
		exit(1);
	}

	root = DefaultRootWindow(display);
	XGetWindowAttributes(display,root,&root_attrib);

	if (!alloc_color(s_on,&c_on) || !alloc_color(s_off,&c_off)) {
		fprintf(stderr,"%s: couldn't allocate color resources.\n",PROGNAME);
		exit(1);

	}

	/* set bar window attributes */
	switch (bar_edge) {
		case TOP:
			bar_attrib.x = root_attrib.x;
			bar_attrib.y = root_attrib.y;
			bar_attrib.width = root_attrib.width;
			bar_attrib.height = bar_width;
		break;
		case BOTTOM:
			bar_attrib.x = root_attrib.x;
			bar_attrib.y = root_attrib.height - bar_width;
			bar_attrib.width = root_attrib.width;
			bar_attrib.height = bar_width;
		break;
		case LEFT:
			bar_attrib.x = root_attrib.x;
			bar_attrib.y = root_attrib.y;
			bar_attrib.width = bar_width;
			bar_attrib.height = root_attrib.height;
		break;
		case RIGHT:
			bar_attrib.x = root_attrib.width - bar_width;
			bar_attrib.y = root_attrib.y;
			bar_attrib.width = bar_width;
			bar_attrib.height = root_attrib.height;
		break;
	}
	bar_attrib.border_width = 0;

	win_bar = XCreateSimpleWindow(display,root,bar_attrib.x,bar_attrib.y,bar_attrib.width,bar_attrib.height,bar_attrib.border_width,BlackPixel(display,0),BlackPixel(display,0));

	/* remove titlebar */
	bar_swattrib.override_redirect = True;
	XChangeWindowAttributes(display,win_bar,CWOverrideRedirect,&bar_swattrib);

	bar_gc = XCreateGC(display,win_bar,0,0);

	XMapWindow(display,win_bar);
}

Status
alloc_color(char *name, unsigned long *pixel) {
/* this function borrowed entirely from xbattbar */
	XColor color, exact;
	int status;
	status = XAllocNamedColor(display,DefaultColormap(display,0),name,&color,&exact);
	*pixel = color.pixel;
	return(status);
}

void
sig_handle() {
	stat_poll();
	redraw_bar();
	if (win_stat_exists) {
		kill_stat();
		draw_stat();
	}
}

void
redraw_bar() {
	int lq_size;
	
	XSetForeground(display,bar_gc,c_on);
	if (! bar_edge % 2) {
		lq_size = (link_quality/LQMAX) * bar_attrib.width;	
		XFillRectangle(display,win_bar,bar_gc,0,0,lq_size,bar_attrib.height);
		XSetForeground(display,bar_gc,c_off);
		XFillRectangle(display,win_bar,bar_gc,lq_size,0,bar_attrib.width - lq_size,bar_attrib.height);
	} else {
		lq_size = (link_quality/LQMAX) * bar_attrib.height;	
		XFillRectangle(display,win_bar,bar_gc,0,bar_attrib.height - lq_size,bar_attrib.width,lq_size);
		XSetForeground(display,bar_gc,c_off);
		XFillRectangle(display,win_bar,bar_gc,0,0,bar_attrib.width,bar_attrib.height - lq_size);
	}

	XFlush(display);
}

void
draw_stat() {
	XWindowAttributes stat_attrib;
	XSetWindowAttributes stat_swattrib;
	GC stat_gc;
	XFontStruct *font;
	char mesg[26];
	int tw;
	int txm = 10;
	int tym = 5;

	font = XLoadQueryFont(display,"fixed");
	snprintf(mesg,26,"Link Quality: %2d%% (%2d/%2d)",(int)(100*(link_quality/LQMAX)),link_quality,(int)LQMAX);

	tw = XTextWidth(font,mesg,strlen(mesg));

	/* create stat window */
	stat_attrib.border_width = 2;
	stat_attrib.x = (root_attrib.width/2) - ((tw/2) + txm);
	stat_attrib.y = (root_attrib.height/2) - (((font->ascent + font->descent)/2) + tym);
	stat_attrib.width = tw + (2 * txm);
	stat_attrib.height = (font->ascent + font->descent) + (2 * tym);

	win_stat = XCreateSimpleWindow(display,DefaultRootWindow(display),stat_attrib.x,stat_attrib.y,stat_attrib.width,stat_attrib.height,stat_attrib.border_width,BlackPixel(display,0),WhitePixel(display,0));

	/* remove titlebar */
	stat_swattrib.override_redirect = True;
	XChangeWindowAttributes(display,win_stat,CWOverrideRedirect,&stat_swattrib);

	XMapWindow(display,win_stat);

	/* create another graphics context */
	stat_gc = XCreateGC(display,win_stat,0,0);

	XSetForeground(display,stat_gc,BlackPixel(display,0));
	XSetFont(display,stat_gc,font->fid);
	XDrawString(display,win_stat,stat_gc,txm,tym+font->ascent,mesg,strlen(mesg));
	XFreeFont(display,font);

	XFlush(display);
	
	win_stat_exists = 1;
}

void
kill_stat() {
	if (win_stat_exists) {
		XDestroyWindow(display,win_stat);
	}
	win_stat_exists = 0;
}

void
stat_poll() {
	FILE *fd;
	char tmp[255];
	char *p;
	
	fd = fopen(PROCPATH,"r");
	if (fd == NULL) {
		fprintf(stderr,"%s: couldn't open /proc/net/wireless for reading.\n",PROGNAME);
	}

	/* skip two lines of headers */
	fgets(tmp,255,fd);
	fgets(tmp,255,fd);
	
	while (fgets(tmp,255,fd)) {
		p = tmp + strspn(tmp," ");
		if (!strncmp(p,ifname,strlen(ifname))) {
			p += strspn(p," ") + 1;
			sscanf(p,"%*s %*s %d %*s %*s %*s %*s %*s %*s %*s  ",&link_quality); 
		}
	}	

	fclose(fd);
}
