#include <assert.h>
#include <conio.h>
#include <cstdio>
#include <windows.h>

#include "errcodes.h"
#include "delta.h"

static int global_state = 0;

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
show_help()
{
	printf("Commands:\n");
	printf("a - add a product with 13-digit EAN code.\n");
	printf("e - extract a product with 13-digit EAN code.\n");
	printf("c - connect to controller.\n");
	printf("d - enable/disable logging.\n");
	printf("f - close connection to controller.\n");
	printf("h - this text.\n");
	printf("i - create database file (if still doesn't exists).\n");
	printf("r - reset database.\n");
	printf("z - dump database.\n");
	printf("q - exit from this program.\n");
}

static void
execute_console_command(char cmd)
{
	printf("\n");

	switch (cmd)
	{
	case 'a': /* Add goods. */
		global_state = 2;
		//		debugging_delay();
		break;

	case 'e': /* Extract goods. */
		global_state = 3;
		//		debugging_delay();
		break;

	case 'q': /* Exit */
		printf("Exit from the program.\n");
		DCM_Disconnection();
		debugging_delay();
		exit(0);
		break;

	case 'c': /* Connect to the controller */
		global_state = 1;
		break;

	case 'i': /* Create database file. */
		DCM_Create_database();
		break;

	case 'h':
		show_help();
		printf("Press any key to continue ...\n");
		(void)_getch();
		break;

	case 'f': /* Close connection to current controller. */
		if (DCM_Conn_serial() < 0)
			break;

		printf("Close connection for the serial No. %d.\n", DCM_Conn_serial());
		DCM_Disconnection();
		debugging_delay();
		break;

	case 'd': /* Enable/Disable debugging */
		if (!DCM_IsLoggingEnabled())
		{
			DCM_Enable_logging(stdout);
			printf("Debugging enabled.\n");
		}
		else
		{
			DCM_Disable_logging();
			printf("Debugging disabled.\n");
		}

		Sleep(2000);
		FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
		break;

	case 'r': /* Clear database state. */
	{
		char smb;

		printf("Do you really want to clear database? (y/n) ");
		smb = _getche();
		printf("\n");
		if (smb == 'y')
		{
			DCM_Clear();
			debugging_delay();
		}
		break;
	}

	case 'z': /* Dump database */
		printf("Dump database.\n");
		DCM_Dump_database();
		debugging_delay();
		break;

	default:
		printf("%c - unknown command!\n", cmd);
		Sleep(1000);

		/* Discard all input before reading new command. */
		FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
		break;
	}
}

static void
debugging_delay(void)
{
	if (!DCM_IsLoggingEnabled())
	{
		Sleep(1000);

		/* Discard all input before reading new command. */
		FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
		return;
	}

	printf("Press any key to continue ...\n");
	(void)_getch();
}

static void
show_status()
{
	const char* path;

	printf("Data directory: ");
	if ((path = DCM_Get_path()) != NULL)
		printf("%s\n", path);
	else
		printf("not created.\n");

	printf("CONNECTION: ");

	if (DCM_Conn_serial() > 0)
		printf("established, serial No.: %d.\n", DCM_Conn_serial());
	else
		printf("NONE.\n");

	printf("DATABASE: ");
	if (DCM_Check_database())
		printf("connected\n");
	else
		printf("not connected (%s)\n", DCMErrStr);
}

int
main(int argc, char** argv)
{
	char cmd = '\0';
	dcm_errcode_t errcode;

	assert(argc >= 0);

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

			if ((errcode = DCM_Connection(num)) == SUCCESS)
				printf("Connection on COM%d established.\n", DCM_Conn_serial());
			else
				printf("Error on Serial. Connection attempt was for number %d (errcode=%d).\n", num, errcode);

			global_state = 0;
			debugging_delay();
		}
		break;

		case 2: /* Adding a product. */
		{
			code_t code = { 0 };
			int count;

			printf("Write the 13-digit code and press enter: ");
			FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));

			count = scanf("%13s", code);

			if (!checkEANcode(code))
				printf("Incorrect EAN code '%s'!", code);
			else
			{
				if (!DCM_Add_item(code))
					printf("A product addition problems.\nDETAILS: %s", DCMErrStr);
			}

			global_state = 0;
			debugging_delay();
		}
		break;

		case 3: /* Extract a product. */
		{
			code_t code = { 0 };
			int count;

			printf("Write the 13-digit code (or 0) and press ENTER: ");
			FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));

			count = scanf("%13s", code);
			assert(count == 1);

			if (strcmp(code, "0") == 0)
			{
				unsigned int cellnum;

				/* Extract product by cell number */
				printf("Write the cell number in the range from 1 to %d and press ENTER: ", CELLS_NUMBER);
				FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
				count = scanf("%u", &cellnum);
	
				if (count != 1 || cellnum < 1 || cellnum > CELLS_NUMBER)
					printf("Incorrect cell number: '%d'!\n", cellnum);

				if ((errcode = DCM_Extract_from_cell(cellnum, code)) != SUCCESS)
					printf("Error of extracting a product from the cell %u (errcode=%d).\n", cellnum, errcode);
			}
			else if (!checkEANcode(code))
				printf("Incorrect EAN code '%s'!", code);
			else
			{
				if ((errcode = DCM_Get_item(code)) != SUCCESS)
					printf("A product extraction with EAN '%s' problems (errcode=%d).\n", code, errcode);
			}

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
