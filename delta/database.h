#pragma once

#include "common.h"

#define PATH_STR_MAX_LEN	(1024)

extern char dberrstr[ERRMSG_MAX_LEN];
extern char dbpathname[PATH_STR_MAX_LEN];

extern bool is_database_exists(const char* path);
extern bool is_database_opened(void);
extern bool check_database();

extern bool open_database(const char* path, bool create);
extern void close_database(void);
extern bool create_schema();
extern int db_empty_cell_number(void);
extern bool db_store_code(unsigned int cellnum, code_t code);
extern char* db_get_cell_code(int cellnum);
extern bool clear_database(void);
