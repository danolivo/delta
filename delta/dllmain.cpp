// dllmain.cpp : Определяет точку входа для приложения DLL.
#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include "windows.h"

#include "com.h"
#include "common.h"
#include "database.h"
#include "delta.h"

#define COMMAND_MAX_LENGTH	(100)

#define DEFAULT_TIMEOUT (100) /* ms */

#define REQ_CLR_REGS		"01101064000306000000000000" /* Clear D100,D101 and D102 */
#define REQ_READ_CMD		":010310640001" /* D100 */
#define REQ_READ_RES		":010310660001" /* D102 */
#define REQ_READ_ALL		":010310640003"

#define REQ_WRITE_CMD_RECOGNIZE		":011010640001021F1F"
#define REQ_WRITE_ADD_ITEM			":011010640001020001"
#define REQ_WRITE_GET_ITEM			":011010640001020002"
#define REQ_WRITE_DATA_REG			":01101065000102" /* Prefix of the 'write to the data register' modbus ASCII string */
#define REQ_WRITE_CLEAN				":01101064000306000000000000"

#define MAGIC_WORD "1F1F"

/* Commands for a controller*/
#define CHECK_PROTOCOL		(1)
#define ADD_NEW_ITEM		(2)
#define GET_CELLNUM_ITEM	(3)

static unsigned char sendbuf[MAX_BUFFER_LENGTH];
static char recvbuf[MAX_BUFFER_LENGTH];
static char errmsg[ERRMSG_MAX_LEN];
char DCMErrStr[ERRMSG_MAX_LEN]; /* Error string that can be interpret by user of this DLL. */
char recvmsg[MAX_BUFFER_LENGTH];

/* Standard modbus errors. */
#define ECODEFUNC	"01" /* Принятый код функции не может быть обработан. */
#define EINVADDESS	"02" /* Адрес данных, указанный в запросе, недоступен. */
#define EINCORRECTQUERY	"07" /* Ведомое устройство не может выполнить программную функцию, заданную в запросе. Этот код возвращается для неуспешного программного запроса, использующего функции с номерами 13 или 14. Ведущее устройство должно запросить диагностическую информацию или информацию об ошибках от ведомого. */
#define ECRCVIOLATE	"08" /* Ведомое устройство при чтении расширенной памяти обнаружило ошибку контроля четности. Главный может повторить запрос позже, но обычно в таких случаях требуется ремонт оборудования. */

/*
	03 — Значение, содержащееся в поле данных запроса, является недопустимой величиной.
	04 — Невосстанавливаемая ошибка имела место, пока ведомое устройство пыталось выполнить затребованное действие.
	05 — Ведомое устройство приняло запрос и обрабатывает его, но это требует много времени. Этот ответ предохраняет ведущее устройство от генерации ошибки тайм-аута.
	06 — Ведомое устройство занято обработкой команды. Ведущее устройство должно повторить сообщение позже, когда ведомое освободится.

	 */

static int com_port_number = -1; /* -1 - not initialized. */

/*
 * Logging stuff
 */
#define LOGFILE "logfile.log"

 /* descriptor of log file. open on start */
FILE* flog = NULL;

/* NULL means no output at all. */
FILE* output_device = NULL;

