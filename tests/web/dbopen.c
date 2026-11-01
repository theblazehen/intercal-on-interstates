/* dbopen — open the ActiveCassette database (IOI_DATABASE, IOI_APP_DIR) and
 * print "tables=N rows=R ms=T": the table count, the total row count and the
 * milliseconds the first query took (including first-open setup). */
#include "ioi_web.h"

#include <stdio.h>
#include <time.h>

int main(void)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    unsigned tables = 0;
    unsigned long rows = 0;
    for (;; tables++) {
        struct ioi_db_query q = {.table = tables, .shape = IOI_DB_HOW_MANY};
        uint32_t n;
        int rc = ioi_db_select(&q, &n);
        if (tables == 0)
            clock_gettime(CLOCK_MONOTONIC, &b);
        if (rc == IOI_DB_E_TABLE)
            break;
        if (rc) {
            fprintf(stderr, "dbopen: table %u: %s\n", tables, ioi_db_errmsg());
            return 1;
        }
        rows += ioi_val_to_number(n, NULL);
    }
    printf("tables=%u rows=%lu ms=%ld\n", tables, rows,
           (long)((b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000));
    return 0;
}
