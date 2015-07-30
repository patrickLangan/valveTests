#include <stdio.h>
#include <signal.h>
#include <setjmp.h>

static jmp_buf buf;

struct gpioInfo {
	int pin;
	FILE *file;
};

void signalCatcher (int null)
{
	longjmp (buf, 1);
}


int compareInts (const void *a, const void *b)
{
	const int *fa = (const int *)a;
	const int *fb = (const int *)b;

	return (*fa > *fb) - (*fa < *fb);
}

/*
 * Honeywell PX2AN2XX150PAAAX analog pressure sensor: http://www.farnell.com/datasheets/1514338.pdf
 * Adafruit ADS1015 ADC: http://www.adafruit.com/products/1083
 * Constants found from adafruit source code: https://github.com/adafruit/Adafruit_ADS1X15
 */
float pressureRead (int handle, int channel)
{
	const int sampleSize = 10;
	int array[sampleSize];
	char buffer[3];
	int i;

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
	i2c_write (handle, buffer, 3);

	for (i = 0; i < sampleSize; i++) {
		i2c_write_byte (handle, 0x00);
		i2c_read (handle, buffer, 2);
		array[i] = (int)buffer[0] << 8 | (int)buffer[1];
	}

	qsort (array, sampleSize, sizeof(*array), compareInts);
	return (float)(array[sampleSize / 2] >> 4) * 0.1125 - 18.75;
}

/*
 * Automation Direct TTD25N-20-0100C-H temperature sensor: http://www.automationdirect.com/static/specs/prosensettrans.pdf
 * Adafruit INA219 Current Monitor: http://www.adafruit.com/products/904
 * Constants found from adafruit source code: https://github.com/adafruit/Adafruit_INA219
 */
int tempInit (int bus, int address)
{
	int handle;
	char buffer[3];

	handle = i2c_open (bus, address);

	buffer[0] = 0x05;
	buffer[1] = 0x0A;
	buffer[2] = 0xAA;
	i2c_write (handle, buffer, 3);

	buffer[0] = 0x00;
	buffer[1] = 0x3C;
	buffer[2] = 0x1F;
	i2c_write (handle, buffer, 3);

	return handle;
}

float tempRead (int handle)
{
	const int sampleSize = 30;
	int array[sampleSize];
	char buffer[3];
	int i;

	buffer[0] = 0x05;
	buffer[1] = 0x0A;
	buffer[2] = 0xAA;
	i2c_write (handle, buffer, 3);

	for (i = 0; i < sampleSize; i++) {
		i2c_write_byte (handle, 0x04);
		i2c_read (handle, buffer, 2);
		array[i] = (int)buffer[0] << 8 | (int)buffer[1];
	}

	qsort (array, sampleSize, sizeof(*array), compareInts);
	return (float)array[sampleSize / 2] * 0.00625 - 25;
}


void gpioOutputInit (struct gpioInfo *gpio, char *value)
{
        char gpioPath[30];

        sprintf (gpioPath, "/sys/class/gpio/gpio%d/value", gpio->pin);
        gpio->file = fopen (gpioPath, "w");
        fprintf (gpio->file, value);
        fflush (gpio->file);
}


void gpioOutputTerminate (struct gpioInfo *gpio, char *value)
{
        fprintf (gpio->file, value);
        fclose (gpio->file);
}

void gpioOutput (struct gpioInfo *gpio, int value)
{
	switch (value) {
	case 0: 
		fprintf (gpio->file, "0");
		break;
	case 1: 
		fprintf (gpio->file, "1");
	}

	fflush (gpio->file);
}

int main (int argc, char **argv)
{
	int pressureHandle;
	int tempHandle;

	float pressure1;
	float pressure2;
	float temp;

	struct gpioInfo stepperPul = {65};
	struct gpioInfo stepperDir = {66};

	int i;

	if (setjmp (buf))
		goto shutdown;

	signal (SIGINT, signalCatcher);

	pressureHandle = i2c_open (1, 0x48);
	tempHandle = tempInit (1, 0x40);

	gpioOutputInit (&stepperPul, "0");
	gpioOutputInit (&stepperDir, "0");

	while (1) {
		pressure1 = pressureRead (pressureHandle, 0);
		pressure2 = pressureRead (pressureHandle, 1);

		temp = tempRead (tempHandle);

		printf ("%f, %f, %f\n", temp, pressure1, pressure2);
	}

shutdown:
	gpioOutputTerminate (&stepperPul, "0");
	gpioOutputTerminate (&stepperDir, "0");

	return 0;
}

