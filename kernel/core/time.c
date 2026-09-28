/* time.c - Tage seit 1970 in ein Datum umrechnen (Verfahren nach H. Hinnant). */
#include "time.h"

void rc_date_from_unix(int64_t t, struct rc_date *d)
{
    int64_t days = t / 86400, rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    d->hour = (unsigned)(rem / 3600);
    d->minute = (unsigned)(rem / 60 % 60);
    d->second = (unsigned)(rem % 60);

    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d->day = doy - (153 * mp + 2) / 5 + 1;
    d->month = mp < 10 ? mp + 3 : mp - 9;
    d->year = (int)(yoe + era * 400) + (d->month <= 2);
}
