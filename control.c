#include <stdio.h>
#include <signal.h>
#include <setjmp.h>

static jmp_buf buf;

void signalCatcher (int null)
{
	longjmp (buf, 1);
}

/*
 * Honeywell PX2AN2XX150PAAAX analog pressure sensor: http://www.farnell.com/datasheets/1514338.pdf
 * Adafruit ADS1015 ADC: http://www.adafruit.com/products/1083
 * Constants found from adafruit source code: https://github.com/adafruit/Adafruit_ADS1X15
 */
float pressureRead (int handle)
{
	char buffer[3];

	buffer[0] = 0x01;
	buffer[1] = 0xC1;
	buffer[2] = 0x83;
	i2c_write (handle, buffer, 3);

	i2c_write_byte (handle, 0x00);
	i2c_read (handle, buffer, 2);
	return (float)(((int)buffer[0] << 8 | (int)buffer[1]) >> 4) * 0.1125 - 18.75;
}

int main (int argc, char **argv)
{
	int handlePress1;
	float pressure1;

	if (setjmp (buf))
		goto shutdown;

	signal (SIGINT, signalCatcher);

	handlePress1 = i2c_open (1, 0x48);

	while (1) {
		pressure1 = pressureRead (handlePress1);
		printf ("%f\n", pressure1);
	}

shutdown:

	return 0;
}