/* Path to dll library (without its name)*/
static char path[PATH_STR_MAX_LEN] = { 0 };
static char DBStateError[ERRMSG_MAX_LEN] = { 0 }; /* If database opened, it is zeroed. Another case it contains description of the problem. */

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	LPWSTR wideStr = new TCHAR[PATH_STR_MAX_LEN];
	char* pathptr = NULL;
	char* tmppath;

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		size_t nsymbols = 0;

		/* Debugging output disabled by default */
		output_device = NULL;

		if (strlen(path) == 0)
		{
			/* Get path to DLL library. */
			if (GetModuleFileName(hModule, wideStr, PATH_STR_MAX_LEN) == 0)
			{
				int ret = GetLastError();
				fprintf(stderr, "GetModuleFileName failed, error = %d\n", ret);
			}

			nsymbols = wcstombs(path, wideStr, PATH_STR_MAX_LEN);
			assert(nsymbols > 0 && nsymbols < PATH_STR_MAX_LEN);
			delete[] wideStr;
			tmppath = _strdup(path);
			assert(tmppath != NULL && strlen(tmppath) > 0);

			pathptr = tmppath;
			while (pathptr && strstr(pathptr, "\\") != NULL)
				pathptr = strstr(pathptr, "\\") + 1;

			assert(pathptr != tmppath);
			path[pathptr - tmppath] = '\0';
			free(tmppath);
		}
		else
		{
			printf("Impossible to join more than one process at the same time.\n");
			break;
		}

		if (!flog)
		{
			char logpathname[PATH_STR_MAX_LEN] = { 0 };
			sprintf(logpathname, "%s%s", path, LOGFILE);
			flog = fopen(logpathname, "at+");
		}

		assert(flog != NULL);
		elog(CLOG, "Session begin");

		/* Check that database file exists. */
		if (is_database_exists(path))
		{
			/* If database file exists, but it is empty, close connection. */
			if (!open_database(path, false))
			{
				elog(CLOG, "ERROR: Database can't be opened:\n%s", dberrstr);

				/* Save description of this critical problem. */
				snprintf(DBStateError, ERRMSG_MAX_LEN, "%s", dberrstr);
				close_database();
				break;
			}

			/* Database opened correctly. Clean all possible previous error messages. */
			memset(DBStateError, 0, ERRMSG_MAX_LEN);
		}
		else
		{
			elog(ERR, "The database doesn't exists. You must explicitly create it with explicit call of the DCM_Create_database() routine.");
			/* Save description of this critical problem. */
			snprintf(DBStateError, ERRMSG_MAX_LEN, "Database file doesn't exists");

			break;
		}
	}
	break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		/* Cleanup on exit */
		memset(path, 0, PATH_STR_MAX_LEN);
		if (output_device &&
			output_device != stdout && output_device != stdin && output_device != stderr)
			fclose(output_device);

		close_database();
		elog(CLOG, "Session finished.");
		fclose(flog);
		break;
	}
	return TRUE;
}

static bool IsRegistersEmpty(const char* data, int nreqs);
BOOL DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
static errcode_t send_modbus_data(const char* input, int timeout);

/*
 * Fill sendbuf structure with the message data in correct MODBUS format.
 * Returns SUCCESS (see recvmsg for this bytes)
 * or error code (See errcodes.h and the errmsg string for details).
 */
static errcode_t
send_modbus_data(const char* input, int timeout)
{
	unsigned int i;
	int buflen = 0;
	int resp_device_addr;
	int resp_command_code;
	int received = 0;

	memset(errmsg, 0, ERRMSG_MAX_LEN);
	memset(recvmsg, 0, MAX_BUFFER_LENGTH);

	/* Send the data */
	if (ModbusSerialRequest(input) == -1)
	{
		snprintf(errmsg, ERRMSG_MAX_LEN,
			"Request to COM%d failed.\nDETAILS: device_addr=%d, input data=\'%s\'",
			com_port_number, DEVICE_ADDR, input);
		return MODBUS_REQUEST_FAILED;
	}

	/* Response for result. */
	buflen = ModbusSerialResponse(recvbuf, MAX_BUFFER_LENGTH, timeout);
	if (buflen <= 0)
	{
		snprintf(errmsg, ERRMSG_MAX_LEN,
			"COM%d: No data received on response.\nDETAILS: device_addr=%d, input data=\'%s\'\nCONTEXT: %s",
			com_port_number, DEVICE_ADDR, input, modbus_errmsg);
		return MODBUS_RESPONSE_FAILED;
	}

	if (buflen > 0)
	{
		char A2H[3] = { 0 };

		memset(recvmsg, 0, MAX_BUFFER_LENGTH);

		/* Read address and command */
		for (i = 1; i < 7; i += 2)
		{
			A2H[0] = recvbuf[i];
			A2H[1] = recvbuf[i + 1];

			/* recvbuf[0] == ':' */
			if (i == 1)
				(void)sscanf(A2H, "%02X", &resp_device_addr);
			else if (i == 3)
				(void)sscanf(A2H, "%02X", &resp_command_code);
			else
				/* i == 5 */
				(void)sscanf(A2H, "%02X", &received);

			assert(i <= 5);
		}

		assert(resp_device_addr == 1);
		assert(resp_command_code > 0 && resp_command_code < 256);
		assert(received > 0);
		assert(buflen = received + 7 + 4);
		assert(received < MAX_BUFFER_LENGTH);
		memcpy(recvmsg, &(recvbuf[7]), (size_t)received * 2); /* exclude prefix (7-byte :<addr><command><count>) and postfix (4-byte <CRC><LR><CF>) */

		/* Detect error during the command execution. */
		if ((resp_command_code & 128) != 0)
		{
			assert(strlen(recvmsg) > 0);
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller error \'%s\'.\nDETAILS: device_addr=%d, input data=\'%s\'",
				com_port_number, recvmsg, DEVICE_ADDR, input);
			return MODBUS_UNKNOWN_PROBLEM;
		}
	}

	return SUCCESS;
}

