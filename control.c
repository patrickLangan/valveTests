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
 * Adafruit ADS1015 ADC: http://www.adafruit.com/products/1083
 * Constants found from adafruit source code: https://github.com/adafruit/Adafruit_ADS1X15
 */
int adcRead (int handle, int channel)
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
		fprintf (stderr, "adcRead() expects a channel number between 0 and 3.\n");
		break;
	};

	buffer[2] = 0x83;
	i2c_write (handle, buffer, 3);
	i2c_write (handle, buffer, 3);

	i2c_write_byte (handle, 0x00);
	i2c_read (handle, buffer, 2);
	return (int)buffer[0] << 8 | (int)buffer[1];
}

/*
 * Honeywell PX2AN2XX150PAAAX analog pressure sensor: http://www.farnell.com/datasheets/1514338.pdf
 */
float pressureRead (int handle, int channel)
{
	const int sampleSize = 10;
	int array[sampleSize];
	int i;

	for (i = 0; i < sampleSize; i++)
		array[i] = adcRead (handle, channel);

	qsort (array, sampleSize, sizeof(*array), compareInts);
	return (float)(array[sampleSize / 2] >> 4) * 0.1125 - 18.75;
}

/*
 * Alicat Scientific M-250SLPM-D compressed air flow sensor: http://www.alicat.com/documents/specifications/Alicat_Mass_Meter_Specs.pdf
 */
float flowRead (int handle, int channel)
{
	const int sampleSize = 10;
	int array[sampleSize];
	float ret;
	int i;

	for (i = 0; i < sampleSize; i++)
		array[i] = adcRead (handle, channel);

	qsort (array, sampleSize, sizeof(*array), compareInts);

	ret = (float)(array[sampleSize / 2] >> 4) * 0.1500596393 + 0.2813477731;
	if (ret > 300.0)
		ret = 0;

	return ret;
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

/*
 * Moves the stepper-valve a given number of steps, with a given period of time (in uSecs) between steps
 */
void stepperValve (struct gpioInfo *pul, struct gpioInfo *dir, int steps, int period)
{
	period /= 2;

	if (steps < 0) {
		steps = abs (steps);
		fprintf (dir->file, "0");
	} else {
		fprintf (dir->file, "1");
	}
	fflush (dir->file);

	for (; steps > 0; steps--) {
		fprintf (pul->file, "1");
		fflush (pul->file);
		usleep (period);
		fprintf (pul->file, "0");
		fflush (pul->file);
		usleep (period);
	}
}

int main (int argc, char **argv)
{
	int adcHandle;
	int tempHandle;

	float pressure1;
	float pressure2;
	float flow;
	float temp;

	struct gpioInfo stepperPul = {65};
	struct gpioInfo stepperDir = {66};

	int i;

	if (setjmp (buf))
		goto shutdown;

	signal (SIGINT, signalCatcher);

	adcHandle = i2c_open (1, 0x48);
	tempHandle = tempInit (1, 0x40);

	gpioOutputInit (&stepperPul, "0");
	gpioOutputInit (&stepperDir, "0");

	while (1) {
		pressure1 = pressureRead (adcHandle, 0);
		pressure2 = pressureRead (adcHandle, 1);

		flow = flowRead (adcHandle, 2);

		temp = tempRead (tempHandle);

		printf ("%f, %f, %f, %f\n", temp, pressure1, pressure2, flow);
	}

/*
	while (1) {
		stepperValve (&stepperPul, &stepperDir, 40, 10000);
		stepperValve (&stepperPul, &stepperDir, -40, 10000);
	}
*/

shutdown:
	gpioOutputTerminate (&stepperPul, "0");
	gpioOutputTerminate (&stepperDir, "0");

	return 0;
}

