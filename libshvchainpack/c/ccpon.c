#include "ccpon.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

// see http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap04.html#tag_04_15
// see https://stackoverflow.com/questions/16647819/timegm-cross-platform
// see https://www.boost.org/doc/libs/1_62_0/boost/chrono/io/time_point_io.hpp
static inline int is_leap(int y)
{
	return (y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0);
}

static inline int32_t days_from_0(int32_t year)
{
	year--;
	return 365 * year + (year / 400) - (year/100) + (year / 4);
}

static inline int32_t days_from_1970(int32_t year)
{
	static int32_t days_from_0_to_1970 = 0;
	if(days_from_0_to_1970 == 0)
		days_from_0_to_1970 = days_from_0(1970);
	return days_from_0(year) - days_from_0_to_1970;
}

static inline int days_from_1jan(int year, int month, int mday)
{
	static const int days[2][12] =
	{
		{ 0,31,59,90,120,151,181,212,243,273,304,334},
		{ 0,31,60,91,121,152,182,213,244,274,305,335}
	};

	return days[is_leap(year)][month] + mday - 1;
}

time_t ccpon_timegm(struct tm *tm)
{
	// leap seconds are not part of Posix
	time_t res = 0;
	int year = tm->tm_year + 1900;
	int month = tm->tm_mon; // 0 - 11
	int mday = tm->tm_mday; // 1 - 31
	res = days_from_1970(year);
	res += days_from_1jan(year, month, mday);
	res *= 24;
	res += tm->tm_hour;
	res *= 60;
	res += tm->tm_min;
	res *= 60;
	res += tm->tm_sec;
	return res;
}

// Returns year/month/day triple in civil calendar
// Preconditions:  z is number of days since 1970-01-01 and is in the range:
//                   [numeric_limits<Int>::min(), numeric_limits<Int>::max()-719468].
static void civil_from_days(long z, int *py, unsigned *pm, unsigned *pd)
{
	int y;
	unsigned m;
	unsigned d;
	z += 719468;
	const long era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (z - era * 146097);          // [0, 146096]
	const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
	y = ((long)yoe) + era * 400;
	const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
	const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
	d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
	m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
	y += (m <= 2);
	--m;
	if(py)
		*py = y;
	if(pm)
		*pm = m;
	if(pd)
		*pd = d;
}

void ccpon_gmtime(int64_t epoch_sec, struct tm *tm)
{
	if (!tm)
		return;

	const long seconds_in_day = 3600 * 24;
	long days_since_epoch = (epoch_sec / seconds_in_day);
	long hms = epoch_sec - seconds_in_day * days_since_epoch;
	if (hms < 0) {
		days_since_epoch -= 1;
		hms = seconds_in_day + hms;
	}

	int32_t y;
	unsigned m, d;
	civil_from_days(days_since_epoch, &y, &m, &d);
	tm->tm_year = y - 1900;
	tm->tm_mon = m;
	tm->tm_mday = d;

	tm->tm_hour = hms / 3600;
	const int ms = hms % 3600;
	tm->tm_min = ms / 60;
	tm->tm_sec = ms % 60;

	tm->tm_isdst = -1;
}

static const char CCPON_STR_NULL[] = "null";
static const char CCPON_STR_TRUE[] = "true";
static const char CCPON_STR_FALSE[] = "false";
static const char CCPON_STR_IMAP_BEGIN[] = "i{";
static const char CCPON_DATE_TIME_BEGIN[] = "d\"";

#define CCPON_C_KEY_DELIM ':'
#define CCPON_C_FIELD_DELIM ','
#define CCPON_C_LIST_BEGIN '['
#define CCPON_C_LIST_END ']'
#define CCPON_C_ARRAY_END ']'
#define CCPON_C_MAP_BEGIN '{'
#define CCPON_C_MAP_END '}'
#define CCPON_C_META_BEGIN '<'
#define CCPON_C_META_END '>'
//#define CCPON_C_DECIMAL_END 'n'
#define CCPON_C_UNSIGNED_END 'u'

#ifdef FORCE_NO_LIBRARY

static void	*memcpy(void *dst, const void *src, size_t n)
{
    unsigned int i;
    uint8_t *d=(uint8_t*)dst, *s=(uint8_t*)src;
    for (i=0; i<n; i++)
    {
        *d++ = *s++;
    }
    return dst;
}

#endif

