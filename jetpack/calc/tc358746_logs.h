#ifndef TC358746_LOGS_H
#define TC358746_LOGS_H

#include <stdio.h>

#ifndef log_error
#define log_error(fmt, args...) fprintf(stderr, "[ERROR] " fmt, ##args)
#endif
#ifndef log_info
/* #define log_info(fmt, args...)  fprintf(stderr, "[INFO] " fmt, ##args) */
#endif

#endif
