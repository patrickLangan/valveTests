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
float pressureRead (int handle, int channel)
{
	char buffer[3];

	buffer[0] = 0x01;

	switch (channel) {
	case 0:
		buffer[1] = 0xC1;
		break;
	case 1:
		buffer[1] = 0xD1;
		break;
	case 2:
		buffer[1] = 0xE1;
		break;
	case 3:
		buffer[1] = 0xF1;
		break;
	default:
		fprintf (stderr, "pressureRead() expects a channel number between 0 and 3.\n");
		break;
	};

	buffer[2] = 0x83;
	i2c_write (handle, buffer, 3);

	i2c_write_byte (handle, 0x00);
	i2c_read (handle, buffer, 2);
	return (float)(((int)buffer[0] << 8 | (int)buffer[1]) >> 4) * 0.1125 - 18.75;
}

int main (int argc, char **argv)
{
	int pressureHandle;

	float pressure1;
	float pressure2;

	if (setjmp (buf))
		goto shutdown;

	signal (SIGINT, signalCatcher);

	pressureHandle = i2c_open (1, 0x48);

	while (1) {
		pressure1 = pressureRead (pressureHandle, 0);
		pressure2 = pressureRead (pressureHandle, 1);

		printf ("%f, %f\n", pressure1, pressure2);
	}

shutdown:

	return 0;
}

