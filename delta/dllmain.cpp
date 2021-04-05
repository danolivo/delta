// dllmain.cpp : Определяет точку входа для приложения DLL.
#include <assert.h>
#include <cstdio>
#include <time.h>
#include "windows.h"

#include "com.h"
#include "delta.h"

#define COMM_TYPE (0) /* RS-232 */

#define READ_REGISTERS	(3)
#define DEFAULT_TIMEOUT (100) /* ms */

#define REQ_CLR_REGS		"01101064000306000000000000" /* Clear D100,D101 and D102 */
#define REQ_READ_CMD		":010310640001" /* D100 */
#define REQ_READ_RES		":010310660001" /* D102 */
#define REQ_READ_ALL		":010310640003"

#define REQ_WRITE_CMD_RECOGNIZE		":011010640001021F1F"
#define REQ_WRITE_CLEAN				":01101064000306000000000000"

#define MAGIC_WORD "1F1F"

/* Commands for a controller*/
#define CHECK_PROTOCOL	(1)

static unsigned char sendbuf[MAX_BUFFER_LENGTH];
static char recvbuf[MAX_BUFFER_LENGTH];
static char errmsg[1024];
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

static FILE* output = NULL; /* destination for log output */

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		FILE* f = fopen("logfile.log", "wt");

		assert(f != NULL);

		/* Debugging output disabled by default */
		output = f;
	}
	break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		if (!output || output == stdout)
			break;

		/* Cleanup on exit */
		fclose(output);
		break;
	}
	return TRUE;
}

static bool IsRegistersEmpty(const char* data, int nreqs);
static int send_modbus_data(const char* input, int timeout);

/*
 * Fill sendbuf structure with the message data in correct MODBUS format.
 * Returns number of bytes received (see recvmsg for this bytes)
 * or -1 on error (See the errmsg string for details).
 */
static int
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
		return -1;
	}

	/* Response for result. */
	buflen = ModbusSerialResponse(recvbuf, MAX_BUFFER_LENGTH, timeout);
	if (buflen <= 0)
	{
		snprintf(errmsg, ERRMSG_MAX_LEN,
			"COM%d: No data received on response.\nDETAILS: device_addr=%d, input data=\'%s\'\nCONTEXT: %s",
			com_port_number, DEVICE_ADDR, input, modbus_errmsg);
		return -1;
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
			return -1;
		}
		//		printf("--- EEE: %s %d %d\n", recvmsg, received, buflen);
	}

	return received;
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

static int
wait_cmd_cleanup(int timeout)
{
	time_t start, end;
	int errcode = 0;
	double seconds;

	start = time(NULL);
	do
	{
		if ((errcode = send_modbus_data(REQ_READ_CMD, DEFAULT_TIMEOUT)) < 0)
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
*/
static int
execute_command(int command)
{
	int errcode = 0;

	assert(com_port_number > 0);

	memset(recvbuf, 0, MAX_BUFFER_LENGTH);
	memset(errmsg, 0, 1024);

	/* Before each command check that registers zeroed. */
	if ((errcode = send_modbus_data(REQ_READ_ALL, DEFAULT_TIMEOUT)) < 0)
		return errcode;

	if (!IsRegistersEmpty(recvmsg, 3))
	{
		snprintf(errmsg, 1024,
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
			snprintf(errmsg, 1024,
				"COM%d: Controller doesn't clean command register.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -2;
		}

		/* Wait for result code. */
		if (wait_cmd_result(5) != 0)
		{
			snprintf(errmsg, 1024,
				"COM%d: Timeout expired. Controller doesn't return result code.", com_port_number);
			return -3;
		}

		if (strcmp(recvmsg, "1F1F") != 0)
		{
			snprintf(errmsg, 1024,
				"COM%d: Controller doesn't return correct value.\nDETAILS: data in registers=\'%s\'",
				com_port_number, recvmsg);
			return -3;
		}

		break;

	default:
		snprintf(errmsg, 1024, "Incorrect command");
		errcode = -1;
		break;
	}

	return errcode;
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
			fprintf(output, "COM%d: error on opening: \'%s\'\n", portnum, modbus_errmsg);
			goto cleanup;
		}

		/* Serial port opened. Set current port value. */
		com_port_number = portnum;

		/* Initial cleaning of registers. */
		if ((errcode = send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT)) < 0)
		{
			fprintf(output, "Error during cleaning of the service registers of controller.\nCONTEXT:%s (errcode = %d)\n", errmsg, errcode);
			goto cleanup;
		}

		if ((errcode = execute_command(CHECK_PROTOCOL)) <= 0)
		{
			fprintf(output, "Error during controller recognizing.\nCONTEXT:%s (%d)\n", errmsg, errcode);
			goto cleanup;
		}

		return com_port_number;
	}

	/* Auto search of connected device */
	for (com_port_number = 1; com_port_number < 10; com_port_number++)
	{
		if (!ModbusSerialOpen(com_port_number))
		{
			fprintf(output, "COM%d: error on opening: \'%s\'\n", com_port_number, modbus_errmsg);
			DCM_Disconnection();
			continue;
		}

		/* Initial cleaning of registers. */
		send_modbus_data(REQ_WRITE_CLEAN, DEFAULT_TIMEOUT);

		/* Port opened without errors. Check for the controller. */
		if ((errcode = execute_command(CHECK_PROTOCOL)) <= 0)
		{
			fprintf(output, "Error during controller recognizing.\nCONTEXT:%s (errcode=%d)\n", errmsg, errcode);
			DCM_Disconnection();
			continue;
		}
		else
		{
			fprintf(output, "Connection with controller %d established\n", com_port_number);
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
	assert(com_port_number != 0);
	return !ModbusSerialClose();
}

void
DCM_Enable_logging(const char* filename)
{
	if (DCM_IsLoggingEnabled())
		/* Already enabled */
		return;

	if (filename == NULL)
	{
		/* Set stdout as destination for log messages */
		output = stdout;
	}
	/*
		if (OUTNUL != NULL)
		{
			fclose(OUTNUL);
			OUTNUL = NULL;
		}
	*/
}

/*
 * Disable logging if enabled.
 * Return true, if logging had enabled. otherwise, false.
 */
bool
DCM_Disable_logging()
{
	FILE* f;

	if (!DCM_IsLoggingEnabled())
		/* already disabled */
		return false;

	f = fopen("logfile.log", "wt");
	assert(f != NULL);
	/*	if (OUTNUL == NULL)
		{
			OUTNUL = fopen("nul", "w");
			assert(OUTNUL != NULL);
		}
		*/
	if (output == stdout)
		/* Can't close stdout */
		output = f;
	else
		fclose(output);

	return true;
}

bool
DCM_IsLoggingEnabled()
{
	if (!output || output != stdout)
		return false;
	else
		return true;
}