//============================   P A C K   =================================
void ccpcp_pack_context_init (ccpcp_pack_context* pack_context, void *data, size_t length, ccpcp_pack_overflow_handler hpo)
{
	pack_context->start = pack_context->current = (char*)data;
	pack_context->end = pack_context->start + length;
	pack_context->err_no = 0;
	pack_context->handle_pack_overflow = hpo;
	pack_context->err_no = CCPCP_RC_OK;
	pack_context->cpon_options.indent = NULL;
	pack_context->cpon_options.json_output = 0;
	pack_context->nest_count = 0;
	pack_context->custom_context = NULL;
}

void ccpon_pack_copy_str(ccpcp_pack_context *pack_context, const char *str)
{
	size_t len = strlen(str);
	ccpcp_pack_copy_bytes(pack_context, str, len);
}

static void start_block(ccpcp_pack_context* pack_context)
{
	pack_context->nest_count++;
}

static void indent_element(ccpcp_pack_context* pack_context)
{
	if(pack_context->cpon_options.indent) {
		for (int i = 0; i < pack_context->nest_count; ++i) {
			ccpon_pack_copy_str(pack_context, pack_context->cpon_options.indent);
		}
	}
}

static void end_block(ccpcp_pack_context* pack_context)
{
	pack_context->nest_count--;
	if(pack_context->cpon_options.indent) {
		if(pack_context->cpon_options.indent)
			ccpcp_pack_copy_bytes(pack_context, "\n", 1);
		indent_element(pack_context);
	}
}

void ccpon_pack_uint(ccpcp_pack_context* pack_context, uint64_t i)
{
	if (pack_context->err_no)
		return;

	// at least 21 characters for 64-bit types.
	static const unsigned LEN = 32;
	char str[LEN];
	int n = snprintf(str, LEN, "%llu", (unsigned long long)i);
	if(n < 0) {
		pack_context->err_no = CCPCP_RC_LOGICAL_ERROR;
		return;
	}
	ccpcp_pack_copy_bytes(pack_context, str, n);
	if(!pack_context->cpon_options.json_output)
		ccpcp_pack_copy_byte(pack_context, 'u');
}

void ccpon_pack_int(ccpcp_pack_context* pack_context, int64_t i)
{
	if (pack_context->err_no)
		return;

	// at least 21 characters for 64-bit types.
	static const unsigned LEN = 32;
	char str[LEN];
	int n = snprintf(str, LEN, "%lld", (long long)i);
	if(n < 0) {
		pack_context->err_no = CCPCP_RC_LOGICAL_ERROR;
		return;
	}
	ccpcp_pack_copy_bytes(pack_context, str, n);
}


void ccpon_pack_decimal(ccpcp_pack_context *pack_context, int64_t mantisa, int exponent)
{
	// at least 21 characters for 64-bit types.
	static const int LEN = 64;
	char buff[LEN];
	int n = ccpcp_decimal_to_string(buff, LEN, mantisa, exponent);
	if(n < 0) {
		pack_context->err_no = CCPCP_RC_LOGICAL_ERROR;
		return;
	}

	ccpcp_pack_copy_bytes(pack_context, buff, n);
}

void ccpon_pack_double(ccpcp_pack_context* pack_context, double d)
{
	if (pack_context->err_no)
		return;

	// at least 21 characters for 64-bit types.
	static const unsigned LEN = 32;
	char str[LEN];
	int n = snprintf(str, LEN, "%lg", d);
	if(n < 0) {
		pack_context->err_no = CCPCP_RC_LOGICAL_ERROR;
		return;
	}
	for (int i = 0; i < n; ++i)
		if(str[i] == ',')
			str[i] = '.';
	int has_dot = 0;
	int has_e = 0;
	for (int i = 0; i < n; ++i) {
		if(str[i] == 'e') {
			has_e = 1;
			break;
		}
		if(str[i] == '.') {
			has_dot = 1;
			break;
		}
	}
	if(!has_dot && !has_e) {
		str[n++] = '.';
	}
	char *p = ccpcp_pack_reserve_space(pack_context, (unsigned)n);
	if(p) {
		memcpy(p, str, (unsigned)n);
	}
}

