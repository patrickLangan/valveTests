#!/bin/bash

enableGpio(){
	if [ -f /sys/class/gpio/gpio"$1" ]; then
		echo gpio"$1" already enabled
	else
		echo "$1" > /sys/class/gpio/export
	fi

	if [ -f /sys/class/gpio/gpio"$1"/direction ]; then
		echo "$2" > /sys/class/gpio/gpio"$1"/direction
	else
		echo "$1" gpio direction file not found
		exit 0;
	fi

	if [ -f /sys/class/gpio/gpio"$1"/value ]; then
		if [ "$2" == "out" ]; then
			echo "$3" > /sys/class/gpio/gpio"$1"/value
		fi
	else
		echo "$1" gpio value file not found
		exit 0;
	fi
}

#Set i2c to 400hz
echo i2c1-400hz > /sys/devices/bone_capemgr.*/slots
echo i2c2-400hz > /sys/devices/bone_capemgr.*/slots
#Enable gpio pins
echo gpio-enable > /sys/devices/bone_capemgr.*/slots

cat /sys/devices/bone_capemgr.*/slots

sleep 1

#Enable stepper driver pulse/dir
enableGpio 65 "out" 0
enableGpio 66 "out" 0

