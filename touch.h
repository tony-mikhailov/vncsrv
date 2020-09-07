#pragma once

enum MouseAction
{
    MouseDrag = -1,
    MouseRelease,
    MousePress
};

int init_touch(const char *touch_device, int vnc_rotate);
void cleanup_touch();
void injectTouchEvent(enum MouseAction mouseAction, int x, int y, struct fb_var_screeninfo *scrinfo);

void trim5SysMenu(struct fb_var_screeninfo *scrinfo);
void trim5Start(struct fb_var_screeninfo *scrinfo);
void trim5Info(struct fb_var_screeninfo *scrinfo);
void trim5Home(struct fb_var_screeninfo *scrinfo);
void trim5Menu(struct fb_var_screeninfo *scrinfo);