void ccpon_pack_date_time(ccpcp_pack_context *pack_context, int64_t epoch_msecs, int min_from_utc)
{
	/// ISO 8601 with msecs extension
	ccpcp_pack_copy_bytes(pack_context, CCPON_DATE_TIME_BEGIN, sizeof (CCPON_DATE_TIME_BEGIN) - 1);
	ccpon_pack_date_time_str(pack_context, epoch_msecs, min_from_utc, CCPON_Auto, true);
	ccpcp_pack_copy_bytes(pack_context, "\"", 1);
}

void ccpon_pack_date_time_str(ccpcp_pack_context *pack_context, int64_t epoch_msecs, int min_from_utc, ccpon_msec_policy msec_policy, bool with_tz)
{
	struct tm tm;
	ccpon_gmtime(epoch_msecs / 1000, &tm);
	static const unsigned LEN = 32;
	char str[LEN];
	int n = snprintf(str, LEN, "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	if(n > 0)
		ccpcp_pack_copy_bytes(pack_context, str, n);
	int msec = epoch_msecs % 1000;
	if((msec > 0 && msec_policy == CCPON_Auto) || msec_policy == CCPON_Always) {
		n = snprintf(str, LEN, ".%03d", msec);
		if(n > 0)
			ccpcp_pack_copy_bytes(pack_context, str, n);
	}
	if(with_tz) {
		if(min_from_utc == 0) {
			ccpcp_pack_copy_bytes(pack_context, "Z", 1);
		}
		else {
			if(min_from_utc < 0) {
				ccpcp_pack_copy_bytes(pack_context, "-", 1);
				min_from_utc = -min_from_utc;
			}
			else {
				ccpcp_pack_copy_bytes(pack_context, "+", 1);
			}
			if(min_from_utc%60)
				n = snprintf(str, LEN, "%02d%02d", min_from_utc/60, min_from_utc%60);
			else
				n = snprintf(str, LEN, "%02d", min_from_utc/60);
			if(n > 0)
				ccpcp_pack_copy_bytes(pack_context, str, n);
		}
	}
}

void ccpon_pack_null(ccpcp_pack_context* pack_context)
{
	if (pack_context->err_no)
		return;
	ccpcp_pack_copy_bytes(pack_context, CCPON_STR_NULL, sizeof(CCPON_STR_NULL) - 1);
}

static void ccpon_pack_true (ccpcp_pack_context* pack_context)
{
	if (pack_context->err_no)
		return;
	ccpcp_pack_copy_bytes(pack_context, CCPON_STR_TRUE, sizeof(CCPON_STR_TRUE) - 1);
}

static void ccpon_pack_false (ccpcp_pack_context* pack_context)
{
	if (pack_context->err_no)
		return;
	ccpcp_pack_copy_bytes(pack_context, CCPON_STR_FALSE, sizeof(CCPON_STR_FALSE) - 1);
}

void ccpon_pack_boolean(ccpcp_pack_context* pack_context, bool b)
{
	if (pack_context->err_no)
		return;

	if(b)
		ccpon_pack_true(pack_context);
	else
		ccpon_pack_false(pack_context);
}

void ccpon_pack_list_begin(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p)
		*p = CCPON_C_LIST_BEGIN;
	start_block(pack_context);

}

void ccpon_pack_list_end(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	end_block(pack_context);
	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p)
		*p = CCPON_C_LIST_END;
}

void ccpon_pack_map_begin(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p)
		*p = CCPON_C_MAP_BEGIN;
	start_block(pack_context);
}

void ccpon_pack_map_end(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	end_block(pack_context);
	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p) {
		*p = CCPON_C_MAP_END;
	}
}

void ccpon_pack_imap_begin(ccpcp_pack_context* pack_context)
{
	if (pack_context->err_no)
		return;

	ccpcp_pack_copy_bytes(pack_context, CCPON_STR_IMAP_BEGIN, sizeof(CCPON_STR_IMAP_BEGIN)-1);
	start_block(pack_context);
}

void ccpon_pack_imap_end(ccpcp_pack_context *pack_context)
{
	ccpon_pack_map_end(pack_context);
}

void ccpon_pack_meta_begin(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p)
		*p = CCPON_C_META_BEGIN;
	start_block(pack_context);
}

void ccpon_pack_meta_end(ccpcp_pack_context *pack_context)
{
	if (pack_context->err_no)
		return;

	end_block(pack_context);
	char *p = ccpcp_pack_reserve_space(pack_context, 1);
	if(p) {
		*p = CCPON_C_META_END;
	}
}