static bool
IsRegistersEmpty(const char* data, int nreqs)
{
	int i;

	assert(strlen(data) == nreqs * 4);

	for (i = 0; i < nreqs * 4; i++)
		if (data[i] != '0')
			return false;

	return true;
}

/*
 * Wait for timeout (in seconds) of the command and data registers cleaning
 * from the controller-side.
 * Return 0 on success. Otherwise, return -1.
 */
static int
wait_cmd_cleanup(int timeout)
{
	time_t start, end;
	errcode_t errcode;
	double seconds;

	start = time(NULL);
	do
	{
		if ((errcode = send_modbus_data(REQ_READ_CMD, DEFAULT_TIMEOUT)) != SUCCESS)
			return errcode;

		end = time(NULL);
		seconds = difftime(end, start);

		if (IsRegistersEmpty(recvmsg, 1))
			/* Success */
			return 0;
	} while (seconds < timeout);

	return -1;
}

static int
wait_cmd_result(int timeout)
{
	time_t start;
	int errcode = 0;
	double seconds;

	start = time(NULL);
	do
	{
		if ((errcode = send_modbus_data(REQ_READ_RES, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		seconds = difftime(time(NULL), start);

		if (!IsRegistersEmpty(recvmsg, 1))
			/* Success */
			return 0;
	} while (seconds < timeout);

	return -1;
}

/*
* Send a command to the controller and return error code or 0.
* Request data write to the recvbuf variable.
* Errors of DLL itself < 0.
* Errors of a controller > 0.
*/
static int
execute_command(int command, const char* data)
{
	int errcode = 0;
	char wrtdatareq[COMMAND_MAX_LENGTH];

	assert(com_port_number > 0);

	memset(recvbuf, 0, MAX_BUFFER_LENGTH);
	memset(errmsg, 0, ERRMSG_MAX_LEN);

	if (data != NULL)
	{
		int len;

		assert(strlen(data) == 4);
		memset(wrtdatareq, 0, COMMAND_MAX_LENGTH);

		len = snprintf(wrtdatareq, COMMAND_MAX_LENGTH,
			"%s%s",
			":01101065000102", data);
		assert(len < COMMAND_MAX_LENGTH);
	}

	/* Before each command check that registers zeroed. */
	if ((errcode = send_modbus_data(REQ_READ_ALL, DEFAULT_TIMEOUT)) < 0)
		return errcode;

	if (!IsRegistersEmpty(recvmsg, 3))
	{
		snprintf(errmsg, ERRMSG_MAX_LEN,
			"COM%d: Controller registers is not empty.\nDETAILS: device_addr=%d, data in registers=\'%s\'",
			com_port_number, DEVICE_ADDR, recvmsg);
		return -1;
	}

	switch (command)
	{
	case CHECK_PROTOCOL:
		/* Write command (do not need to fill data register) */
		if ((errcode = send_modbus_data(REQ_WRITE_CMD_RECOGNIZE, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		/* Wait for reaction of the controller program. */
		if (wait_cmd_cleanup(1) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller doesn't clean command register.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -2;
		}

		/* Wait for result code. */
		if (wait_cmd_result(5) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Timeout expired. Controller doesn't return result code.", com_port_number);
			return -3;
		}

		if (strcmp(recvmsg, "1F1F") != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller doesn't return correct value.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -3;
		}

		break;

	case ADD_NEW_ITEM:
		assert(strlen(wrtdatareq) > 0);
		elog(LOG, "START MODBUS PROTOCOL OF ADDITION DATA. modbus cmd='%s'\n", wrtdatareq);
		/* Write data */
		if ((errcode = send_modbus_data(wrtdatareq, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		/* Write command */
		if ((errcode = send_modbus_data(REQ_WRITE_ADD_ITEM, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		elog(LOG, "COMMAND '%s' and data '%s' has written to controller.", REQ_WRITE_ADD_ITEM, data);

		/* Wait for reaction of the controller program. */
		if (wait_cmd_cleanup(1) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller doesn't clean command register.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -2;
		}

		elog(LOG, "Controller cleaned its registers.");

		/* Wait for result code. */
		if (wait_cmd_result(5) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Timeout expired. Controller doesn't return result code.", com_port_number);
			return -3;
		}

		elog(LOG, "Controller has returned a code '%s'.", recvmsg);

		if (strcmp(recvmsg, "0001") != 0)
		{
			char* ptr;

			(void)sscanf(recvmsg, "%02X", &errcode);
			elog(LOG, "Error detected: errcode = '%d'.", errcode);

			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller failed to add the item.\nDETAILS: error code in register=\'%s\'",
				com_port_number, recvmsg);

			/* Save error message before sending cleanup command. */
			ptr = _strdup(errmsg);
			/*
			 * Try to clean registers. We don't know details of this problem
			 * and don't check result of this operation.
			 */
			(void) send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT);

			/* Restore error message. */
			memset(errmsg, 0, ERRMSG_MAX_LEN);
			snprintf(errmsg, ERRMSG_MAX_LEN, "%s", ptr);
			free(ptr);

			return errcode;
		}
		break;

	case GET_CELLNUM_ITEM: /* Send command to extract a product from the cell */
		assert(strlen(wrtdatareq) > 0);

		elog(LOG, "START MODBUS command chain to extract a product. modbus cmd='%s'\n", wrtdatareq);

		/* Write data */
		if ((errcode = send_modbus_data(wrtdatareq, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		/* Write command */
		if ((errcode = send_modbus_data(REQ_WRITE_GET_ITEM, DEFAULT_TIMEOUT)) < 0)
			return errcode;

		elog(LOG, "COMMAND '%s' and data '%s' has written to controller.", REQ_WRITE_GET_ITEM, data);

		/* Wait for reaction of the controller program. */
		if (wait_cmd_cleanup(1) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller doesn't clean command register.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -2;
		}

		elog(LOG, "Controller cleaned its registers.");

		/* Wait for result code. */
		if (wait_cmd_result(5) != 0)
		{
			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Timeout expired. Controller doesn't return result code.", com_port_number);
			return -3;
		}

		elog(LOG, "Controller has returned a code '%s'.", recvmsg);

		if (strcmp(recvmsg, "0002") != 0)
		{
			char* ptr;

			(void)sscanf(recvmsg, "%02X", &errcode);
			elog(LOG, "Error detected: errcode = '%d'.", errcode);

			snprintf(errmsg, ERRMSG_MAX_LEN,
				"COM%d: Controller failed to get the item.\nDETAILS: error code in register=\'%s\'",
				com_port_number, recvmsg);

			/* Save error message before sending cleanup command. */
			ptr = _strdup(errmsg);

			/*
			 * Try to clean registers. We don't know details of this problem
			 * and don't check result of this operation.
			 */
			send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT);

			/* Restore error message. */
			memset(errmsg, 0, ERRMSG_MAX_LEN);
			snprintf(errmsg, ERRMSG_MAX_LEN, "%s", ptr);
			free(ptr);

			return errcode;
		}
		break;

	default:
		snprintf(errmsg, ERRMSG_MAX_LEN, "Incorrect command");
		errcode = -1;
		break;
	}

	/* Finally, clean registers in accordance with our convention. */
	if ((errcode = send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT)) < 0)
	{
		size_t errmsglen = strlen(errmsg);
		char* ptr;

		assert(errmsglen < ERRMSG_MAX_LEN);

		if (errmsglen != 0)
		{
			/* Copy error message to temporary buffer. */
			ptr = _strdup(errmsg);
			snprintf(errmsg,
				ERRMSG_MAX_LEN,
				"Error during cleaning of the service registers at the end of command\n%s\n(errcode=%d).",
				ptr, errcode);
			free(ptr);
			return -4;
		}
	}

	return 0;
}

/*
 * Connect to controller by modbus ASCII protocol that can understand
 * our logic of connection.
 * Use 0 to auto search.
 * Return the number of the serial port or an error code < 0.
 */
int
DCM_Connection(int portnum)
{
	int errcode;

	if (!is_database_opened())
	{
		fprintf(stderr, "Database doesn't opened. You can't do any operations before explicit creation of the database.");
		Sleep(2000);
		return -2;
	}

	assert(portnum >= 0);

	if (com_port_number > 0)
	{
		/* Disconnect before establishing a new connection. */
		DCM_Disconnection();
		com_port_number = -1;
	}

	if (portnum != 0)
	{
		/* Try to connect to portnum */
		if (!ModbusSerialOpen(portnum))
		{
			elog(ERR, "COM%d: error on opening: \'%s\'\n", portnum, modbus_errmsg);
			goto cleanup;
		}

		/* Serial port opened. Set current port value. */
		com_port_number = portnum;

		/* Initial cleaning of registers. */
		if ((errcode = send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT)) < 0)
		{
			elog(ERR, "Error during cleaning of the service registers of controller.\nCONTEXT:%s (errcode = %d)\n", errmsg, errcode);
			goto cleanup;
		}

		if ((errcode = execute_command(CHECK_PROTOCOL, 0)) != 0)
		{
			elog(ERR, "Error during controller recognizing.\nCONTEXT:%s (%d)\n", errmsg, errcode);
			goto cleanup;
		}

		/* Connection established. Log this fact. */
		elog(CLOG, "Connection with controller %d established", com_port_number);
		return com_port_number;
	}

	/* Auto search of connected device */
	for (com_port_number = 1; com_port_number < 10; com_port_number++)
	{
		if (!ModbusSerialOpen(com_port_number))
		{
			elog(ERR, "COM%d: error on opening: \'%s\'\n", com_port_number, modbus_errmsg);
			DCM_Disconnection();
			continue;
		}

		/* Initial cleaning of registers. */
		send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT);

		/* Port opened without errors. Check for the controller. */
		if ((errcode = execute_command(CHECK_PROTOCOL, 0)) != 0)
		{
			elog(ERR, "Error during controller recognizing.\nCONTEXT:%s (errcode=%d)\n", errmsg, errcode);
			DCM_Disconnection();
			continue;
		}
		else
		{
			elog(CLOG, "Connection with controller %d established", com_port_number);
			return com_port_number;
		}
	}

cleanup:
	com_port_number = -1;
	DCM_Disconnection();
	return com_port_number;
}

/*
 * Return 0 if OK.
 */
bool
DCM_Disconnection()
{
	bool result;

	if (!is_database_opened())
	{
		fprintf(stderr, "Database doesn't opened. You can't do any operations before explicit creation of the database.");
		Sleep(2000);
		return true;
	}

	assert(com_port_number != 0);
	result = ModbusSerialClose();

	if (result)
		/* Log action of successful disconnection */
		elog(CLOG, "Disconnect the controller on serial port %d.", com_port_number);

	return !result;
}

void
DCM_Enable_logging_file(const char* filename)
{
	assert(filename != NULL);

	/* Close file descriptor. */
	if (output_device &&
		output_device != stdout && output_device != stdin && output_device != stderr)
		fclose(output_device);

	output_device = fopen(filename, "wt");
	assert(output_device != NULL);
}

void
DCM_Enable_logging(FILE* descriptor)
{
	assert(descriptor == stdout || descriptor == stdin || descriptor == stderr);

	/* Close file descriptor. */
	if (output_device &&
		output_device != stdout && output_device != stdin && output_device != stderr)
		fclose(output_device);

	output_device = descriptor;
}

/*
 * Disable logging if enabled.
 * Return true, if logging had enabled. otherwise, false.
 */
bool
DCM_Disable_logging()
{
	if (!DCM_IsLoggingEnabled())
		/* already disabled */
		return false;

	/* Close file descriptor. */
	if (output_device &&
		output_device != stdout && output_device != stdin && output_device != stderr)
		fclose(output_device);

	output_device = NULL;
	return true;
}

bool
DCM_IsLoggingEnabled()
{
	return output_device != NULL;
}

/*
 * Directly it'd be used for debug purposes, mostly.
 */
bool
DCM_Put_item(unsigned int cellnum, code_t code)
{
	int errcode;
	char AH4[5] = { 0 };
	char* tmpcode;

	memset(DCMErrStr, 0, ERRMSG_MAX_LEN);

	if (!is_database_opened())
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Database doesn't opened. You can't do any operations before explicit creation of the database.\nCONTEXT: %s\n",
			dberrstr);
		return false;
	}

	/* Check EAN code */
	if (!checkEANcode(code))
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN, "Incorrect code: %s.\n", code);
		return false;
	}

	sprintf(AH4, "%04X", cellnum);
	/* Sanity check: get EAN code from the cell. It would be NULL in normal case. */
	tmpcode = db_get_cell_code(cellnum);
	if (strlen(tmpcode) != 0)
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN, "Cell %d doesn't empty. code='%s'\n", cellnum, tmpcode);
		free(tmpcode);
		return false;
	}

	elog(LOG, "Try to put EAN code %s to cellnum: %d (%s)", code, cellnum, AH4);

	if ((errcode = execute_command(ADD_NEW_ITEM, AH4)) != 0)
	{
		elog(CLOG,
			"Error during item addition (errcode=%d).\n%s", errcode, errmsg);
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Error during item addition (errcode=%d).\n%s", errcode, errmsg);
		return false;
	}

	assert(errcode == 0);
	if (!db_store_code(cellnum, code))
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Error during storing the result in the database.\nDETAILS: %s", dberrstr);
		return false;
	}
	elog(LOG, "Addition of the product with EAN code '%s' to cellnum '%d' has finished.", code, cellnum);
	return true;
}

/*
 * Протокол выполнения команды:
 * 1. Проверить, что регистры пустые
 * 2. Определить номер ячейки, или вернуть ошибку, если все ячейки заполнены.
 * 3. Занести в регистр D101 номер ячейки
 * 4. Занести в регистр D100 команду.
 * 5. Подождать с таймаутом очистки регистров D100, D101
 * 6. Подождать с таймаутом записи в регистр D102 кода возврата.
 * - Величина таймаута - настраиваемая величина.
 * 7. Если код результата - SUCCESS, то записать в БД EAN code в соответствующую ячейку.
  */
bool
DCM_Add_item(code_t code)
{
	int cellnum;

	memset(DCMErrStr, 0, ERRMSG_MAX_LEN);

	if (!is_database_opened())
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Database doesn't opened. You can't do any operations before explicit creation of the database.\nCONTEXT: %s\n",
			dberrstr);
		return false;
	}

	cellnum = db_cell_number(NULL);
	if (cellnum < 0)
	{
		/* Error during searching for a cell. */
		printf("ERR CODE: %s, cellnum: %d", code, cellnum);
		return false;
	}

	if (!DCM_Put_item(cellnum, code))
		return false;

	return true;
}

