#pragma once

#include "common.h"

#ifdef DELTA_EXPORTS
#define DELTA_API __declspec(dllexport)
#else
#define DELTA_API __declspec(dllimport)
#endif

/*
 * ############################################################################
 *
 * INTERFACE
 * 
 * Error codes definition see in errcodes.h
 *
 * ############################################################################
 */

/* Error string that can be read by user of this DLL. */
extern "C" DELTA_API char DCMErrStr[ERRMSG_MAX_LEN];

extern "C" DELTA_API errcode_t DCM_Connection(int portnum);
extern "C" DELTA_API errcode_t DCM_Disconnection(void);

/*
 * Clear all data in the database. This is critical action and it
 * will be logged.
 * Returns true on success or false on fail. See DCMErrStr for details.
 */
extern "C" DELTA_API bool DCM_Clear(void);

/*
 * Add a product into the machine. Returns 0 if operation completed successfully.
 * Otherwise, returns an error code.
 */
extern "C" DELTA_API errcode_t DCM_Add_item(code_t code);

/*
 * Extract one product from the machine. Returns SUCCESS if operation completed successfully.
 * Otherwise, returns an error code.
 */
extern "C" DELTA_API errcode_t DCM_Get_item(code_t code);

/*
 * Create a database file in the directory where current DLL is placed.
 * For security reasons we do it explicitly.
 */
extern "C" DELTA_API errcode_t DCM_Create_database(void);


/*
 * ############################################################################
 *
 * Service functions.
 *
 * ############################################################################
 */

extern FILE* output_device;

#define LOG		1
#define ERR		2
#define CLOG	3

/*
 * Log the message.
 *
 * In the case of CLOG we will write message in special log file in all cases.
 * In DEBUG mode it will be written to debug device too.
 *
 */
extern FILE* flog;
#define elog(level, ...) \
{ \
	if (level == CLOG) \
	{ \
		fprintf(flog, "%s: ", current_time_str()); \
		fprintf(flog, __VA_ARGS__); \
		fprintf(flog, "\n"); \
	} \
	if (level == ERR) \
	{ \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} \
	else if (output_device != NULL) \
	{ \
		fprintf(output_device, __VA_ARGS__); \
		fprintf(output_device, "\n"); \
	} \
	fflush(flog); \
	fflush(output_device); \
	fflush(NULL); \
}

extern "C" DELTA_API const char* current_time_str(void);
extern "C" DELTA_API void DCM_Enable_logging_file(const char* filename);
extern "C" DELTA_API void DCM_Enable_logging(FILE * descriptor);
extern "C" DELTA_API bool DCM_Disable_logging(void);
extern "C" DELTA_API bool DCM_IsLoggingEnabled(void);
extern "C" DELTA_API bool DCM_Check_database(void);
extern "C" DELTA_API void DCM_Dump_database(void);

/* Returns path to the database. */
extern "C" DELTA_API const char* DCM_Get_path();

/* Return true, of string contains symbols in correct EAN code format */
extern "C" DELTA_API errcode_t checkEANcode(const char* str);

extern "C" DELTA_API int DCM_Conn_serial(void);