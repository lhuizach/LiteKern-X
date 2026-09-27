/* LiteKern X — kernel error codes. Functions return 0 or a positive count on
 * success and a negative code on failure (e.g. -ENOSYS). Numbers follow
 * Linux so they read familiarly in logs. */
#ifndef LKX_ERRNO_H
#define LKX_ERRNO_H

#define EIO     5       /* I/O error */
#define ENOMEM  12      /* out of memory / table full */
#define EFAULT  14      /* bad address (e.g. a user pointer the kernel won't touch) */
#define EBUSY   16      /* device or resource busy */
#define ENODEV  19      /* no such device, or device not usable */
#define EINVAL  22      /* invalid argument */
#define ENOSYS  38      /* operation not supported by this driver */

/* "ENOSYS" for -ENOSYS or ENOSYS; "E?" for anything unknown. */
const char *errno_name(int err);

#endif