errcode_t
DCM_Get_item(code_t code)
{
	int cellnum;
	char AH4[5] = { 0 };
	errcode_t errcode;

	memset(DCMErrStr, 0, ERRMSG_MAX_LEN);

	if (!is_database_opened())
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Database doesn't opened. You can't do any operations before explicit creation of the database.\nCONTEXT: %s\n",
			dberrstr);
		return DB_NOT_OPENED;
	}

	if (!checkEANcode(code))
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN, "Incorrect EAN code '%s'.\n", code);
		return EAN_CODE_FORMAT;
	}

	cellnum = db_cell_number(code);
	sprintf(AH4, "%04X", cellnum);

	elog(LOG, "Try to get EAN code %s from cellnum: %d (%s)", code, cellnum, AH4);

	if ((errcode = execute_command(GET_EAN_ITEM, AH4)) != SUCCESS)
	{
		elog(CLOG,
			"Error during item addition (errcode=%d).\n%s", errcode, errmsg);
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Error during item addition (errcode=%d).\n%s", errcode, errmsg);
		return errcode;
	}

	assert(errcode == 0);
	if (!db_store_code(cellnum, code))
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Error during storing the result in the database.\nDETAILS: %s", dberrstr);
		return false;
	}
	elog(LOG, "Addition of the product with EAN code '%s' to cellnum '%d' has finished.", code, cellnum);

	return SUCCESS;
}