static char* copy_data_escaped(ccpcp_pack_context* pack_context, const void* str, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		if(pack_context->err_no != CCPCP_RC_OK)
			return NULL;
		uint8_t ch = ((const uint8_t*)str)[i];
		switch(ch) {
		case '\0':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, '0');
			break;
		case '\\':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, '\\');
			break;
		case '\t':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, 't');
			break;
		case '\b':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, 'b');
			break;
		case '\r':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, 'r');
			break;
		case '\n':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, 'n');
			break;
		case '"':
			ccpcp_pack_copy_byte(pack_context, '\\');
			ccpcp_pack_copy_byte(pack_context, '"');
			break;
		default:
			ccpcp_pack_copy_byte(pack_context, ch);
		}
	}
	return pack_context->current;
}

void ccpon_pack_string(ccpcp_pack_context* pack_context, const char* s, size_t l)
{
	ccpcp_pack_copy_byte(pack_context, '"');
	copy_data_escaped(pack_context, s, l);
	ccpcp_pack_copy_byte(pack_context, '"');
}

void ccpon_pack_string_terminated (ccpcp_pack_context* pack_context, const char* s)
{
	size_t len = s? strlen(s): 0;
	ccpon_pack_string(pack_context, s, len);
}

void ccpon_pack_string_start (ccpcp_pack_context* pack_context, const char*buff, size_t buff_len)
{
	ccpcp_pack_copy_byte(pack_context, '"');
	copy_data_escaped(pack_context, buff, buff_len);
}

void ccpon_pack_string_cont (ccpcp_pack_context* pack_context, const char*buff, unsigned buff_len)
{
	copy_data_escaped(pack_context, buff, buff_len);
}

void ccpon_pack_string_finish (ccpcp_pack_context* pack_context)
{
	ccpcp_pack_copy_byte(pack_context, '"');
}

//============================   U N P A C K   =================================

const char* ccpon_unpack_skip_insignificant(ccpcp_unpack_context* unpack_context)
{
	while(1) {
		const char* p = ccpcp_unpack_take_byte(unpack_context);
		if(!p)
			return p;
		if(*p > ' ') {
			if(*p == '/') {
				p = ccpcp_unpack_take_byte(unpack_context);
				if(*p == '*') {
					//multiline_comment_entered;
					while(1) {
						p = ccpcp_unpack_take_byte(unpack_context);
						if(!p)
							return p;
						if(*p == '*') {
							p = ccpcp_unpack_take_byte(unpack_context);
							if(*p == '/')
								break;
						}
					}
				}
				else if(*p == '/') {
					// to end of line comment entered;
					while(1) {
						p = ccpcp_unpack_take_byte(unpack_context);
						if(!p)
							return p;
						if(*p == '\n')
							break;
					}
				}
				else {
					return NULL;
				}
			}
			else if(*p == CCPON_C_KEY_DELIM) {
				continue;
			}
			else if(*p == CCPON_C_FIELD_DELIM) {
				continue;
			}
			else {
				return p;
			}
		}
	}
}

static int unpack_int(ccpcp_unpack_context* unpack_context, int64_t *p_val)
{
	int64_t val = 0;
	int neg = 0;
	int n = 0;
	for (; ; n++) {
		const char *p = ccpcp_unpack_take_byte(unpack_context);
		if(!p)
			goto eonumb;
		uint8_t b = *p;
		switch (b) {
		case '+':
		case '-':
			if(n != 0) {
				unpack_context->current--;
				goto eonumb;
			}
			if(b == '-')
				neg = 1;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			val *= 10;
			val += b - '0';
			break;
		default:
			unpack_context->current--;
			goto eonumb;
		}
	}
eonumb:
	if(neg)
		val = -val;
	if(p_val)
		*p_val = val;
	return n;
}

