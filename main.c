/*
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 *This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This project is an adaptation of the original fbvncserver for the iPAQ
 * and Zaurus and then 
 * rewrited by Anton Mikhailov for SMH4, TRIM5, MATRIX by Segnetics industrial PLCs.
 * 
 * 07/2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h> 

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>


#include "rfb/rfb.h"
#include "rfb/keysym.h"

#include "touch.h"
#include "keyboard.h"
#include "logging.h"

 #define CLOCKID CLOCK_REALTIME
#define SIG SIGRTMIN

#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE); \
                         } while (0)

#define LOG_FPS

#define BITS_PER_SAMPLE 5
#define SAMPLES_PER_PIXEL 2

static char fb_device[256] = "/dev/fb0";
static char touch_device[256] = "/dev/input/ts";
static char kbd_device[256] = "/dev/input/kbd";

static struct fb_var_screeninfo scrinfo;
static int fbfd = -1;
static int kbdfd = -1;
static unsigned short int *fbmmap = MAP_FAILED;
static unsigned short int *vncbuf;
static unsigned short int *fbbuf;

static int vnc_port = 5900;
static int vnc_rotate = 0;
static rfbScreenInfoPtr server;
static size_t bytespp;
static unsigned int bits_per_pixel;
static unsigned int frame_size;
static int trim5 = 0;
int verbose = 0;

#define UNUSED(x) (void)(x)

static struct varblock_t
{
    int min_i;
    int min_j;
    int max_i;
    int max_j;
    int r_offset;
    int g_offset;
    int b_offset;
    int rfb_xres;
    int rfb_maxy;
} varblock;

static void init_fb(void)
{
    size_t pixels;

    if ((fbfd = open(fb_device, O_RDONLY)) == -1)
    {
        error_print("cannot open fb device %s\n", fb_device);
        exit(EXIT_FAILURE);
    }


    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
    {
        error_print("ioctl error\n");
        exit(EXIT_FAILURE);
    }

    pixels = scrinfo.xres * scrinfo.yres;
    bytespp = scrinfo.bits_per_pixel / 8;
    bits_per_pixel = scrinfo.bits_per_pixel;
    frame_size = pixels * bits_per_pixel / 8;
    fbmmap = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, fbfd, 0);

    if (fbmmap == MAP_FAILED)
    {
        error_print("mmap failed\n");
        exit(EXIT_FAILURE);
    }
}

static void cleanup_fb(void)
{
    if (fbfd != -1)
    {
        close(fbfd);
        fbfd = -1;
    }
}

static int cnt = 0;

static void update_screen(void);

int wait_time(int time_to_wait) // ms
{
    static struct timeval now2 = {0, 0}, then = {0, 0};
    double elapsed, dnow, dthen;
    gettimeofday(&now2, NULL);
    
    dnow = now2.tv_sec + (now2.tv_usec / 1000000.0);
    dthen = then.tv_sec + (then.tv_usec / 1000000.0);
    
    elapsed = dnow - dthen;
    if (elapsed > time_to_wait)
        memcpy((char *)&then, (char *)&now2, sizeof(struct timeval));
    return elapsed > time_to_wait;
}

static int pass_update_screen = 1;

static void print_siginfo(siginfo_t *si) {
    timer_t *tidp;
    int or;

    tidp = si->si_value.sival_ptr;

    printf("    sival_ptr = %p; ", si->si_value.sival_ptr);
    printf("    *sival_ptr = 0x%lx\n", (long) *tidp);

    or = timer_getoverrun(*tidp);
    if (or == -1)
        errExit("timer_getoverrun");
    else
        printf("    overrun count = %d\n", or);
}

static void handler(int sig, siginfo_t *si, void *uc) {
    /* Note: calling printf() from a signal handler is not safe
        (and should not be done in production programs), since
        printf() is not async-signal-safe; see signal-safety(7).
        Nevertheless, we use printf() here as a simple way of
        showing that the handler was called. */

    printf("Caught signal %d\n", sig);
    print_siginfo(si);
    signal(sig, SIG_IGN);
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    static struct timeval now3 = {0, 0};
    static struct timeval then3 = {0, 0};

    double elapsed, dnow, dthen;
    
    gettimeofday(&now3, NULL);
    dnow = now3.tv_sec + (now3.tv_usec / 1000000.0);
    dthen = then3.tv_sec + (then3.tv_usec / 1000000.0);

    elapsed = dnow - dthen;

    memcpy((char *)&then3, (char *)&now3, sizeof(struct timeval));
    if (elapsed < 0.1 && (down == 1)) {
        rfbProcessEvents(server, 50000);
        return;
    }

    int scancode;

    int k;
    int left_key;
    int right_key;

    for (k = 0; k < SMH4_KEY_COUNT; ++k) {

        if (keys[k].code == key && keys[k].down == down) {
            rfbProcessEvents(server, 50000);
            update_screen();
            
            rfbProcessEvents(server, 500000);
            return;
        } else {
            keys[k].down = down;
        }

    }

    if (key == 0xFFC7) {
        injectKeyEventSeq(down, trim5);
        return;
    }

    scancode = keysym2scancode(key, cl);

    if (scancode) 
    {
        injectKeyEvent(scancode, down);
	}
    ++cnt;
}

