#include <cstdio>
#include <cstdlib>
#include "windows.h"

#include "assert.h"
#include "com.h" /* for ERRMSG_MAX_LEN */
#include "database.h"
#include "delta.h"
#include "sqlite3.h"

#define DBFILENAME			"database.dblite"
#define CELLS_NUMBER		(1768)

char dberrstr[ERRMSG_MAX_LEN];
char dbpathname[PATH_STR_MAX_LEN] = { 0 };
static sqlite3* db = NULL; /* SQLite3 database handler. */

bool
is_database_exists(const char* path)
{
	FILE* f = NULL;

	sprintf(dbpathname, "%s%s", path, DBFILENAME);
	f = fopen(dbpathname, "r");
	if (f != NULL)
		return true;
	else
		return false;
}

bool
is_database_opened()
{
	FILE* f = NULL;
	char* err = 0;

	/* Check path variable initialization. */
	if (strlen(dbpathname) == 0)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Path for a database file doesn't initialized.");
		return false;
	}

	/* Check file existence. */
	f = fopen(dbpathname, "r");
	if (f == NULL)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database file doesn't exists.");
		return false;
	}

	/* Check database descriptor */
	if (db == NULL)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database descriptor is NULL.");
		return false;
	}

	/* And finally, ping the database. */
	if (sqlite3_exec(db, "SELECT 1", NULL, NULL, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Error of database ping: %s.", err);
		sqlite3_free(err);
		return false;
	}

	return true;
}

int
correct_cell_number_cb(void* a_param, int argc, char** argv, char** column)
{
	bool* ok = (bool*)a_param;
	int count;

	*ok = true;

	if (argc != 1)
		*ok = false;

	count = atoi(argv[0]);
	if (count != CELLS_NUMBER)
		*ok = false;

	return 0;
}

bool
check_database()
{
	const char* SQL = "SELECT count(*) FROM data";
	char* err = 0;
	bool ok = false;

	memset(dberrstr, 0, ERRMSG_MAX_LEN);

	if (db == NULL)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database doesn't opened.");
		return false;
	}

	if (sqlite3_exec(db, SQL, correct_cell_number_cb, &ok, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Query error: '%s'.", err);
		sqlite3_free(err);
		return false;
	}

	return ok;
}

errcode_t
open_database(const char* path, bool create)
{
	assert(path != NULL);
	assert(strlen(path) < PATH_STR_MAX_LEN - strlen(DBFILENAME));
	elog(CLOG, "Try to open database.");
	memset(dberrstr, 0, ERRMSG_MAX_LEN);
	sprintf(dbpathname, "%s%s", path, DBFILENAME);

	if (sqlite3_open(dbpathname, &db))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Error on open/create database: %s", sqlite3_errmsg(db));
		return DB_ACCESS_FAILED;
	}

	if (create)
		create_schema();

	if (!check_database())
	{
		char* tmp = _strdup(dberrstr);

		snprintf(dberrstr, ERRMSG_MAX_LEN,
			"Incorrect database structure.\nCONTEXT: %s.", tmp);
		free(tmp);
		return DB_INCORRECT_STATE;
	}

	elog(CLOG, "Database opened correctly.");
	return SUCCESS;
}

void
close_database()
{
	int res;

	assert(db != NULL);
	memset(dbpathname, 0, PATH_STR_MAX_LEN);
	memset(dberrstr, 0, ERRMSG_MAX_LEN);

	res = sqlite3_close(db);
	if (res != SQLITE_OK)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database problems on closing: %d.", res);
		assert(0);
	}

	db = NULL;
}

/*
 * Clear all tables in the schema.
 * Critical action. Must be logged.
 * Returns true on success or false on an error.
 */
bool
clear_database()
{
	const char* SQL = "UPDATE data SET EANcode=NULL";
	char* err = 0;

	if (db == NULL)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database doesn't opened.");
		return false;
	}

	if (sqlite3_exec(db, SQL, NULL, NULL, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Query error: '%s'.", err);
		sqlite3_free(err);
		return false;
	}

	return true;
}

/*
 * Create schema, if it not exists.
 */