void ccpon_unpack_date_time(ccpcp_unpack_context *unpack_context, struct tm *tm, int *msec, int *utc_offset)
{
	tm->tm_year = 0;
	tm->tm_mon = 0;
	tm->tm_mday = 1;
	tm->tm_hour = 0;
	tm->tm_min = 0;
	tm->tm_sec = 0;
	tm->tm_isdst = -1;

	*msec = 0;
	*utc_offset = 0;

	const char *p;

	int64_t val;
	int n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed year in DateTime";
		return;
	}
	tm->tm_year = (int)val - 1900;

	UNPACK_ASSERT_BYTE();
	if(*p != '-') {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed year-month separator in DateTime";
		return;
	}

	n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed month in DateTime";
		return;
	}
	tm->tm_mon = (int)val - 1;

	UNPACK_ASSERT_BYTE();
	if(*p != '-') {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed month-day separator in DateTime";
		return;
	}

	n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed day in DateTime";
		return;
	}
	tm->tm_mday = (int)val;

	UNPACK_ASSERT_BYTE();
	if(!(*p == 'T' || *p == ' ')) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed date-time separator in DateTime";
		return;
	}

	n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed hour in DateTime";
		return;
	}
	tm->tm_hour = (int)val;

	UNPACK_ASSERT_BYTE();

	n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed minutes in DateTime";
		return;
	}
	tm->tm_min = (int)val;

	UNPACK_ASSERT_BYTE();

	n = unpack_int(unpack_context, &val);
	if(n <= 0) {
		unpack_context->err_no = CCPCP_RC_MALFORMED_INPUT;
		unpack_context->err_msg = "Malformed seconds in DateTime";
		return;
	}
	tm->tm_sec = (int)val;

	p = ccpcp_unpack_take_byte(unpack_context);
	if(p) {
		if(*p == '.') {
			n = unpack_int(unpack_context, &val);
			if(n < 0)
				return;
			*msec = (int)val;
			p = ccpcp_unpack_take_byte(unpack_context);
		}
		if(p) {
			uint8_t b = *p;
			if(b == 'Z') {
				// UTC time
			}
			else if(b == '+' || b == '-') {
				// UTC time
				n = unpack_int(unpack_context, &val);
				if(!(n == 2 || n == 4))
					UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed TS offset in DateTime.");
				if(n == 2)
					*utc_offset = (int)(60 * val);
				else if(n == 4)
					*utc_offset = (int)(60 * (val / 100) + (val % 100));
				if(b == '-')
					*utc_offset = -*utc_offset;
				p += n;
			}
			else {
				// unget unused char
				unpack_context->current--;
			}
		}
	}
	unpack_context->err_no = CCPCP_RC_OK;
	unpack_context->item.type = CCPCP_ITEM_DATE_TIME;
	int64_t epoch_msec = ccpon_timegm(tm) * 1000;
	ccpcp_date_time *it = &unpack_context->item.as.DateTime;
	epoch_msec += *msec;
	it->msecs_since_epoch = epoch_msec;
	it->minutes_from_utc = *utc_offset;
}

/*
static inline int is_octal(uint8_t b)
{
	return b >= '0' && b <= '7';
}

static inline int is_hex(uint8_t b)
{
	return (b >= '0' && b <= '9')
			|| (b >= 'a' && b <= 'f')
			|| (b >= 'A' && b <= 'F');
}
*/
static void ccpon_unpack_string(ccpcp_unpack_context* unpack_context)
{
	if(unpack_context->item.type != CCPCP_ITEM_STRING)
		UNPACK_ERROR(CCPCP_RC_LOGICAL_ERROR, "Unpack cpon string internal error.");

	const char *p;
	ccpcp_string *it = &unpack_context->item.as.String;
	if(it->chunk_cnt == 0) {
		// must start with '"'
		UNPACK_ASSERT_BYTE();
		if (*p != '"') {
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "String should start with '\"' character.");
		}
	}
	for(it->chunk_size = 0; it->chunk_size < it->chunk_buff_len; ) {
		UNPACK_ASSERT_BYTE();
		if(*p == '\\') {
			UNPACK_ASSERT_BYTE();
			if(!p)
				return;
			switch (*p) {
			case '\\': (it->chunk_start)[it->chunk_size++] = '\\'; break;
			case '"' : (it->chunk_start)[it->chunk_size++] = '"'; break;
			case 'b': (it->chunk_start)[it->chunk_size++] = '\b'; break;
			case 'f': (it->chunk_start)[it->chunk_size++] = '\f'; break;
			case 'n': (it->chunk_start)[it->chunk_size++] = '\n'; break;
			case 'r': (it->chunk_start)[it->chunk_size++] = '\r'; break;
			case 't': (it->chunk_start)[it->chunk_size++] = '\t'; break;
			case '0': (it->chunk_start)[it->chunk_size++] = '\0'; break;
			default: (it->chunk_start)[it->chunk_size++] = *p; break;
			}
		}
		else {
			if (*p == '"') {
				// end of string
				it->last_chunk = 1;
				break;
			}
			else {
				(it->chunk_start)[it->chunk_size++] = *p;
			}
		}
	}
	it->chunk_cnt++;
}

