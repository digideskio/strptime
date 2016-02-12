/**********************************************************************

  time.c -

  $Author$
  created at: Tue Dec 28 14:31:59 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#include "ruby.h"

#include <time.h>
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

/* rbtime_timespec_new */
typedef uint64_t WIDEVALUE;
typedef WIDEVALUE wideval_t;

#ifndef HAVE_RB_TIME_TIMESPEC_NEW
# if defined(PACKED_STRUCT_UNALIGNED) /* 2.2 */
PACKED_STRUCT_UNALIGNED(struct vtm {
    VALUE year;	/* 2000 for example.  Integer. */
    VALUE subsecx;     /* 0 <= subsecx < TIME_SCALE.  possibly Rational. */
    VALUE utc_offset;  /* -3600 as -01:00 for example.  possibly Rational. */
    const char *zone;  /* "JST", "EST", "EDT", etc. */
    uint16_t yday : 9; /* 1..366 */
    uint8_t mon : 4;   /* 1..12 */
    uint8_t mday : 5;  /* 1..31 */
    uint8_t hour : 5;  /* 0..23 */
    uint8_t min : 6;   /* 0..59 */
    uint8_t sec : 6;   /* 0..60 */
    uint8_t wday : 3;  /* 0:Sunday, 1:Monday, ..., 6:Saturday 7:init */
    uint8_t isdst : 2; /* 0:StandardTime 1:DayLightSavingTime 3:init */
});
PACKED_STRUCT_UNALIGNED(struct time_object {
    wideval_t timew; /* time_t value * TIME_SCALE.  possibly Rational. */
    struct vtm vtm;
    uint8_t gmt : 3; /* 0:utc 1:localtime 2:fixoff 3:init */
    uint8_t tm_got : 1;
});
# else /* 2.0.0~2.1 */
struct vtm {
    VALUE year; /* 2000 for example.  Integer. */
    int mon; /* 1..12 */
    int mday; /* 1..31 */
    int hour; /* 0..23 */
    int min; /* 0..59 */
    int sec; /* 0..60 */
    VALUE subsecx; /* 0 <= subsecx < TIME_SCALE.  possibly Rational. */
    VALUE utc_offset; /* -3600 as -01:00 for example.  possibly Rational. */
    int wday; /* 0:Sunday, 1:Monday, ..., 6:Saturday */
    int yday; /* 1..366 */
    int isdst; /* 0:StandardTime 1:DayLightSavingTime */
    const char *zone; /* "JST", "EST", "EDT", etc. */
};
struct time_object {
    wideval_t timew; /* time_t value * TIME_SCALE.  possibly Rational. */
    struct vtm vtm;
    int gmt; /* 0:utc 1:localtime 2:fixoff */
    int tm_got;
};
# endif

VALUE
rb_time_timespec_new(const struct timespec *ts, int offset)
{
    VALUE obj = rb_time_nano_new(ts->tv_sec, ts->tv_nsec);
    if (-86400 < offset && offset <  86400) { /* fixoff */
	struct time_object *tobj;
	tobj = DATA_PTR(obj);
	tobj->tm_got = 0;
	tobj->gmt = 2;
	tobj->vtm.utc_offset = INT2FIX(offset);
	tobj->vtm.zone = NULL;
    }
    else if (offset == INT_MAX) { /* localtime */
    }
    else if (offset == INT_MAX-1) { /* UTC */
	struct time_object *tobj;
	tobj = DATA_PTR(obj);
	tobj->tm_got = 0;
	tobj->gmt = 1;
    }
    else {
	rb_raise(rb_eArgError, "utc_offset out of range");
    }

    return obj;
}

void
rb_timespec_now(struct timespec *ts)
{
#ifdef HAVE_CLOCK_GETTIME
    if (clock_gettime(CLOCK_REALTIME, ts) == -1) {
	rb_sys_fail("clock_gettime");
    }
#else
    {
	struct timeval tv;
	if (gettimeofday(&tv, 0) < 0) {
	    rb_sys_fail("gettimeofday");
	}
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;
    }
#endif
}
#endif

