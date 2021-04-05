#include <assert.h>
#include <stdio.h>
#include "tchar.h"
#include <windows.h>
//#include <WINSOCK2.H>

#include "com.h"

static HANDLE hCom = INVALID_HANDLE_VALUE;
char modbus_errmsg[ERRMSG_MAX_LEN];

static byte CalcLRC(const byte* buf, int len);
static int _write(const byte* sendbuf, int sendlen);

static byte
CalcLRC(const byte* buf, int len)
{
	byte LRC = 0x00;
	while (len--)
		LRC += *buf++;
	return (byte)(-((char)LRC));
}

/*
* Open COM port.
*
* Use fixed values for:
* fParity = true
* Baud rate = 9600
* ByteSize = 7
* Parity = EVENPARITY
* StopBits = ONESTOPBIT
*
* Returns true on success or false on error. Use modbus_errmsg to get an error string.
*
* External links:
*	 https://stackoverflow.com/questions/4768590/serial-device-ignores-escapecommfunction-with-c
*	 https://docs.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-dcb
*	 https://stackoverflow.com/questions/4188782/set-dcb-fails-when-attempting-to-configure-com-port
*	 https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-
*/
bool
ModbusSerialOpen(int port)
{
	assert(port > 0);

	if (hCom != INVALID_HANDLE_VALUE)
		return false;

	TCHAR port_name[12] = { 0 };
	_stprintf_s(port_name, _T("\\\\.\\COM%d"), port);

	hCom = CreateFile(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (hCom == INVALID_HANDLE_VALUE)
	{
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
			"ERROR: COM port %d cannot be opened",
			port);
		return false;
	}
	else
	{
		DCB Dcb = { 0 };
		COMMTIMEOUTS TimeOuts;
		Dcb.DCBlength = sizeof(DCB);

		if (!GetCommState(hCom, &Dcb))
		{
			snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
				"ERROR: Unable to retrieve the current control settings for a specified communications device for the port %d\nCONTEXT: system errcode=%d",
				port,
				GetLastError());
		}

		Dcb.fParity = true; // enable parity check
		Dcb.BaudRate = CBR_9600;
		Dcb.ByteSize = 7;
		Dcb.Parity = EVENPARITY;
		Dcb.StopBits = ONESTOPBIT; /* ONESTOPBIT to prevent errors on the port opening */

		if (!SetCommState(hCom, &Dcb))
		{
			snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
				"ERROR: Unable to configure the port %d\nCONTEXT: system errcode=%d",
				port,
				GetLastError());
			return false;
		}

		if (!SetupComm(hCom, MAX_BUFFER_LENGTH, MAX_BUFFER_LENGTH))
		{
			snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
				"ERROR: Unable to set the data parameters for the port %d",
				port);
			return false;
		}

		TimeOuts.ReadIntervalTimeout = 25;
		TimeOuts.ReadTotalTimeoutMultiplier = 1;
		TimeOuts.ReadTotalTimeoutConstant = 1;
		TimeOuts.WriteTotalTimeoutMultiplier = 25;
		TimeOuts.WriteTotalTimeoutConstant = 1;

		if (!SetCommTimeouts(hCom, &TimeOuts))
		{
			snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
				"ERROR: Unable to set the time-out parameters for the port %d",
				port);
			return false;
		}
	}

	if (!EscapeCommFunction(hCom, SETDTR) || !EscapeCommFunction(hCom, SETRTS))
	{
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
			"ERROR in EscapeCommFunction for the port %d\nCONTEXT: system errcode=%d",
			port,
			GetLastError());
		return false;
	}

	return true;
}

bool
ModbusSerialClose()
{
	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (CloseHandle(hCom))
		{
			hCom = INVALID_HANDLE_VALUE;
			return true;
		}
	}

	snprintf(modbus_errmsg, ERRMSG_MAX_LEN,
		"ERROR on closing the serial port.\nCONTEXT: system errcode=%d",
		GetLastError());
	return false;
}