bool
create_schema()
{
	char* err = 0;
	const char* SQL =
		"BEGIN;"
		"CREATE TABLE IF NOT EXISTS data (cell int PRIMARY KEY, EANcode VARCHAR(13) DEFAULT NULL);"
		"END;";

	assert(db != NULL);

	if (sqlite3_exec(db, SQL, NULL, NULL, &err))
	{
		elog(CLOG, "SQL error: %s", err);
		sqlite3_free(err);
	}

	/*
	 * Fill the table.
	 */
	for (int i = 1; i <= CELLS_NUMBER; i++)
	{
		char SQL1[QUERY_STR_MAX_LEN];

		sprintf(SQL1, "INSERT INTO data (cell, EANcode) VALUES (%d, NULL)", i);
		if (sqlite3_exec(db, SQL1, NULL, NULL, &err))
		{
			elog(CLOG, "SQL error: %sn", err);
			sqlite3_free(err);
		}
	}

	return true;
}

int
cell_number_cb(void* a_param, int argc, char** argv, char** column)
{
	int* cellnum = (int*)a_param;

	if (argc != 1)
		return -1;

	*cellnum = atoi(argv[0]);
	assert(cellnum >= 0 && cellnum <= CELLS_NUMBER);

	return 0;
}

/*
 * Get number of a cell.
 * If EANCode is NULL, return a first free cell number.
 * Other case, return a first cell, contains this EANCode value.
 * Return cell number in the range of [1 .. CELLS_NUMBER] or error code < 0.
 */
int
db_cell_number(const char *EANCode)
{
	char* err = 0;
	char SQL[QUERY_STR_MAX_LEN];
	int cellnum = -1;

	memset(dberrstr, 0, ERRMSG_MAX_LEN);
	assert(db == NULL);

	if (EANCode == NULL)
		sprintf(SQL, "SELECT cell FROM data WHERE EANcode IS NULL ORDER BY(cell) ASC LIMIT 1");
	else
		sprintf(SQL, "SELECT cell FROM data WHERE EANcode ='%s' ORDER BY(cell) ASC LIMIT 1", EANCode);

	if (sqlite3_exec(db, SQL, cell_number_cb, (void*)&cellnum, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "SQL error: '%s'", err);
		sqlite3_free(err);
		return -2;
	}

	if (cellnum == -1)
	{
		/* No free cells found. */
		snprintf(dberrstr, ERRMSG_MAX_LEN, "No free cells found.");
		return -3;
	}

	return cellnum;
}

/*
 * Store the EAN code into the cell of database.
 * Return true on success or false.
 * See the dberrstr string for details.
 */
bool
db_store_code(unsigned int cellnum, code_t code)
{
	char* err = 0;
	char SQL[QUERY_STR_MAX_LEN] = { 0 };

	memset(dberrstr, 0, ERRMSG_MAX_LEN);

	if (db == NULL)
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "Database doesn't opened.");
		return false;
	}

	snprintf(SQL, QUERY_STR_MAX_LEN,
		"UPDATE data SET EANcode = '%s' WHERE cell = %d",
		code, cellnum);
	//	printf("SQL: %s", SQL);
	if (sqlite3_exec(db, SQL, NULL, NULL, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "SQL error: '%s'", err);
		sqlite3_free(err);
		return false;
	}

	return true;
}

int
cell_code_cb(void* a_param, int argc, char** argv, char** column)
{
	char* const code = (char*)a_param;

	if (argc != 1)
		return -1;

	memset(code, 0, sizeof(code_t));

	if (argv[0] != NULL)
	{
		assert(strlen(argv[0]) == 13);
		memcpy(code, argv[0], strlen(argv[0]));
	}

	return 0;
}

/*
 * Get EAN code in the cell of database.
 * If cell is empty, return NULL.
 * Caller must free the code value manually to avoid memory leaks.
 */
char*
db_get_cell_code(int cellnum)
{
	char* err = 0;
	char SQL[QUERY_STR_MAX_LEN] = { 0 };
	code_t code = { 0 };

	snprintf(SQL, QUERY_STR_MAX_LEN,
		"SELECT EANcode FROM data WHERE cell = %d", cellnum);
	printf("CODE\n");
	if (sqlite3_exec(db, SQL, cell_code_cb, (void*)code, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "SQL error: '%s'", err);
		sqlite3_free(err);
		return NULL;
	}
	return _strdup(code);
}

static int
dumpdb_cb(void* a_param, int argc, char** argv, char** column)
{
	printf("arc: %d %s", argc, argv[0]);
	if (argc != 1)
		return -1;

	return 0;
}

bool
dump_database()
{
	const char* SQL = "SELECT * FROM data WHERE EANcode IS NOT NULL";
	char* err = 0;

	if (sqlite3_exec(db, SQL, dumpdb_cb, NULL, &err))
	{
		snprintf(dberrstr, ERRMSG_MAX_LEN, "SQL error: '%s'", err);
		sqlite3_free(err);
		return false;
	}

	return true;
}
