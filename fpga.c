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

enum fpga_pkt_type {
	PKT_NAND = 0,
	PKT_SD_CMD = 1,
	PKT_SD_RESPONSE = 2,
};

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
#define CLOCK_OVERFLOW_PIN 72
#define GPIO_PATH "/sys/class/gpio"
#define GET_NEW_SAMPLE_PIN 54
#define SAMPLE_READY_PIN 60


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
		if (gpio_get_value(SAMPLE_READY_PIN)) {
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
	return gpio_get_value(DATA_READY_PIN);
}


int fpga_init(struct sd *sd) {
	int i;
	char str[256];

	/* Grab the "data ready pin", and open it so we can poll() */
	gpio_export(DATA_READY_PIN);
	gpio_set_direction(DATA_READY_PIN, GPIO_IN);
	gpio_set_edge(DATA_READY_PIN, GPIO_EDGE_BOTH);
	snprintf(str, sizeof(str)-1, "%s/gpio%d/value", GPIO_PATH, DATA_READY_PIN);
	sd->fpga_ready_fd = open(str, O_RDONLY | O_NONBLOCK);
	if (sd->fpga_ready_fd == -1)
		return -1;

	gpio_export(CLOCK_OVERFLOW_PIN);
	gpio_set_direction(CLOCK_OVERFLOW_PIN, GPIO_IN);
	gpio_set_edge(CLOCK_OVERFLOW_PIN, GPIO_EDGE_BOTH);
	snprintf(str, sizeof(str)-1, "%s/gpio%d/value", GPIO_PATH, CLOCK_OVERFLOW_PIN);
	sd->fpga_overflow_fd = open(str, O_RDONLY | O_NONBLOCK);
	if (sd->fpga_overflow_fd == -1)
		return -1;

	for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++) {
		gpio_export(data_pins[i]);
		gpio_set_direction(data_pins[i], GPIO_IN);
	}

	gpio_export(GET_NEW_SAMPLE_PIN);
	gpio_set_direction(GET_NEW_SAMPLE_PIN, GPIO_OUT);

	gpio_export(SAMPLE_READY_PIN);
	gpio_set_direction(SAMPLE_READY_PIN, GPIO_IN);

	for (i=0; i<sizeof(bank_select_pins)/sizeof(*bank_select_pins); i++) {
		gpio_export(bank_select_pins[i]);
		gpio_set_direction(bank_select_pins[i], GPIO_OUT);
		gpio_set_value(bank_select_pins[i], 0);
	}

	return 0;
}


int fpga_read_data(struct sd *sd) {
	uint8_t pkt[8];
	if (!fpga_data_avail(sd)) {
		fprintf(stderr, "No data avilable!\n");
		return -1;
	}
	
	/* Obtain the new sample and send it over the wire */
	fpga_get_new_sample(sd, pkt);

	uint32_t fpga_counter;
	uint8_t pkt_type;

	memcpy(&fpga_counter, pkt, sizeof(fpga_counter));
	pkt_type = pkt[4] & 0x0f;

	if (pkt_type == PKT_NAND) {
		uint8_t data;
		uint8_t ctrl;
		uint8_t unknown[2];
                data = ((pkt[4] & 0xf0) >> 4) | ((pkt[5] & 0x0f) << 4);
		ctrl = ((pkt[5] & 0xf0) >> 4) | ((pkt[6] & 0x03) << 4);
		unknown[0] = ((pkt[6] & 0xfc) >> 2) | ((pkt[7] & 0x03) << 6);
		unknown[1] = ((pkt[7] & 0x0c) >> 2);
		return pkt_send_nand_cycle(sd, fpga_counter, data, ctrl, unknown);
	}
	else if (pkt_type == PKT_SD_CMD) {
		uint8_t regnum;
		uint8_t val;
                regnum = ((pkt[4] & 0xf0) >> 4) | ((pkt[5] & 0x0f) << 4);
		val = ((pkt[5] & 0xf0) >> 4) | ((pkt[6] & 0x03) << 4);
		return pkt_send_sd_cmd_arg_fpga(sd, fpga_counter, regnum, val);
	}

	else if (pkt_type == PKT_SD_RESPONSE) {
		uint8_t val;
                val = ((pkt[4] & 0xf0) >> 4) | ((pkt[5] & 0x0f) << 4);
		return pkt_send_sd_response_fpga(sd, fpga_counter, val);
	}
	else {
		uint32_t err = MAKE_ERROR(SUBSYS_FPGA, FPGA_ERR_UNKNOWN_PKT, pkt_type);
		char errmsg[512];
		snprintf(errmsg, sizeof(errmsg)-1, "Unrecognized FPGA packet type %d", pkt_type);
		return pkt_send_error(sd, err, errmsg);
	}
}


int fpga_ready_fd(struct sd *sd) {
	char bfr[15];
	/* Dummy read required to get poll() to work */
	read(sd->fpga_ready_fd, bfr, sizeof(bfr));
	return sd->fpga_ready_fd;
}

int fpga_overflow_fd(struct sd *sd) {
	char bfr[15];
	/* Dummy read required to get poll() to work */
	read(sd->fpga_overflow_fd, bfr, sizeof(bfr));
	return sd->fpga_overflow_fd;
}

int fpga_reset_ticks(struct sd *sd) {
	sd->fpga_clock_ticks = 0;
	return 0;
}

int fpga_tick_clock(struct sd *sd) {
	sd->fpga_clock_ticks++;
	return 0;
}

uint32_t fpga_ticks(struct sd *sd) {
	return sd->fpga_clock_ticks;
}