static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
    UNUSED(cl);
  
    static int pressed = 0;
    static int pressed_x = 0;
    static int pressed_y = 0;
    if (buttonMask == 0 && ! (pressed == 1)) {   
        rfbProcessEvents(server, 1000000);
    } 

    if (buttonMask & 1)
    {
        if (pressed == 1)
        {
            rfbProcessEvents(server, 1000000);
        }
        else
        {
            pressed = 1;
            pressed_x = x;
            pressed_y = y;
	    
            injectTouchEvent(MousePress, x, y, &scrinfo);
        }
    }
    if (buttonMask == 0)
    {
        if (pressed == 1)
        {
            pressed = 0;
            injectTouchEvent(MouseRelease, x, y, &scrinfo);
        }
    }
}

static void init_fb_server(int argc, char **argv, rfbBool enable_touch)
{
    int rbytespp = bits_per_pixel == 1 ? 1 : bytespp;
    int rframe_size = bits_per_pixel == 1 ? frame_size * 8 : frame_size;

    vncbuf = malloc(rframe_size);
    assert(vncbuf != NULL);
    memset(vncbuf, bits_per_pixel == 1 ? 0xFF : 0x00, rframe_size);

    fbbuf = calloc(frame_size, 1);
    assert(fbbuf != NULL);

    server = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres, BITS_PER_SAMPLE, SAMPLES_PER_PIXEL, rbytespp);
    assert(server != NULL);

    //passwords
    static const char* passwords[4]={"1", "pwd", "arsie", 0};
    
    server->authPasswdData = (void*)passwords;
    server->passwordCheck=rfbCheckPasswordByList;

    server->desktopName = "Mikhailov's vncsrv";
    server->frameBuffer = (char *)vncbuf;
    server->alwaysShared = TRUE;
    server->httpDir = NULL;
    server->port = vnc_port;

    server->kbdAddEvent = keyevent;
    //server->ptrAddEvent = ptrevent;

    if (enable_touch)
    {
        server->ptrAddEvent = ptrevent;
    }

    rfbInitServer(server);

    rfbMarkRectAsModified(server, 0, 0, scrinfo.xres, scrinfo.yres);

    varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - BITS_PER_SAMPLE;
    varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - BITS_PER_SAMPLE;
    varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - BITS_PER_SAMPLE;
    varblock.rfb_xres = scrinfo.yres;
    varblock.rfb_maxy = scrinfo.xres - 1;
}

// sec
#define LOG_TIME 5