void ccpon_unpack_next (ccpcp_unpack_context* unpack_context)
{
	if (unpack_context->err_no)
		return;

	ccpcp_container_state *top_cont_state = ccpcp_unpack_context_top_container_state(unpack_context);

	const char *p;
	if(unpack_context->item.type == CCPCP_ITEM_STRING) {
		ccpcp_string *str_it = &unpack_context->item.as.String;
		if(!str_it->last_chunk) {
			//UNPACK_ASSERT_BYTE();
			//unpack_context->current--;
			ccpon_unpack_string(unpack_context);
			return;
		}
	}

	unpack_context->item.type = CCPCP_ITEM_INVALID;

	p = ccpon_unpack_skip_insignificant(unpack_context);
	if(!p)
		return;

	switch(*p) {
	case CCPON_C_LIST_END:
	case CCPON_C_MAP_END:
	case CCPON_C_META_END:  {
		unpack_context->item.type = CCPCP_ITEM_CONTAINER_END;
		if(unpack_context->container_stack) {
			ccpcp_container_state *top_cont_state = ccpcp_unpack_context_top_container_state(unpack_context);
			if(!top_cont_state)
				UNPACK_ERROR(CCPCP_RC_CONTAINER_STACK_UNDERFLOW, "Container stack underflow.")
		}
		break;
	}
	case CCPON_C_META_BEGIN:
		unpack_context->item.type = CCPCP_ITEM_META;
		break;
	case CCPON_C_MAP_BEGIN:
		unpack_context->item.type = CCPCP_ITEM_MAP;
		break;
	case CCPON_C_LIST_BEGIN:
		unpack_context->item.type = CCPCP_ITEM_LIST;
		break;
	case 'i': {
		UNPACK_ASSERT_BYTE();
		if(*p != '{')
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "IMap should start with '{'.")
		unpack_context->item.type = CCPCP_ITEM_IMAP;
		break;
	}
	case 'a': {
		UNPACK_ASSERT_BYTE();
		if(*p != '[')
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "List should start with '['.")
		// unpack unsupported ARRAY type as list
		unpack_context->item.type = CCPCP_ITEM_LIST;
		break;
	}
	case 'd': {
		UNPACK_ASSERT_BYTE();
		if(!p || *p != '"')
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "DateTime should start with 'd'.")
		struct tm tm;
		int msec;
		int utc_offset;
		ccpon_unpack_date_time(unpack_context, &tm, &msec, &utc_offset);
		UNPACK_ASSERT_BYTE();
		if(!p || *p != '"')
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "DateTime should start with 'd\"'.")
		break;
	}
	case 'n': {
		UNPACK_ASSERT_BYTE();
		if(*p == 'u') {
			UNPACK_ASSERT_BYTE();
			if(*p == 'l') {
				UNPACK_ASSERT_BYTE();
				if(*p == 'l') {
					unpack_context->item.type = CCPCP_ITEM_NULL;
					break;
				}
			}
		}
		UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed 'null' literal.")
		break;
	}
	case 'f': {
		UNPACK_ASSERT_BYTE();
		if(*p == 'a') {
			UNPACK_ASSERT_BYTE();
			if(*p == 'l') {
				UNPACK_ASSERT_BYTE();
				if(*p == 's') {
					UNPACK_ASSERT_BYTE();
					if(*p == 'e') {
						unpack_context->item.type = CCPCP_ITEM_BOOLEAN;
						unpack_context->item.as.Bool = false;
						break;
					}
				}
			}
		}
		UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed 'false' literal.")
		break;
	}
	case 't': {
		UNPACK_ASSERT_BYTE();
		if(*p == 'r') {
			UNPACK_ASSERT_BYTE();
			if(*p == 'u') {
				UNPACK_ASSERT_BYTE();
				if(*p == 'e') {
					unpack_context->item.type = CCPCP_ITEM_BOOLEAN;
					unpack_context->item.as.Bool = true;
					break;
				}
			}
		}
		UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed 'true' literal.")
		break;
	}
	case '"': {
		unpack_context->item.type = CCPCP_ITEM_STRING;
		ccpcp_string *str_it = &unpack_context->item.as.String;
		ccpcp_string_init(str_it, unpack_context);
		//str_it->format = CCPON_STRING_FORMAT_UTF8_ESCAPED;
		unpack_context->current--;
		ccpon_unpack_string(unpack_context);
		break;
	}
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case '+':
	case '-': {
		// number
		int64_t mantisa = 0;
		int64_t exponent = 0;
		int64_t decimals = 0;
		int dec_cnt = 0;
		struct {
			uint8_t is_decimal: 1;
			//uint8_t is_double: 1;
			uint8_t is_uint: 1;
			uint8_t is_neg: 1;
		} flags;
		flags.is_decimal = 0;
		//flags.is_double = 0;
		flags.is_uint = 0;
		flags.is_neg = 0;

		flags.is_neg = *p == '-';
		if(!flags.is_neg)
			unpack_context->current--;
		int n = unpack_int(unpack_context, &mantisa);
		if(n < 0)
			UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed number.")
		p = ccpcp_unpack_take_byte(unpack_context);
		while(p) {
			if(*p == CCPON_C_UNSIGNED_END) {
				flags.is_uint = 1;
				break;
			}
			if(*p == '.') {
				flags.is_decimal = 1;
				n = unpack_int(unpack_context, &decimals);
				if(n < 0)
					UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed number decimal part.")
				dec_cnt = n;
				p = ccpcp_unpack_take_byte(unpack_context);
				if(!p)
					break;
				/*
				if(*p == CCPON_C_DECIMAL_END) {
					flags.is_decimal = 1;
					break;
				}
				*/
			}
			if(*p == 'e' || *p == 'E') {
				flags.is_decimal = 1;
				n = unpack_int(unpack_context, &exponent);
				if(n < 0)
					UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Malformed number exponetional part.")
				break;
			}
			if(*p != '.') {
				// unget char
				unpack_context->current--;
			}
			break;
		}
		if(flags.is_decimal) {
			for (int i = 0; i < dec_cnt; ++i)
				mantisa *= 10;
			mantisa += decimals;
			unpack_context->item.type = CCPCP_ITEM_DECIMAL;
			unpack_context->item.as.Decimal.mantisa = flags.is_neg? -mantisa: mantisa;
			unpack_context->item.as.Decimal.exponent = exponent - dec_cnt;
		}
		/*
		else if(flags.is_double) {
			double d = decimals;
			for (int i = 0; i < dec_cnt; ++i)
				d /= 10;
			d += mantisa;
			if(exponent < 0) {
				for (int i=0; i>exponent; i--)
					d /= 10;
			}
			else {
				for (int i=0; i<exponent; i++)
					d *= 10;
			}
			unpack_context->item.type = CCPCP_ITEM_DOUBLE;
			unpack_context->item.as.Double = flags.is_neg? -d: d;
		}
		*/
		else if(flags.is_uint) {
			unpack_context->item.type = CCPCP_ITEM_UINT;
			unpack_context->item.as.UInt = mantisa;;

		}
		else {
			unpack_context->item.type = CCPCP_ITEM_INT;
			unpack_context->item.as.Int = flags.is_neg? -mantisa: mantisa;;
		}
		unpack_context->err_no = CCPCP_RC_OK;
		break;
	}
	default:
		UNPACK_ERROR(CCPCP_RC_MALFORMED_INPUT, "Invalid character.");
	}

	bool is_container_end = false;
	switch(unpack_context->item.type) {
	case CCPCP_ITEM_LIST:
	//case CCPCP_ITEM_ARRAY:
	case CCPCP_ITEM_MAP:
	case CCPCP_ITEM_IMAP:
	case CCPCP_ITEM_META:
		ccpcp_unpack_context_push_container_state(unpack_context, unpack_context->item.type);
		break;
	case CCPCP_ITEM_CONTAINER_END:
		ccpcp_unpack_context_pop_container_state(unpack_context);
		is_container_end = true;
		break;
	default:
		break;
	}

	if(top_cont_state && !is_container_end)
		top_cont_state->item_count++;
}

void ccpon_pack_field_delim(ccpcp_pack_context *pack_context, bool is_first_field)
{
	if(!is_first_field)
		ccpcp_pack_copy_bytes(pack_context, ",", 1);
	if(pack_context->cpon_options.indent)
		ccpcp_pack_copy_bytes(pack_context, "\n", 1);
	indent_element(pack_context);
}

void ccpon_pack_key_val_delim(ccpcp_pack_context *pack_context)
{
	ccpcp_pack_copy_bytes(pack_context, ":", 1);
}


