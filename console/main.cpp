#include <assert.h>
#include <conio.h>
#include <cstdio>
#include <windows.h>

#include "delta.h"

static int global_state = 0;
static int portnum = -1;

static void debugging_delay(void);
static void clear_screen(char fill);
static void execute_console_command(char cmd);

/*
 *
 * ----------------------------------------------------------------------------
 *
 * IMPLEMENTATION
 *
 * ----------------------------------------------------------------------------
 */

static void
clear_screen(char fill = ' ')
{
	COORD tl = { 0,0 };
	CONSOLE_SCREEN_BUFFER_INFO s;
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(console, &s);
	DWORD written, cells = s.dwSize.X * s.dwSize.Y;
	FillConsoleOutputCharacter(console, fill, cells, tl, &written);
	FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
	SetConsoleCursorPosition(console, tl);
}

static void
execute_console_command(char cmd)
{
	printf("\n");

	switch (cmd)
	{
	case 'q': /* Exit */
		printf("Exit from the program.\n");
		DCM_Disconnection();
		debugging_delay();
		exit(0);
		break;

	case 'c': /* Connect to the controller */
		global_state = 1;
		break;

	case 'f': /* Close connection with current controller. */
		if (portnum < 0)
			break;

		printf("Close connection for the serial No. %d.\n", portnum);
		portnum = -1;
		DCM_Disconnection();
		debugging_delay();
		break;

	case 'd': /* Enable/Disable debugging */
		if (!DCM_IsLoggingEnabled())
		{
			DCM_Enable_logging(NULL);
			printf("Debugging enabled.\n");
		}
		else
		{
			DCM_Disable_logging();
			printf("Debugging disabled.\n");
		}

		Sleep(2000);
		break;
	default:
		printf("%c - unknown command!\n", cmd);
		Sleep(1000);
		break;
	}
}

static void
debugging_delay(void)
{
	if (!DCM_IsLoggingEnabled())
	{
		Sleep(1000);
		return;
	}

	printf("Press any key to continue ...\n");
	(void)_getch();
}

static void
show_status()
{
	printf("CONNECTION: ");

	if (portnum > 0)
		printf("established, serial No.: %d.\n", portnum);
	else
		printf("NONE.\n");
}

int
main()
{
	char cmd = '\0';

	do {
		clear_screen();
		show_status();

		switch (global_state)
		{
		case 0: /* main screen */
			printf("Enter command (h - for help): ");
			cmd = _getche();
			printf("\n");
			execute_console_command(cmd);
			break;
		case 1: /* Connection screen. enter COM port number or 0 for auto search. */
		{
			char pnum[2] = { '\0', '\0' };
			int num;

			printf("Enter COM port number ([1..9] or 0 for auto search): ");
			pnum[0] = _getche();
			printf("\n");
			assert(strlen(pnum) == 1);
			(void)sscanf(pnum, "%d", &num);
			assert(num >= 0 && num < 10);
			portnum = DCM_Connection(num);

			if (portnum > 0)
				printf("Connection on COM%d established.\n", portnum);
			else
				printf("Error on Serial. Connection attempt was for number %d.\n", num);

			global_state = 0;
			debugging_delay();
		}
		break;

		default:
			printf("Unknown console state!\n");
			exit(-1);
			break;
		}
	} while (cmd != 'q');
	return 0;
}
