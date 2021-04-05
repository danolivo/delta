#include <conio.h>
#include <cstdio>
#include <windows.h>

static int global_state = 0;

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
		exit(0);
		break;

	case 'c': /* Connect to the controller */
		global_state = 1;
		break;
	default:
		printf("%c - unknown command!\n", cmd);
		Sleep(1000);
		break;
	}
}

int
main()
{
	char cmd = '\0';
	int portnum;

	do {
		clear_screen();

		switch (global_state)
		{
		case 0: /* main screen */
			printf("Enter command (h - for help): ");
			cmd = _getch();
			execute_console_command(cmd);
			break;
		case 1: /* Connection screen. enter COM port number or 0 for auto search. */
			printf("Enter COM port number (or 0 for auto search): ");
			scanf("%d", &portnum);
			printf("Try to connect on COM%d ...", portnum);
			Sleep(2000);
			global_state = 0;
			break;
		default:
			printf("Unknown console state!\n");
			exit(-1);
			break;
		}
	} while (cmd != 'q');
	return 0;
}
