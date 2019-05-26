/*
 * dwmstatus - a small program to update dwm window manager status
 * line for OpenBSD.
 *
 * Copyright (c) 2019 Michael Cardell Widerkrantz, mc at the domain hack.org.
 *
 * https://hack.org/mc/
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <machine/apmvar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>

int sigcode;

static void	sigcatch(int);
static void	settitle(xcb_connection_t *, xcb_screen_t *, char *);
static int	readtemp(void);
static void	readbat(int, char **, int *);

void
sigcatch(int sig) {
	sigcode = sig;
}

void
settitle(xcb_connection_t *conn, xcb_screen_t *screen, char *status) {
	xcb_void_cookie_t cookie;

	cookie = xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE,
					     screen->root,
					     XCB_ATOM_WM_NAME,
					     XCB_ATOM_STRING, 8,
					     strlen(status), status);

	xcb_flush(conn);
	if (NULL != xcb_request_check(conn, cookie)) {
		printf("Couldn't set X server's root window name\n");
		exit(1);
	}
}

int
readtemp(void) {
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

void
readbat(int apmfd, char **batstring, int *batterylife) {
	struct apm_power_info info;

	ioctl(apmfd, APM_IOC_GETPOWER, &info);
	*batterylife = info.battery_life;

	switch (info.ac_state)
	{
	case APM_AC_OFF:
		*batstring = "-";
		break;
	case APM_AC_ON:
		*batstring = "+";
		break;
	default:
		*batstring = "U";
		break;
	}

	if (info.ac_state == 0 && info.battery_life < 15) {
		*batstring = "!!!";
	}
}

int
main(void) {
	int apmfd;
	time_t rawtime;
	struct tm *timeinfo;
	char timestr[17];
	char *batstring = "";
	int batterypercent;
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

	apmfd = open("/dev/apm", O_RDONLY);
	if (apmfd == -1) {
		perror("opening /dev/apm");
		exit(1);
	}

	while (1) {
		temp = readtemp();
		readbat(apmfd, &batstring, &batterypercent);
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(timestr, 17, "%F %R", timeinfo);

		snprintf(status, 80, "Bat %s%d%% | %d°C | %s", batstring,
			 batterypercent, temp, timestr);
		// printf("%s\n", status);

		settitle(conn, screen, status);

		// Interrupted if we're sent a SIGHUP
		sleep(10);
	}

	return 0;
}
