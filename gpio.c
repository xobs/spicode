#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "gpio.h"

#define GPIO_PATH "/sys/class/gpio"
#define EXPORT_PATH GPIO_PATH "/export"
#define UNEXPORT_PATH GPIO_PATH "/unexport"

static int gpio_is_exported(int gpio) {
	char gpio_path[256];
	struct stat buf;
	int ret;
	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/direction", gpio);
	ret = stat(gpio_path, &buf);
	if (ret == -1)
		return 0;
	return 1;
}


static int gpio_export_unexport(char *path, int gpio) {
	int fd;
	char str[16];
	int bytes;

	fd = open(path, O_WRONLY);
	if (fd == -1) {
		perror("Unable to find GPIO files -- /sys/class/gpio enabled?");
		return -errno;
	}

	bytes = snprintf(str, sizeof(str)-1, "%d", gpio) + 1;

	if (-1 == write(fd, str, bytes)) {
		fprintf(stderr, "Unable to modify gpio%d: %s",
			gpio, strerror(errno));
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}

int gpio_export(int gpio) {
	if (gpio_is_exported(gpio))
		return 0;
	return gpio_export_unexport(EXPORT_PATH, gpio);
}

int gpio_unexport(int gpio) {
	if (!gpio_is_exported(gpio))
		return 0;
	return gpio_export_unexport(UNEXPORT_PATH, gpio);
}

int gpio_set_direction(int gpio, int is_output) {
	char gpio_path[256];
	int fd;
	int ret;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/direction", gpio);

	fd = open(gpio_path, O_WRONLY);
	if (fd == -1) {
		fprintf(stderr, "Direction file: [%s]\n", gpio_path);
		perror("Couldn't open direction file for gpio");
		return -errno;
	}

	if (is_output)
		ret = write(fd, "out", 4);
	else
		ret = write(fd, "in", 3);

	if (ret == -1) {
		perror("Couldn't set output direction");
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}


int gpio_set_value(int gpio, int value) {
	char gpio_path[256];
	int fd;
	int ret;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/value", gpio);

	fd = open(gpio_path, O_WRONLY);
	if (fd == -1) {
		char errormsg[256];
		snprintf(errormsg, sizeof(errormsg)-1, "Value file %s: %s\n",
				gpio_path, strerror(errno));
		fprintf(stderr, errormsg);
		return -errno;
	}

	if (value)
		ret = write(fd, "1", 2);
	else
		ret = write(fd, "0", 2);

	if (ret == -1) {
		fprintf(stderr, "Couldn't set GPIO %d output value: %s\n",
			gpio, strerror(errno));
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}


int gpio_get_value(int gpio) {
	char gpio_path[256];
	int fd;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/value", gpio);

	fd = open(gpio_path, O_RDONLY);
	if (fd == -1) {
		perror("Couldn't open value file for gpio");
		return -errno;
	}

	if (read(fd, gpio_path, sizeof(gpio_path)) <= 0) {
		perror("Couldn't get input value");
		close(fd);
		return -errno;
	}

	close(fd);

	return gpio_path[0] != '0';
}