/* localtime_with_gmtoff_zone */
#ifdef HAVE_GMTIME_R
#define rb_gmtime_r(t, tm) gmtime_r((t), (tm))
#define rb_localtime_r(t, tm) localtime_r((t), (tm))
#else
static inline struct tm *
rb_gmtime_r(const time_t *tp, struct tm *result)
{
    struct tm *t = gmtime(tp);
    if (t) *result = *t;
    return t;
}

static inline struct tm *
rb_localtime_r(const time_t *tp, struct tm *result)
{
    struct tm *t = localtime(tp);
    if (t) *result = *t;
    return t;
}
#endif

static struct tm *
rb_localtime_r2(const time_t *t, struct tm *result)
{
#if defined __APPLE__ && defined __LP64__
    if (*t != (time_t)(int)*t) return NULL;
#endif
    result = rb_localtime_r(t, result);
#if defined(HAVE_MKTIME) && defined(LOCALTIME_OVERFLOW_PROBLEM)
    if (result) {
	long gmtoff1 = 0;
	long gmtoff2 = 0;
	struct tm tmp = *result;
	time_t t2;
#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
	gmtoff1 = result->tm_gmtoff;
#endif
	t2 = mktime(&tmp);
#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
	gmtoff2 = tmp.tm_gmtoff;
#endif
	if (*t + gmtoff1 != t2 + gmtoff2) result = NULL;
    }
#endif
    return result;
}
#define LOCALTIME(tm, result) (tzset(), rb_localtime_r2((tm), &(result)))

#if !defined(HAVE_STRUCT_TM_TM_GMTOFF)
static struct tm *
rb_gmtime_r2(const time_t *t, struct tm *result)
{
    result = rb_gmtime_r(t, result);
#if defined(HAVE_TIMEGM) && defined(LOCALTIME_OVERFLOW_PROBLEM)
    if (result) {
	struct tm tmp = *result;
	time_t t2 = timegm(&tmp);
	if (*t != t2) result = NULL;
    }
#endif
    return result;
}
#define GMTIME(tm, result) rb_gmtime_r2((tm), &(result))
#endif

struct tm *
localtime_with_gmtoff_zone(const time_t *t, struct tm *result, long *gmtoff,
			   const char **zone)
{
    struct tm tm;

    if (LOCALTIME(t, tm)) {
#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
	*gmtoff = tm.tm_gmtoff;
#else
	struct tm *u, *l;
	long off;
	struct tm tmbuf;
	l = &tm;
	u = GMTIME(t, tmbuf);
	if (!u) return NULL;
	if (l->tm_year != u->tm_year)
	    off = l->tm_year < u->tm_year ? -1 : 1;
	else if (l->tm_mon != u->tm_mon)
	    off = l->tm_mon < u->tm_mon ? -1 : 1;
	else if (l->tm_mday != u->tm_mday)
	    off = l->tm_mday < u->tm_mday ? -1 : 1;
	else
	    off = 0;
	off = off * 24 + l->tm_hour - u->tm_hour;
	off = off * 60 + l->tm_min - u->tm_min;
	off = off * 60 + l->tm_sec - u->tm_sec;
	*gmtoff = off;
#endif

	*result = tm;
	return result;
    }
    return NULL;
}

#define NDIV(x,y) (-(-((x)+1)/(y))-1)
#define DIV(n,d) ((n)<0 ? NDIV((n),(d)) : (n)/(d))

