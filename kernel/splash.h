/* LiteKern X — the boot splash (Phase 3 §5): the logo and a progress bar
 * while the kernel starts, instead of the scrolling log.
 *
 * `make SPLASH=logo` (the default) or `make SPLASH=log` (the scrolling
 * boot log, as before). Behind the splash the log keeps being recorded (the
 * Log app shows it), and a panic always shows it, on red. */
#ifndef LKX_SPLASH_H
#define LKX_SPLASH_H

/* Show it (after the screen and the console are up), if it's the chosen one. */
void splash_start(void);

/* Move the bar: 0..100. Does nothing without a splash. */
void splash_progress(int percent);

/* 1 while the splash is up. */
int splash_active(void);

/* The desktop takes over from here. */
void splash_end(void);

#endif
