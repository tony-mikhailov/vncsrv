#ifndef KEYBOARD_H
#define KEYBOARD_H

#pragma once

int init_kbd(const char *);
void cleanup_kbd();

void injectKeyEvent(uint16_t, uint16_t);
void injectKeyEventSeq(uint16_t, int trim5);
int keysym2scancode(rfbKeySym key, rfbClientPtr cl);

static const int SMH4_KEY_COUNT = 13;

struct key_stat {
    int code;
    int down;
} key_stat_tag;

static struct key_stat keys[13] = {
    {0xff51,0},
    {0xff53,0},
    {0xff52,0},
    {0xff54,0},
    {0xffc7,0},
    {0xff1b,0},
    {0xFF0D,0},
    {0xFFBE,0},
    {0xFFBF,0},
    {0xFFC0,0},
    {0xFFC1,0},
    {0xFFC2,0},
    {0xFFC3,0}
};

#endif //KEYBOARD_H
