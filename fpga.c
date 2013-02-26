#define _POSIX_C_SOURCE 20121221L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

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
#define DATA_OVERFLOW_PIN 60

// When we drain the FPGA, store this many packets max
#define MAX_PACKETS 1000000


static int set_ignore_blocks(struct sd *sd, int arg) {
	sd->fpga_ignore_blocks = htonl(arg);
	return i2c_set_buffer(sd, 0x10,
			sizeof(sd->fpga_ignore_blocks),
			&sd->fpga_ignore_blocks);
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

	sd->fpga_overflow_pin = CLOCK_OVERFLOW_PIN;
	gpio_export(sd->fpga_overflow_pin);
	gpio_set_direction(sd->fpga_overflow_pin, GPIO_IN);
	gpio_set_edge(sd->fpga_overflow_pin, GPIO_EDGE_BOTH);
	snprintf(str, sizeof(str)-1, "%s/gpio%d/value", GPIO_PATH, sd->fpga_overflow_pin);
	sd->fpga_overflow_fd = open(str, O_RDONLY | O_NONBLOCK);
	if (sd->fpga_overflow_fd == -1)
		return -1;
	sd->fpga_overflow_pin_value = gpio_get_value(sd->fpga_overflow_pin);


	for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++) {
		gpio_export(data_pins[i]);
		gpio_set_direction(data_pins[i], GPIO_IN);
	}

	gpio_export(GET_NEW_SAMPLE_PIN);
	gpio_set_direction(GET_NEW_SAMPLE_PIN, GPIO_OUT);

	gpio_export(DATA_OVERFLOW_PIN);
	gpio_set_direction(DATA_OVERFLOW_PIN, GPIO_IN);

	for (i=0; i<sizeof(bank_select_pins)/sizeof(*bank_select_pins); i++) {
		gpio_export(bank_select_pins[i]);
		gpio_set_direction(bank_select_pins[i], GPIO_OUT);
		gpio_set_value(bank_select_pins[i], 0);
	}

	pthread_mutex_init(&sd->fpga_overflow_mutex, NULL);
	
	parse_set_hook(sd, "ib", set_ignore_blocks);

	return 0;
}

static int nsleep(int nsecs) {
/*
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = nsecs;
	nanosleep(&ts, NULL);
*/
	return 0;
}

int fpga_get_new_sample(struct sd *st, uint8_t bytes[8]) {
	uint8_t data[sizeof(data_pins)/sizeof(*data_pins)];
	static uint8_t last_data[8];
	int bank;
	int i;
	static int repeat_count;

	/* Load the next sample */
	st->fpga_read = !st->fpga_read;
	gpio_set_value(GET_NEW_SAMPLE_PIN, st->fpga_read);

	for (bank=0; bank<4; bank++) {
		gpio_set_value(bank_select_pins[0], !!(bank&1));
		gpio_set_value(bank_select_pins[1], !!(bank&2));
		nsleep(100000);
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

	if (!memcmp(last_data, bytes, sizeof(last_data))) {
		fprintf(stderr, "Got duplicate packet - %d in a row\n", ++repeat_count);
		return fpga_get_new_sample(st, bytes);
	}
	memcpy(last_data, bytes, sizeof(last_data));
	repeat_count = 0;
	return 0;
}

int fpga_data_avail(struct sd *st) {
	return gpio_get_value(DATA_READY_PIN);
}


static int fpga_send_packet(struct sd *sd, uint8_t *pkt) {
	uint8_t pkt_type;
	uint32_t fpga_counter;
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


int fpga_read_data(struct sd *sd) {
	uint8_t pkt[8];
	if (!fpga_data_avail(sd)) {
		fprintf(stderr, "No data avilable!\n");
		return -1;
	}

	if (gpio_get_value(DATA_OVERFLOW_PIN)) {
		fprintf(stderr, "FPGA FIFO overflowed\n");
		pkt_send_error(sd,
			       MAKE_ERROR(SUBSYS_FPGA, FPGA_ERR_OVERFLOW, 0),
			       "FPGA FIFO overflowed");
	}
	
	/* Obtain the new sample and send it over the wire */
	fpga_get_new_sample(sd, pkt);
	return fpga_send_packet(sd, pkt);
}

int fpga_drain(struct sd *sd) {
	uint8_t pkt_buffer[MAX_PACKETS][8];
	uint16_t wr_data_count;
	int packet_offset = 0;
	int overflow_count = 0;
	int current_packet;

	/* Prevent the FPGA from filling the FIFO */
	i2c_set_byte(sd, 2, 1);
	i2c_get_buffer(sd, 0x1c, 2, &wr_data_count);
	wr_data_count = ntohs(wr_data_count);
	fprintf(stderr, "Should read %d packets\n", wr_data_count);

	for (packet_offset = 0;
	     packet_offset < MAX_PACKETS && fpga_data_avail(sd);
	     packet_offset++) {
		if (gpio_get_value(DATA_OVERFLOW_PIN))
			overflow_count++;
		fpga_get_new_sample(sd, pkt_buffer[packet_offset]);
	}
	fprintf(stderr, "Read %d packets\n", packet_offset);

	for (current_packet=0; current_packet<packet_offset; current_packet++)
		fpga_send_packet(sd, pkt_buffer[current_packet]);

	if (overflow_count) {
		char errmsg[512];
		snprintf(errmsg, sizeof(errmsg)-1,
			"FPGA FIFO overflowed %d times\n", overflow_count);
		printf(errmsg);
		pkt_send_error(sd,
			       MAKE_ERROR(SUBSYS_FPGA,
					FPGA_ERR_OVERFLOW,
					overflow_count),
			       errmsg);
	}

	/* Allow the FPGA to fill the FIFO */
	pkt_send_buffer_drain(sd, PKT_BUFFER_DRAIN_STOP);
	i2c_set_byte(sd, 2, 0);

	return 0;
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
	pthread_mutex_lock(&sd->fpga_overflow_mutex);
	sd->fpga_clock_ticks = 0;
	pthread_mutex_unlock(&sd->fpga_overflow_mutex);
	return 0;
}

int fpga_tick_clock_maybe(struct sd *sd) {
	int new;
	new = gpio_get_value(sd->fpga_overflow_pin);
	if (new != sd->fpga_overflow_pin_value) {
		pthread_mutex_lock(&sd->fpga_overflow_mutex);
		sd->fpga_clock_ticks++;
		pthread_mutex_unlock(&sd->fpga_overflow_mutex);
		sd->fpga_overflow_pin_value = new;
		return 1;
	}
	return 0;
}

uint32_t fpga_ticks(struct sd *sd) {
	uint32_t ticks;
	pthread_mutex_lock(&sd->fpga_overflow_mutex);
	ticks = sd->fpga_clock_ticks;
	pthread_mutex_unlock(&sd->fpga_overflow_mutex);
	return ticks;
}

