// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

#define NANOSEC_PER_SEC (1000000000ULL)
#define NANOSEC_PER_MIN (60ULL * NANOSEC_PER_SEC)
#define NANOSEC_PER_HR  (60ULL * NANOSEC_PER_MIN)
#define NANOSEC_PER_DAY (24ULL * NANOSEC_PER_HR)

#define LEAP_YEAR(y) (((y) % 4 == 0 && (y) % 100 != 0) || ((y) % 400 == 0)) // formula of Gregorian leap year rule

static const int day_count_of_a_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const char * const month_names[] = {
    "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"
};

static int days_in_a_year(int year) {
    if (LEAP_YEAR(year)) {
        return 366;
    }
    return 365;
}

static int days_in_a_month(int year, int month_idx) {
    if (month_idx == 1 && LEAP_YEAR(year)) {
        return 29;
    }
    return day_count_of_a_month[month_idx];
}

void main(int argc, char *argv[]) {
    int fd;
    long n;
    (void)argc;
    (void)argv;

    fd = _open(-1, "/dev/rtc");
    if (fd < 0) {
        dprintf(CONSOLEOUT, "date: cannot open rtc\n");
        _exit();
    }

    unsigned long long timestamp;
    n = _read(fd, &timestamp, sizeof(timestamp)); // current Unix timestamp of the RTC device will be returned in nanoseconds
    _close(fd);
    if (n < 0) {
        dprintf(CONSOLEOUT, "date: cannot read rtc\n");
        _exit();
    }

    // convertion of nanoseconds since 1970-01-01-00:00:00GMT into required calendar representation (ignoring leap seconds)
    unsigned long long days = timestamp / NANOSEC_PER_DAY;
    unsigned long long remainder = timestamp % NANOSEC_PER_DAY;
    int hour = remainder / NANOSEC_PER_HR;
    remainder %= NANOSEC_PER_HR;
    int minute = remainder / NANOSEC_PER_MIN;
    remainder %= NANOSEC_PER_MIN;
    int second = remainder / NANOSEC_PER_SEC;
    int year = 1970;
    while (days >= (unsigned long long)days_in_a_year(year)) {
        days -= days_in_a_year(year);
        year++;
    }
    int month_idx = 0;
    while (days >= (unsigned long long)days_in_a_month(year, month_idx)) {
        days -= days_in_a_month(year, month_idx);
        month_idx++;
    }
    int day = (int)days + 1;
    // format: DD month YYYY HH:MM:SS (for example, 01 May 2026 17:59:00)
    dprintf(STDOUT, "%d%d %s %d %d%d:%d%d:%d%d\n", day / 10, day % 10, month_names[month_idx], year, hour / 10, hour % 10, minute / 10, minute % 10, second / 10, second % 10);
    _exit();
}