int timeToLogFPS()
{
    static struct timeval now = {0, 0}, then = {0, 0};
    double elapsed, dnow, dthen;
    gettimeofday(&now, NULL);
    dnow = now.tv_sec + (now.tv_usec / 1000000.0);
    dthen = then.tv_sec + (then.tv_usec / 1000000.0);
    elapsed = dnow - dthen;
    if (elapsed > LOG_TIME)
        memcpy((char *)&then, (char *)&now, sizeof(struct timeval));
    return elapsed > LOG_TIME;
}

#define COLOR_MASK (((1 << BITS_PER_SAMPLE) << 1) - 1)
#define PIXEL_FB_TO_RFB(p, r_offset, g_offset, b_offset) \
    ((p >> r_offset) & COLOR_MASK) | (((p >> g_offset) & COLOR_MASK) << BITS_PER_SAMPLE) | (((p >> b_offset) & COLOR_MASK) << (2 * BITS_PER_SAMPLE))

static void update_screen(void)
{
/*
if (pass_update_screen == 0 && !timeToLogFPS()) {
   	info_print("pass_update_screen");
	pass_update_screen = 1;
	return;
   }
*/
   static int frames = 0;
   frames++;
   if (timeToLogFPS())
   {
       double fps = frames / LOG_TIME;
       info_print("  fps: %f\n", fps);
       frames = 0;
   }

    varblock.min_i = varblock.min_j = 9999;
    varblock.max_i = varblock.max_j = -1;

    if (vnc_rotate == 0 && bits_per_pixel == 24)
    {
        uint8_t *f = (uint8_t *)fbmmap; /* -> framebuffer         */
        uint8_t *c = (uint8_t *)fbbuf;  /* -> compare framebuffer */
        uint8_t *r = (uint8_t *)vncbuf; /* -> remote framebuffer  */

        if (memcmp(fbmmap, fbbuf, frame_size) != 0)
        {
            int y;
            for (y = 0; y < (int)scrinfo.yres; y++)
            {
                int x;
                for (x = 0; x < (int)scrinfo.xres; x++)
                {
                    uint32_t pixel = *(uint32_t *)f & 0x00FFFFFF;
                    uint32_t comp = *(uint32_t *)c & 0x00FFFFFF;

                    if (pixel != comp)
                    {
                        *(c + 0) = *(f + 0);
                        *(c + 1) = *(f + 1);
                        *(c + 2) = *(f + 2);
                        uint32_t rem = PIXEL_FB_TO_RFB(pixel,
                                                       varblock.r_offset, varblock.g_offset, varblock.b_offset);
                        *(r + 0) = (uint8_t)((rem >> 0) & 0xFF);
                        *(r + 1) = (uint8_t)((rem >> 8) & 0xFF);
                        *(r + 2) = (uint8_t)((rem >> 16) & 0xFF);

                        if (x < varblock.min_i)
                            varblock.min_i = x;
                        else if (x > varblock.max_i)
                            varblock.max_i = x;

                        if (y > varblock.max_j)
                            varblock.max_j = y;
                        else if (y < varblock.min_j)
                            varblock.min_j = y;
                    }

                    f += bytespp;
                    c += bytespp;
                    r += bytespp;
                }
            }
        }
    }
    else if (vnc_rotate == 0 && bits_per_pixel == 1)
    {
        uint8_t *f = (uint8_t *)fbmmap; /* -> framebuffer         */
        uint8_t *c = (uint8_t *)fbbuf;  /* -> compare framebuffer */
        uint8_t *r = (uint8_t *)vncbuf; /* -> remote framebuffer  */

        int xstep = 8;
        if (memcmp(fbmmap, fbbuf, frame_size) != 0)
        {
            int y;
            for (y = 0; y < (int)scrinfo.yres; y++)
            {
                int x;
                for (x = 0; x < (int)scrinfo.xres; x += xstep)
                {
                    uint8_t pixels = *f;

                    if (pixels != *c)
                    {
                        *c = pixels;
                        int bit;
                        for (bit = 0; bit < 8; bit++)
                        {
                            *(r + bit) = ((pixels >> (7 - bit)) & 0x1) ? 0x00 : 0xFF;
                        }

                        int x2 = x + xstep - 1;
                        if (x < varblock.min_i)
                            varblock.min_i = x;
                        else if (x2 > varblock.max_i)
                            varblock.max_i = x2;

                        if (y > varblock.max_j)
                            varblock.max_j = y;
                        else if (y < varblock.min_j)
                            varblock.min_j = y;
                    }

                    f += 1;
                    c += 1;
                    r += 8;
                }
            }
        }
    }
    else if (vnc_rotate == 0)
    {
        uint32_t *f = (uint32_t *)fbmmap; /* -> framebuffer         */
        uint32_t *c = (uint32_t *)fbbuf;  /* -> compare framebuffer */
        uint32_t *r = (uint32_t *)vncbuf; /* -> remote framebuffer  */

        if (memcmp(fbmmap, fbbuf, frame_size) != 0)
        {
            int xstep = 4 / bytespp;

            int y;
            for (y = 0; y < (int)scrinfo.yres; y++)
            {
                /* Compare every 1/2/4 pixels at a time */
                int x;
                for (x = 0; x < (int)scrinfo.xres; x += xstep)
                {
                    uint32_t pixel = *f;

                    if (pixel != *c)
                    {
                        *c = pixel;
                        if (bytespp == 4)
                        {
                            *r = PIXEL_FB_TO_RFB(pixel,
                                                 varblock.r_offset, varblock.g_offset, varblock.b_offset);
                        }
                        else if (bytespp == 2)
                        {
                            *r = PIXEL_FB_TO_RFB(pixel,
                                                 varblock.r_offset, varblock.g_offset, varblock.b_offset);

                            uint32_t high_pixel = (0xffff0000 & pixel) >> 16;
                            uint32_t high_r = PIXEL_FB_TO_RFB(high_pixel, varblock.r_offset, varblock.g_offset, varblock.b_offset);
                            *r |= (0xffff & high_r) << 16;
                        }
                        else if (bytespp == 1)
                        {
                            *r = pixel;
                        }
                        else
                        {
                            // TODO
                        }

                        int x2 = x + xstep - 1;
                        if (x < varblock.min_i)
                            varblock.min_i = x;
                        else if (x2 > varblock.max_i)
                            varblock.max_i = x2;

                        if (y > varblock.max_j)
                            varblock.max_j = y;
                        else if (y < varblock.min_j)
                            varblock.min_j = y;
                    }

                    f++;
                    c++;
                    r++;
                }
            }
        }
    } else if (bits_per_pixel == 16) {
        uint16_t *f = (uint16_t *)fbmmap; /* -> framebuffer         */
        uint16_t *c = (uint16_t *)fbbuf;  /* -> compare framebuffer */
        uint16_t *r = (uint16_t *)vncbuf; /* -> remote framebuffer  */

        switch (vnc_rotate)
        {
        case 0:
        case 180:
            server->width = scrinfo.xres;
            server->height = scrinfo.yres;
            server->paddedWidthInBytes = scrinfo.xres * bytespp;
            break;

        case 90:
        case 270:
            server->width = scrinfo.yres;
            server->height = scrinfo.xres;
            server->paddedWidthInBytes = scrinfo.yres * bytespp;
            break;
        }

        if (memcmp(fbmmap, fbbuf, frame_size) != 0) {
            int y;
            for (y = 0; y < (int)scrinfo.yres; y++)
            {
                /* Compare every pixels at a time */
                int x;
                for (x = 0; x < (int)scrinfo.xres; x++)
                {
                    uint16_t pixel = *f;

                    if (pixel != *c)
                    {
                        int x2, y2;

                        *c = pixel;
                        switch (vnc_rotate)
                        {
                        case 0:
                            x2 = x;
                            y2 = y;
                            break;

                        case 90:
                            x2 = scrinfo.yres - 1 - y;
                            y2 = x;
                            break;

                        case 180:
                            x2 = scrinfo.xres - 1 - x;
                            y2 = scrinfo.yres - 1 - y;
                            break;

                        case 270:
                            x2 = y;
                            y2 = scrinfo.xres - 1 - x;
                            break;
                        default:
                            error_print("rotation is invalid\n");
                            exit(EXIT_FAILURE);
                        }

                        r[y2 * server->width + x2] = PIXEL_FB_TO_RFB(pixel, varblock.r_offset, varblock.g_offset, varblock.b_offset);

                        if (x2 < varblock.min_i)
                            varblock.min_i = x2;
                        else
                        {
                            if (x2 > varblock.max_i)
                                varblock.max_i = x2;

                            if (y2 > varblock.max_j)
                                varblock.max_j = y2;
                            else if (y2 < varblock.min_j)
                                varblock.min_j = y2;
                        }
                    }

                    f++;
                    c++;
                }
            }
        }
    } else {
        exit(EXIT_FAILURE);
    }

    if (varblock.min_i < 9999) {
        if (varblock.max_i < 0)
            varblock.max_i = varblock.min_i;

        if (varblock.max_j < 0)
            varblock.max_j = varblock.min_j;
	
        rfbMarkRectAsModified(server, varblock.min_i, varblock.min_j, varblock.max_i + 2, varblock.max_j + 1);
	
        rfbProcessEvents(server, 10000);
    }
}


