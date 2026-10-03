/* LiteKern X — the screen saver: glowing ribbons, like Windows' "Ribbons".
 *
 * After the desktop has had no key or touchpad input for a while (`make
 * SCREENSAVER=<seconds>`, 300 by default, 0 = never), the screen fades to
 * black and four glowing ribbons sweep and loop across it, each sliding
 * through the colours, thickening and thinning as if twisting, and fading
 * away at its tail. Each head follows its own smooth path (sums of slow sine
 * waves), so the pattern never quite repeats. Any key or touch brings the
 * desktop back; that input is only a wake-up and isn't passed on.
 *
 * Cheap on purpose:
 *   - between frames the CPU sleeps (`hlt`); the CMOS clock's periodic
 *     interrupt (rtc0, RTC_SET_RATE) wakes it 32 times a second. The kernel
 *     has no other timer, and the desktop's own animations spin instead
 *   - a ribbon is a chain of soft dots, combined by taking the brighter of
 *     what's there and the dot ("lighten"), which doesn't depend on drawing
 *     order: so a frame only adds the few new dots at each head, and
 *     redraws from scratch just the small patch where a tail fades (every
 *     other frame), never the whole screen
 *   - the dots are drawn once per size when it starts, then only looked up
 * It logs how much of the time went on drawing when it stops. */
#ifndef LKX_SCREENSAVER_H
#define LKX_SCREENSAVER_H

#include <stdint.h>

/* Every pass of the input loop, before it sleeps: starts the screen saver
 * once the idle time is up, and draws a frame when one is due. */
void screensaver_poll(void);

/* 1 while it's on screen: the input loop may sleep even if the desktop
 * says it's busy (nothing of it is visible). */
int screensaver_active(void);

/* Input arrived. If the screen saver is showing, it stops and this returns 1:
 * the input was only a wake-up, so drop it. Otherwise it restarts the idle
 * countdown and returns 0. */
int screensaver_input(void);

/* Show it now (the self-tests and screenshots). */
void screensaver_start(void);

/* How long without input before it starts, in seconds (0: never). It
 * starts as `make SCREENSAVER=`; the Settings app changes it. Setting it
 * restarts the countdown. */
uint32_t screensaver_timeout(void);
void screensaver_set_timeout(uint32_t seconds);

#endif
