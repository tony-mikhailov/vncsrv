#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h> /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>

/* libvncserver */
#include "rfb/rfb.h"

#include "touch.h"
#include "logging.h"

//static char TOUCH_DEVICE[256] = "/dev/input/event2";
static int touchfd = -1;

static int xmin, xmax;
static int ymin, ymax;
static int rotate;
static int trkg_id = -1;
static volatile int last_x = -1;
static volatile int last_y = -1;
static volatile int last_x_k = -1;
static volatile int last_y_k = -1;

int init_touch(const char *touch_device, int vnc_rotate)
{
    info_print("Initializing touch device %s ...\n", touch_device);
    struct input_absinfo info;
    if ((touchfd = open(touch_device, O_RDWR)) == -1)
    {
        error_print("cannot open touch device %s\n", touch_device);
        return 0;
    }
    // Get the Range of X and Y
    if (ioctl(touchfd, EVIOCGABS(ABS_X), &info))
    {
        error_print("cannot get ABS_X info, %s\n", strerror(errno));
        return 0;
    }
    xmin = info.minimum;
    xmax = info.maximum;
    if (ioctl(touchfd, EVIOCGABS(ABS_Y), &info))
    {
        error_print("cannot get ABS_Y, %s\n", strerror(errno));
        return 0;
    }
    ymin = info.minimum;
    ymax = info.maximum;
    rotate = vnc_rotate;

    info_print("  x:(%d %d)  y:(%d %d) \n", xmin, xmax, ymin, ymax);
    return 1;
}

void cleanup_touch()
{
    if (touchfd != -1)
    {
        close(touchfd);
    }
}

void injectTouchEvent(enum MouseAction mouseAction, int x, int y, struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;


    if (x == last_x) {
        x = x + last_x_k;
        if (x > 799) x-=2;
    }

    if (y == last_y) {
        y = y + last_y_k;
        if (y > 599) y-=2;
    }

    last_x = x;
    last_y = y;

    last_y_k *= -1;

    //info_print("x %d, y %d\n", x, y);

    // Calculate the final x and y
    /* Fake touch screen always reports zero */
    // if (xmin != 0 && xmax != 0 && ymin != 0 && ymax != 0)
    // {
    //     x = xmin + (x * (xmax - xmin)) / (scrinfo->xres);
    //     y = ymin + (y * (ymax - ymin)) / (scrinfo->yres);
    // }sd

    memset(&ev, 0, sizeof(ev));

    bool sendPos;
    bool sendTouch;
    int trkIdValue;
    int touchValue;

    switch (mouseAction)
    {
    case MousePress:
        sendPos = true;
        sendTouch = true;
        trkIdValue = ++trkg_id;
        touchValue = 1;
        break;
    case MouseRelease:
//!tony        sendPos = false;
        sendPos = true;
        sendTouch = true;
        trkIdValue = -1;
        touchValue = 0;
        break;
    case MouseDrag:
        sendPos = true;
        sendTouch = true;
        touchValue = 1;
        trkIdValue = 0;
//!tony        sendTouch = false;
        break;
    default:
        error_print("invalid mouse action\n");
        exit(EXIT_FAILURE);
    }

    if (sendTouch)
    {
        // Then send a ABS_MT_TRACKING_ID
        gettimeofday(&ev.time, 0);
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = trkIdValue;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send a BTN_TOUCH
        gettimeofday(&ev.time, 0);
        ev.type = EV_KEY;
        ev.code = BTN_TOUCH;
        ev.value = touchValue;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }
    }

    if (sendPos)
    {
        // Then send a ABS_MT_POSITION_X
        gettimeofday(&ev.time, 0);
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = x;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send a ABS_MT_POSITION_Y
        gettimeofday(&ev.time, 0);
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = y;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send the X
        gettimeofday(&ev.time, 0);
        ev.type = EV_ABS;
        ev.code = ABS_X;
        ev.value = x;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }

        // Then send the Y
        gettimeofday(&ev.time, 0);
        ev.type = EV_ABS;
        ev.code = ABS_Y;
        ev.value = y;
        if (write(touchfd, &ev, sizeof(ev)) < 0)
        {
            error_print("write event failed, %s\n", strerror(errno));
        }
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}

void trim5SysMenu(struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    int x1, x2, y1, y2;

    x1 = 300;
    y1 = 535;
    x2 = 515;
    y2 = 535;

    // Then send a ABS_MT_TRACKING_ID
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = ++trkg_id;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a KEY_MENU
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_MENU;
    ev.value = 1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
/*
    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x2;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y2;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x2;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y2;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_UNTOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
*/

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

        // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_MENU;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}


void trim5Menu(struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    int x1, x2, y1, y2;

    x1 = 300;
    y1 = 535;

    // Then send a ABS_MT_TRACKING_ID
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = ++trkg_id;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F4;
    ev.value = 1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_UNTOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F4;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}

void trim5Home(struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    int x1, x2, y1, y2;

    x1 = 85;
    y1 = 535;

    // Then send a ABS_MT_TRACKING_ID
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = ++trkg_id;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F2;
    ev.value = 1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_UNTOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F2;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}


void trim5Info(struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    int x1, x2, y1, y2;

    x1 = 515;
    y1 = 535;

    // Then send a ABS_MT_TRACKING_ID
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = ++trkg_id;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F1;
    ev.value = 1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_UNTOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F1;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}

void trim5Start(struct fb_var_screeninfo *scrinfo)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));

    int x1, x2, y1, y2;

    x1 = 720;
    y1 = 535;

    // Then send a ABS_MT_TRACKING_ID
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = ++trkg_id;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F3;
    ev.value = 1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a ABS_MT_POSITION_X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send a ABS_MT_POSITION_Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the X
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }
    // Then send the Y
    gettimeofday(&ev.time, 0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y1;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Then send a BTN_UNTOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

        // Then send a BTN_TOUCH
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = KEY_F3;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(touchfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

 //   debug_print("injectTouchEvent (screen(%d,%d) -> touch(%d,%d), mouse=%d)\n", x, y, mouseAction);
}