void print_usage(char **argv)
{
	//todo: impl
}

void init_timer(int sec, long long usec) {
    timer_t timerid;
    struct sigevent sev;
    struct itimerspec its;
    long long freq_nanosecs;
    sigset_t mask;
    struct sigaction sa;

    /* Establish handler for timer signal */

    printf("Establishing handler for signal %d\n", SIG);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1)
        errExit("sigaction");

    /* Block timer signal temporarily */

    printf("Blocking signal %d\n", SIG);
    sigemptyset(&mask);
    sigaddset(&mask, SIG);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1)
        errExit("sigprocmask");

    /* Create the timer */

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1)
        errExit("timer_create");

    printf("timer ID is 0x%lx\n", (long) timerid);

    /* Start the timer */

    freq_nanosecs = usec;
    its.it_value.tv_sec = freq_nanosecs / 1000000000;
    its.it_value.tv_nsec = freq_nanosecs % 1000000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
        errExit("timer_settime");

    /* Sleep for a while; meanwhile, the timer may expire
        multiple times */

    printf("Sleeping for %d seconds\n", sec);
    sleep(sec);

    /* Unlock the timer signal, so that timer notification
        can be delivered */

    printf("Unblocking signal %d\n", SIG);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
        errExit("sigprocmask");

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    int newfd;

    close(1);
    newfd = open("/dev/null", O_WRONLY);


    static int proc_time = 500000;
    static int fps = 0;    	
    if (argc > 1)
    {
        int i = 1;
        while (i < argc)
        {
            if (*argv[i] == '-')
            {
                switch (*(argv[i] + 1))
                {
                case 'h':
                    print_usage(argv);
                    exit(0);
                    break;
                case 'f':
                    i++;
                    fps=1;
                    break;
                case 's':
                    i++;
                    fps=0;
                    break;
                case 't':
                    i++;
                    trim5 = 1;
                    break;
                }
            }
            i++;
        }
    }
    

    init_fb();
    if (strlen(kbd_device) > 0) {
        kbdfd  = init_kbd(kbd_device);
     }
 
    rfbBool enable_touch = FALSE;
    if (strlen(touch_device) > 0) {
        int ret = init_touch(touch_device, vnc_rotate);
        enable_touch = (ret > 0);
    }
    init_fb_server(argc, argv, enable_touch);

    proc_time = 500000;

    for(;;) {
        while (server->clientHead == NULL) {
           rfbProcessEvents(server, 100000);
        }
        rfbProcessEvents(server, (fps == 0 ? 330000 : 50000) );
        update_screen();
    }

    cleanup_fb();
    cleanup_kbd();
    cleanup_touch();
}
