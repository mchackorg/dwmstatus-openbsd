#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <machine/apmvar.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>

int sigcode;

void sigcatch(int);

void
sigcatch(int sig)
{
	sigcode = sig;
}

int
readtemp() {
	struct sensordev sensordev;
	struct sensor sensor;
	size_t sdlen, slen;
	int dev, mib[5] = {CTL_HW, HW_SENSORS, 0, SENSOR_TEMP, 0};

	sdlen = sizeof(sensordev);
	slen = sizeof(sensor);

	/* Step through devices on this level looking for cpu0 */
	for (dev = 0;; dev++) {
		mib[2] = dev;
		if (sysctl(mib, 3, &sensordev, &sdlen, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			return 0;
		}

		if (strcmp(sensordev.xname, "cpu0") == 0) {
			if (sysctl(mib, 5, &sensor, &slen, NULL, 0) == -1) {
				if (errno != ENOENT) {
					warn("sysctl");
					continue;
				}
			}
			// Temperature is microkelvin. Convert to Celsius
			return (sensor.value - 273150000) / 1000000.0;
		}
	}

	return 0;
}

int
main(void) {
	struct apm_power_info info;
	int apmfd;
	time_t rawtime;
	struct tm *timeinfo;
	char timestr[17];
	char *batstring;
	int scrno;
	char status[80];
	xcb_connection_t *conn;
	xcb_screen_t *screen;
	int temp;

	conn = xcb_connect(NULL, &scrno);
	if (!conn) {
		fprintf(stderr, "can't connect to an X server\n");
		exit(1);
	}

	screen = xcb_aux_get_screen(conn, scrno);

	if (SIG_ERR == signal(SIGHUP, sigcatch)) {
		perror("signal");
		exit(1);
	}

	temp = readtemp();

	apmfd = open("/dev/apm", O_RDONLY);

	while (1) {
		ioctl(apmfd, APM_IOC_GETPOWER, &info);
		// printf("info.battery_life: %d\nstate: %d\nac_state: %d\nminutes_left: %d\n", info.battery_life, info.battery_state, info.ac_state, info.minutes_left);

		switch (info.ac_state)
		{
		case APM_AC_OFF:
			batstring = "-";
			break;
		case APM_AC_ON:
			batstring = "+";
			break;
		default:
			batstring = "U";
			break;
		}

		if (info.ac_state == 0 && info.battery_life < 15) {
			batstring = "!!!";
		}

		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(timestr, 17, "%F %R", timeinfo);

		// snprintf(status, 80, "Bat %s%d%% | %d°C | Vol %d%% | %s", batstring, bat, temp, vol & 0x7f, timestr);

		snprintf(status, 80, "Bat %s%d%% | %d°C | %s", batstring, info.battery_life, temp, timestr);
		// printf("%s\n", status);

		xcb_void_cookie_t cookie;

		cookie = xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE, screen->root, XCB_ATOM_WM_NAME,
						     XCB_ATOM_STRING, 8, strlen(status), status);

		xcb_flush(conn);
		if (NULL != xcb_request_check(conn, cookie)) {
			printf("Couldn't set X server's root window name\n");
			exit(-1);
		}

		// Interrupted if we're sent a SIGHUP
		sleep(10);
	}

	return 0;
}