const char*
DCM_Get_path()
{
	return path;
}

/*
 * Create database file and establish connection.
 * For security reasons it will return error if such file still exists.
 * You must drop this file manually before the call.
 *
 * Return true on success or false on error.
 */
bool
DCM_Create_database()
{
	assert(strlen(path) > 0);

	/* Check that database file doesn't exists. */
	if (is_database_exists(path))
	{
		elog(ERR, "The database file exists. You must explicitly drop it before creating the new.");
		return false;
	}
	else
	{
		bool result;

		/* Create database file and init the schema */
		result = open_database(path, true);
		if (result)
		{
			elog(CLOG, "Database file with path %s was created.", path);
			memset(DBStateError, 0, ERRMSG_MAX_LEN);
		}
		else
		{
			elog(CLOG, "Database file with path %s didn't created:\n%s", path, dberrstr);
			snprintf(DBStateError, ERRMSG_MAX_LEN, "DB Path: %s. Error: %s", path, dberrstr);
		}

		return result;
	}
}

bool
DCM_Check_database()
{
	memset(DCMErrStr, 0, ERRMSG_MAX_LEN);

	if (strlen(DBStateError) != 0)
	{
		/* Critical problem exists. */
		snprintf(DCMErrStr, ERRMSG_MAX_LEN, "%s", DBStateError);
		return false;
	}

	if (check_database())
		return true;
	else
	{
		/* Return error description. */
		snprintf(DCMErrStr, ERRMSG_MAX_LEN, "%s", dberrstr);
		return false;
	}
}

bool
DCM_Clear()
{
	memset(DCMErrStr, 0, ERRMSG_MAX_LEN);

	if (!is_database_opened())
	{
		snprintf(DCMErrStr, ERRMSG_MAX_LEN,
			"Database doesn't opened. You can't do any operations before explicit creation of the database.\nCONTEXT: %s\n",
			dberrstr);
		return false;
	}

	/* Log critical activity. */
	elog(CLOG, "Try to clear database.");

	if (!clear_database())
	{
		elog(CLOG, "Error during the database cleaning process.\nDETAILS: %s", dberrstr);
		return false;
	}

	elog(CLOG, "Database %s has cleared.", path);
	return true;
}

bool
checkEANcode(const char* str)
{
	int i;

	if (strlen(str) != 13)
		return false;

	for (i = 0; i < strlen(str); i++)
	{
		if (!isdigit(str[i]))
			return false;
	}

	return true;
}
