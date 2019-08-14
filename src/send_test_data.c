#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {

	char out0[] = {'m', 'e'};

	// out1 contains a complete RTCM message plus others.
	char out1[] = {'s', 's', 'a', 'g', 'e', '\n',
		'm', 'e', 's', 's', 'a', 'g', 'e', '\n',
		0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x0E,
		0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62, 0xAC, 0x09, 0x41, 0x98,
		0x6F, 0x33, 0x36, 0x0B, 0x98, 'm', 'e'};

	// out2 contains no RTCM messages.
	char out2[] = {'m', 'e', 's', 's', 'a', 'g', 'e', '\n',
		'm', 'e', 's', 's', 'a', 'g', 'e', '\n'};

	// out3 starts an RTCM message which is completed in out4.
	char out3[] = {'t', 'h', 'i', 's', ' ', 'i', 's', ' ', 't', 'h', 'e', '\n',
			's',  't', 'a', 'r', 't', ' ', 'o', 'f', ' ', 'a', ' ',
			'l', 'o', 'n', 'g', ' ', 'm', 'e', 's', 's', 'a', 'g', 'e', '\n',
		0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x0E,
		0xDE, 0xEF, 0x34, 0xB4, 0xBD};

	char out4[] = {0x62, 0xAC, 0x09, 0x41, 0x98,
		0x6F, 0x33, 0x36, 0x0B, 0x98,
		'm', 'e', 's', 's', 'a', 'g', 'e', '\n'};

	// out5 contains just non-RTCM messages.
	char out5[] = {'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', ' ',
		'm', 'e', 's', 's', 'a', 'g', 'e', ' ',
		'f', 'o', 'l', 'l', 'o', 'w', 's', '\n'};

	// out6 contains exactly one message, an RTCM.
	char out6[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x0E,
		0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62, 0xAC, 0x09, 0x41, 0x98,
		0x6F, 0x33, 0x36, 0x0B, 0x98};

	// Edge case: out7 starts an RTCM message but the length value is incomplete.
	// The complete message spans three buffers.
	char out7[] = {'e', 'd', 'g', 'e', ' ', 'c', 'a', 's', 'e', ':', ' ',
		'n',  'o', ' ', 'l', 'e', 'n', 'g', 't', 'h', ' ', 'y', 'e', 't', '\n',
		0xD3, 0x00};

	char out8[] = {0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x0E};

	char out9[] = {
		0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62, 0xAC, 0x09, 0x41, 0x98,
		0x6F, 0x33, 0x36, 0x0B, 0x98,
		'm', 'e', 's', 's', 'a', 'g', 'e', '\n',
		'm', 'e', 's', 's', 'a', 'g', 'e', '\n'};

	// out10 starts an RTCM message which is completed in out11.
	char out10[] = {'m', 'e', 's', 's', 'a', 'g', 'e', '\n',
		0xD3, 0x00};

	// out11 exactly completes the RTCM message started in out11.
	char out11[] = {0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x0E,
		0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62, 0xAC, 0x09, 0x41, 0x98,
		0x6F, 0x33, 0x36, 0x0B, 0x98};

	write(STDOUT_FILENO, out0, sizeof(out0)); sleep(1);
	write(STDOUT_FILENO, out1, sizeof(out1)); sleep(1);
	write(STDOUT_FILENO, out2, sizeof(out2)); sleep(1);
	write(STDOUT_FILENO, out3, sizeof(out3)); sleep(1);
	write(STDOUT_FILENO, out4, sizeof(out4)); sleep(1);
	write(STDOUT_FILENO, out5, sizeof(out5)); sleep(1);
	write(STDOUT_FILENO, out6, sizeof(out6)); sleep(1);
	write(STDOUT_FILENO, out7, sizeof(out7)); sleep(1);
	write(STDOUT_FILENO, out8, sizeof(out8)); sleep(1);
	write(STDOUT_FILENO, out9, sizeof(out9)); sleep(1);
	write(STDOUT_FILENO, out10, sizeof(out10)); sleep(1);
	write(STDOUT_FILENO, out11, sizeof(out11));

	return 0;
}


