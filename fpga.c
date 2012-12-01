#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include "sd.h"
#include "gpio.h"

static int data_pins[] = {
	45, // CAM_D[0]
	44, // CAM_D[1]
	42, // CAM_D[2]
	41, // CAM_D[3]
	40, // CAM_D[4]
	68, // LCD_G[2]
	38, // CAM_D[6]
	37, // CAM_D[7]
	63, // LCD_R[3]
	64, // LCD_R[4]
	65, // LCD_R[5]
	66, // LCD_G[0]
	67, // LCD_G[1]
	69, // LCD_G[3]
	70, // LCD_G[4]
	71, // LCD_G[5]
};

static int bank_select_pins[] = {
	57, // LCD_HS
	56, // LCD_VS
};

#define DATA_READY_PIN 61
#define GPIO_PATH "/sys/class/gpio"
#define GET_NEW_SAMPLE_PIN 54


int fpga_get_new_sample(struct sd *st, uint8_t bytes[8]) {
	uint8_t data[16];
	int tries;
	int bank;
	int i;
	int ret;
	usleep(2);
	gpio_set_value(GET_NEW_SAMPLE_PIN, 0);
	usleep(2);
	gpio_set_value(GET_NEW_SAMPLE_PIN, 1);

	ret = -1;
	for (tries=0; tries<2; tries++) {
		if (gpio_get_value(60)) {
			DBG("Gpio went high after %d tries", tries);
			ret = 0;
			break;
		}
		usleep(1);
	}
	DBG("New sample never went ready!");

	for (bank=0; bank<4; bank++) {
		gpio_set_value(bank_select_pins[0], !!(bank&1));
		gpio_set_value(bank_select_pins[1], !!(bank&2));
		usleep(2);
		for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++)
			data[i] = gpio_get_value(data_pins[i]);
		bytes[bank*2+0] = (data[0]<<0)
				| (data[1]<<1)
				| (data[2]<<2)
				| (data[3]<<3)
				| (data[4]<<4)
				| (data[5]<<5)
				| (data[6]<<6)
				| (data[7]<<7);

		bytes[bank*2+1] = (data[8]<<0)
				| (data[9]<<1)
				| (data[10]<<2)
				| (data[11]<<3)
				| (data[12]<<4)
				| (data[13]<<5)
				| (data[14]<<6)
				| (data[15]<<7);
	}
	return ret;
}

int fpga_data_avail(struct sd *st) {
/*
	struct pollfd fdset[1];
	fdset[0].fd = st->fpga_fd;
	fdset[0].events = POLLPRI;
	if (-1 == poll(fdset, 1, 3*1000)) {
		perror("Error polling");
	}
*/
	return gpio_get_value(DATA_READY_PIN);
}


int fpga_init(struct sd *sd) {
	int i;
	char str[256];

	/* Grab the "data ready pin", and open it so we can poll() */
	gpio_export(DATA_READY_PIN);
	gpio_set_direction(DATA_READY_PIN, GPIO_IN);
	snprintf(str, sizeof(str)-1, "%s/gpio%d/value", GPIO_PATH, DATA_READY_PIN);
	sd->fpga_fd = open(str, O_RDONLY | O_NONBLOCK);
	if (sd->fpga_fd == -1)
		return -1;

	for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++) {
		gpio_export(data_pins[i]);
		gpio_set_direction(data_pins[i], GPIO_IN);
	}

	gpio_export(GET_NEW_SAMPLE_PIN);
	gpio_set_direction(GET_NEW_SAMPLE_PIN, GPIO_OUT);

	for (i=0; i<sizeof(bank_select_pins)/sizeof(*bank_select_pins); i++) {
		gpio_export(bank_select_pins[i]);
		gpio_set_direction(bank_select_pins[i], GPIO_OUT);
		gpio_set_value(bank_select_pins[i], 0);
	}
	return 0;
}


void *fpga_thread(void *arg) {
	struct sd *sd = arg;
	while (!sd->should_exit) {
		uint8_t data[8];

		if (!fpga_data_avail(sd))
			continue;
	
		/* Obtain the new sample and send it over the wire */
		fpga_get_new_sample(sd, data);
		net_write_data(sd, data, sizeof(data));
	}

	return NULL;
}

