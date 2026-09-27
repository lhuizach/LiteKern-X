#include "kernel/errno.h"

const char *errno_name(int err)
{
    switch (err < 0 ? -err : err) {
    case EIO:    return "EIO";
    case ENOMEM: return "ENOMEM";
    case EFAULT: return "EFAULT";
    case EBUSY:  return "EBUSY";
    case ENODEV: return "ENODEV";
    case EINVAL: return "EINVAL";
    case ENOSYS: return "ENOSYS";
    default:     return "E?";
    }
}