static int
leap_year_p(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static const int common_year_yday_offset[] = {
    -1,
    -1 + 31,
    -1 + 31 + 28,
    -1 + 31 + 28 + 31,
    -1 + 31 + 28 + 31 + 30,
    -1 + 31 + 28 + 31 + 30 + 31,
    -1 + 31 + 28 + 31 + 30 + 31 + 30,
    -1 + 31 + 28 + 31 + 30 + 31 + 30 + 31,
    -1 + 31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
    -1 + 31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
    -1 + 31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
    -1 + 31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30
      /* 1    2    3    4    5    6    7    8    9    10   11 */
};
static const int leap_year_yday_offset[] = {
    -1,
    -1 + 31,
    -1 + 31 + 29,
    -1 + 31 + 29 + 31,
    -1 + 31 + 29 + 31 + 30,
    -1 + 31 + 29 + 31 + 30 + 31,
    -1 + 31 + 29 + 31 + 30 + 31 + 30,
    -1 + 31 + 29 + 31 + 30 + 31 + 30 + 31,
    -1 + 31 + 29 + 31 + 30 + 31 + 30 + 31 + 31,
    -1 + 31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
    -1 + 31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
    -1 + 31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30
      /* 1    2    3    4    5    6    7    8    9    10   11 */
};

static const int common_year_days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static const int leap_year_days_in_month[] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

time_t
timegm_noleapsecond(struct tm *tm)
{
    long tm_year = tm->tm_year;
    int tm_yday = tm->tm_mday;
    if (leap_year_p(tm_year + 1900))
	tm_yday += leap_year_yday_offset[tm->tm_mon];
    else
	tm_yday += common_year_yday_offset[tm->tm_mon];

    /*
     *  `Seconds Since the Epoch' in SUSv3:
     *  tm_sec + tm_min*60 + tm_hour*3600 + tm_yday*86400 +
     *  (tm_year-70)*31536000 + ((tm_year-69)/4)*86400 -
     *  ((tm_year-1)/100)*86400 + ((tm_year+299)/400)*86400
     */
    return tm->tm_sec + tm->tm_min*60 + tm->tm_hour*3600 +
	   (time_t)(tm_yday +
		    (tm_year-70)*365 +
		    DIV(tm_year-69,4) -
		    DIV(tm_year-1,100) +
		    DIV(tm_year+299,400))*86400;
}

void
tm_add_offset(struct tm *tm, long diff)
{
    int sign, tsec, tmin, thour, tday;

    if (diff < 0) {
	sign = -1;
	diff = -diff;
    }
    else {
	sign = 1;
    }
    tsec = diff % 60;
    diff = diff / 60;
    tmin = diff % 60;
    diff = diff / 60;
    thour = diff % 24;
    diff = diff / 24;

    if (sign < 0) {
	tsec = -tsec;
	tmin = -tmin;
	thour = -thour;
    }

    tday = 0;

    if (tsec) {
	tsec += tm->tm_sec;
	if (tsec < 0) {
	    tsec += 60;
	    tmin -= 1;
	}
	if (60 <= tsec) {
	    tsec -= 60;
	    tmin += 1;
	}
	tm->tm_sec = tsec;
    }

    if (tmin) {
	tmin += tm->tm_min;
	if (tmin < 0) {
	    tmin += 60;
	    thour -= 1;
	}
	if (60 <= tmin) {
	    tmin -= 60;
	    thour += 1;
	}
	tm->tm_min = tmin;
    }

    if (thour) {
	thour += tm->tm_hour;
	if (thour < 0) {
	    thour += 24;
	    tday = -1;
	}
	if (24 <= thour) {
	    thour -= 24;
	    tday = 1;
	}
	tm->tm_hour = thour;
    }

    if (tday) {
	if (tday < 0) {
	    if (tm->tm_mon == 1 && tm->tm_mday == 1) {
		tm->tm_mday = 31;
		tm->tm_mon = 12; /* December */
		tm->tm_year = tm->tm_year - 1;
	    }
	    else if (tm->tm_mday == 1) {
		const int *days_in_month = leap_year_p(tm->tm_year)
					       ? leap_year_days_in_month
					       : common_year_days_in_month;
		tm->tm_mon--;
		tm->tm_mday = days_in_month[tm->tm_mon - 1];
	    }
	    else {
		tm->tm_mday--;
	    }
	}
	else {
	    int leap = leap_year_p(tm->tm_year);
	    if (tm->tm_mon == 12 && tm->tm_mday == 31) {
		tm->tm_year = tm->tm_year + 1;
		tm->tm_mon = 1; /* January */
		tm->tm_mday = 1;
	    }
	    else if (tm->tm_mday ==
		     (leap ? leap_year_days_in_month
			   : common_year_days_in_month)[tm->tm_mon - 1]) {
		tm->tm_mon++;
		tm->tm_mday = 1;
	    }
	    else {
		tm->tm_mday++;
	    }
	}
    }
}

