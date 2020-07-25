#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "rfb/keysym.h"

#include "keyboard.h"
#include "logging.h"

//static char KBD_DEVICE[256] = "/dev/input/event1";
static int kbdfd = -1;

int init_kbd(const char *kbd_device)
{
    info_print("Initializing keyboard device %s ...\n", kbd_device);
    if ((kbdfd = open(kbd_device, O_RDWR)) == -1)
    {
        error_print("cannot open kbd device %s\n", kbd_device);
        return 0;
    }
    else
    {
        return 1;
    }
}

void cleanup_kbd()
{
    if (kbdfd != -1)
    {
        close(kbdfd);
    }
}

void injectKeyEventSeq(uint16_t code, uint16_t value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    gettimeofday(&ev.time, 0);

    ev.type = EV_KEY;
    ev.code = 106;
    ev.value = value;
    if (write(kbdfd, &ev, sizeof(ev)) < 0) {
      printf("write event failed, %s\n", strerror(errno));
    }

    ev.type = EV_MSC;
    ev.code = MSC_SCAN;
    ev.value = 0x8b;
    if (write(kbdfd, &ev, sizeof(ev)) < 0) {
      printf("write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(kbdfd, &ev, sizeof(ev)) < 0) {
        printf("write event failed, %s\n", strerror(errno));
    }

    printf("injectKey (%d, %d)\n", code, value);
}


void injectKeyEvent(uint16_t code, uint16_t value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if (write(kbdfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time, 0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if (write(kbdfd, &ev, sizeof(ev)) < 0)
    {
        error_print("write event failed, %s\n", strerror(errno));
    }

    debug_print("injectKey (%d, %d)\n", code, value);
}

int keysym2scancode(rfbKeySym key, rfbClientPtr cl)
{
    int scancode = 0;

    int code = (int)key;
    switch (code)
    {
    case 0xff51:
        scancode = KEY_LEFT;
        break;
    case 0xff53:
        scancode = KEY_RIGHT;
        break;
    case 0xff52:
        scancode = KEY_UP;
        break;
    case 0xff54:
        scancode = KEY_DOWN;
        break;
    case 0xffc7://f10 start btn
        scancode = KEY_F7;
        break;
    case 0xff1b:
        scancode = 1;
        break;
    case 0xFF0D:
        scancode = KEY_ENTER;
        break;
    case 0xFFBE:
        scancode = KEY_F1;
        break; 
    case 0xFFBF:
        scancode = KEY_F2;
        break; 
    case 0xFFC0:
        scancode = KEY_F3;
        break; 
    case 0xFFC1:
        scancode = KEY_F4;
        break; 
    case 0xFFC2:
        scancode = KEY_F5;
        break; 
    case 0xFFC3:
        scancode = KEY_F6;
        break; 
    }

    return scancode;
}
