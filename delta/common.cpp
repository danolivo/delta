#include <cstdio>
#include <cstdlib>
#include "windows.h"

#include "common.h"
#include "delta.h"

static char buf[BUF_MAX_LEN];

const char*
current_time_str()
{
	time_t     now = time(NULL);
	struct tm  tstruct;

	memset(buf, 0, BUF_MAX_LEN);
	tstruct = *localtime(&now);
	// Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
	// for more information about date/time format
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
	return buf;
}
