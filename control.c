#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

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

	ret = (float)(array[sampleSize / 2] >> 4) * 0.1501769661 + 0.8169154351;
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

	char command[255];
	int setPos;
	FILE *recFile;

	fd_set s;
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;

	struct timeval curTimeval;
	double curTime;

	const int travel[11] = {5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
	const float Cvi[11] = {0, 0, 0, 0, 0, 0.125, 0, 0, 0, 0, 0.226};
	const float Xti[11] = {0, 0, 0, 0, 0, 0.1916535, 0, 0, 0, 0, 0.560641};
	const float Fdi[11] = {0.0321, 0.0738, 0.1200, 0.1533, 0.1811, 0.2055, 0.2277, 0.2483, 0.2676, 0.2860, 0.30361};
	float Cv = 0.226;
	float Xt = 0.560641;
	float Fd = 0.30361;

	int i;

	if (setjmp (buf))
		goto shutdown;

	signal (SIGINT, signalCatcher);

	adcHandle = i2c_open (1, 0x48);
	tempHandle = tempInit (1, 0x40);

	gpioOutputInit (&stepperPul, "0");
	gpioOutputInit (&stepperDir, "0");

	while (1) {
		printf (">");
		if (scanf ("%s", command) == 1) {
			if (strcmp (command, "help") == 0) {
				puts ("Accepted commands are:");
				puts ("set [steps]\tmoves the stepper-valve by the specified number of steps");
				puts ("val [valve_travel]\tsets Cv and Xt to the appropriate values");
				puts ("rec [file]\t\twrites sensor data to file untill enter is pressed");
				puts ("exit\t\t\tsafely shuts down the program (as does cntrl-c)");
			} else if (strcmp (command, "set") == 0) {
				if (scanf ("%d", &setPos) == 1) {
					printf ("Moving %d steps\n", setPos);
					stepperValve (&stepperPul, &stepperDir, setPos, 10000);
				} else {
					puts ("Bad input, enter \"help\" for list of accepted commands");
				}
			} else if (strcmp (command, "val") == 0) {
				if (scanf ("%d", &setPos) == 1) {
					for (i = 0; i < 11; i++) {
						if (setPos == travel[i]) {
							Cv = Cvi[i];
							Xt = Xti[i];
							Fd = Fdi[i];
							printf ("Set to %d percent open\n", setPos);
							printf ("Cv = %f, Xt = %f, Fd = %f\n", Cv, Xt, Fd);
							break;
						} else if (i == 10) {
							puts ("No data for this valve position, accepted values are:");
							for (i = 0; i < 10; i++)
								printf ("%d, ", travel[i]);
							printf ("%d\n", travel[10]);
						}
					}
				} else {
					puts ("Bad input, enter \"help\" for list of accepted commands");
				}
			} else if (strcmp (command, "rec") == 0) {
				if (scanf ("%s", command) == 1) {
					recFile = fopen (command, "w");
					fprintf (recFile, "time, temp, p1, p2, flow\n");
					do {
						float x;
						float C;
						float Rev;
						float Q;

						pressure1 = pressureRead (adcHandle, 0) * 0.9830463707 - 0.5100681364;
						pressure2 = pressureRead (adcHandle, 1) * 0.9799264548 - 0.4912727444;
						flow = flowRead (adcHandle, 2);
						temp = tempRead (tempHandle) + 1.0016;

						gettimeofday (&curTimeval, NULL);
						curTime = (double)curTimeval.tv_sec + (double)curTimeval.tv_usec / 1e6;

						x = (pressure1 - pressure2) / pressure1;
						C = flow * 0.06 / (22.5 * pressure1 * 6.89475729) * sqrt (28.97 * (temp + 273.15) / x);
						Rev = 7.6e-2 * Fd * flow * 0.06 / (1.461e-5 * sqrt (Cv * 0.9)) * pow (0.81 * pow (Cv, 2) / 0.277344 + 1, 0.25);

						if (Rev < 10000.0) {
							//Temp Q calculation, needs to include Fr
							Q = 16.6666667 * 22.5 * Cv * (pressure1 * 6.89475729) * (1 - x / (3 * Xt)) * sqrt (x / (28.97 * 296.15));
							printf ("Laminar,   ");
						} else if (x < Xt) {
							Q = 16.6666667 * 22.5 * Cv * (pressure1 * 6.89475729) * (1 - x / (3 * Xt)) * sqrt (x / (28.97 * 296.15));
							printf ("Turbulent, ");
						} else {
							Q = 16.6666667 * 0.6666667 * 22.5 * Cv * (pressure1 * 6.89475729) * sqrt (Xt / (28.97 * 296.15));
							printf ("Choked,    ");
						}

						printf ("%f, %f, %f, %f, %f, %f\n", pressure1, pressure2, x, C, Rev, Q);

						fprintf (recFile, "%lf, %f, %f, %f, %f\n", curTime, temp, pressure1, pressure2, flow);

						fflush (stdout);
						FD_ZERO (&s);
						FD_SET (STDIN_FILENO, &s);
						select (STDIN_FILENO + 1, &s, NULL, NULL, &timeout);
					} while (FD_ISSET (STDIN_FILENO, &s) == 0);
					fclose (recFile);
				} else {
					puts ("Bad input, enter \"help\" for list of accepted commands");
				}
			} else if (strcmp (command, "exit") == 0) {
				goto shutdown;
			} else {
				puts ("Bad input, enter \"help\" for list of accepted commands");
			}
		} else {
			puts ("Bad input, enter \"help\" for list of accepted commands");
		}
	}

shutdown:
	gpioOutputTerminate (&stepperPul, "0");
	gpioOutputTerminate (&stepperDir, "0");

	return 0;
}