static char buf[MAX_BUFFER_LENGTH];

int
ModbusSerialRequest(const char* msg)
{
	DWORD dwTemp;
	byte lrcbuf[MAX_BUFFER_LENGTH] = { 0 };
	int lrclen = 0;
	byte LRC;
	OVERLAPPED Overlap = { 0 };

	if (hCom == INVALID_HANDLE_VALUE)
	{
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Invalid handle value.");
		return -1;
	}

	if (strlen(msg) + 4 > MAX_BUFFER_LENGTH)
	{
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "The message string is too long.");
		return -1;
	}

	memset((void*)buf, 0, MAX_BUFFER_LENGTH);
	memcpy(buf, msg, strlen(msg));

	// calculate LRC with the original buffer content including station and func
	for (unsigned int i = 1; i < strlen(buf); i += 2)
	{
		unsigned int value = 0x00;
		char A2H[3] = { 0 };
		A2H[0] = buf[i];
		A2H[1] = buf[i + 1];

		sscanf(A2H, "%02X", &value);
		//		printf("%d/%d (%s) : %d\n", i, strlen(buf), A2H, value);
		assert(value < 256);
		lrcbuf[lrclen++] = (byte)value;
	}

	LRC = CalcLRC(lrcbuf, lrclen);
	sprintf(&buf[strlen(buf)], "%02X", LRC);
	buf[strlen(buf)] = 0x0D;
	buf[strlen(buf)] = 0x0A;

	Overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (!WriteFile(hCom, buf, (DWORD)strlen(buf), &dwTemp, &Overlap))
	{
		if (GetLastError() == ERROR_IO_PENDING)
		{
			if (!GetOverlappedResult(hCom, &Overlap, &dwTemp, TRUE))
			{
				CloseHandle(Overlap.hEvent);
				snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Error 1");
				return  -1;
			}
		}
		else
		{
			snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Error 2");
			CloseHandle(Overlap.hEvent);
			return  -1;
		}
	}

	CloseHandle(Overlap.hEvent);
	return int(dwTemp);
}

int Read(byte* recvbuf, int recvlen, int timeout)
{
	OVERLAPPED Overlap = { 0 };
	DWORD mask = 0;
	DWORD received = 0;

	if (hCom == INVALID_HANDLE_VALUE)
		return -1;

	Overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	SetCommMask(hCom, EV_RXCHAR);
	WaitCommEvent(hCom, &mask, &Overlap);

	if (WaitForSingleObject(Overlap.hEvent, timeout) == WAIT_OBJECT_0)
	{
		if (!ReadFile(hCom, recvbuf, recvlen, &received, &Overlap))
		{
			if (GetLastError() == ERROR_IO_PENDING)
			{
				if (!GetOverlappedResult(hCom, &Overlap, &received, TRUE))
				{
					CloseHandle(Overlap.hEvent);
					SetCommMask(hCom, NULL);
					snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Error 3");
					return -1;
				}
			}
			else
			{
				CloseHandle(Overlap.hEvent);
				SetCommMask(hCom, NULL);
				snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Error 4");
				return -1;
			}
		}
	}
	else // Timeout
	{
		CloseHandle(Overlap.hEvent);
		SetCommMask(hCom, NULL);
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "terminated by timeout (%d ms)", timeout);
		return -1;
	}

	CloseHandle(Overlap.hEvent);
	SetCommMask(hCom, NULL);

	return (int)received;
}

int
ModbusSerialResponse(char* recvbuf, int buflen, int timeout)
{
	int received;

	memset((void*)buf, 0, MAX_BUFFER_LENGTH);
	received = Read((byte*)buf, MAX_BUFFER_LENGTH, timeout);

	if (received > buflen)
	{
		snprintf(modbus_errmsg, ERRMSG_MAX_LEN, "Buffer too small");
		return -1;
	}

	if (received > 0)
		memcpy(recvbuf, buf, received);

	return received;
}
