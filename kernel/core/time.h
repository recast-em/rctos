/* time.h - Kalenderrechnung für die Uhr (UTC). */
#ifndef RC_TIME_H
#define RC_TIME_H

#include "kernel.h"

struct rc_date {
    int year;
    unsigned month, day, hour, minute, second;
};

void rc_date_from_unix(int64_t t, struct rc_date *d);

#endif
