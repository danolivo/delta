#pragma once

#ifdef DELTA_EXPORTS
#define DELTA_API __declspec(dllexport)
#else
#define DELTA_API __declspec(dllimport)
#endif

typedef enum
{
	DELTA_SUCCESS = 0,
	DELTA_ECOM_NEXISTS,
	DELTA_ESTATE_GET,
	DELTA_ESTATE_SET,
	DELTA_EUNDEFINED
} OpRes_t;

extern "C" DELTA_API int DCM_Connection(int portnum);
extern "C" DELTA_API int DCM_Disconnection(void);
