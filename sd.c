#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <endian.h>
#include "gpio.h"
#include "sd.h"

struct sd_state {
	/* Pin numbers */
	uint32_t data_in, data_out, clk, cs, power;
	uint32_t blklen;
};


/* Table for CRC-7 (polynomial x^7 + x^3 + 1) */
static const uint8_t crc7_syndrome_table[256] = {
        0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
        0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
        0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
        0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
        0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
        0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
        0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
        0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
        0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
        0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
        0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
        0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
        0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
        0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
        0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
        0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
        0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
        0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
        0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
        0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
        0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
        0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
        0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
        0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
        0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
        0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
        0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
        0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
        0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
        0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
        0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
        0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

static inline uint8_t crc7_byte(uint8_t crc, uint8_t data)
{
        return crc7_syndrome_table[(crc << 1) ^ data];
}

/**
 * crc7 - update the CRC7 for the data buffer
 * @crc:     previous CRC7 value
 * @buffer:  data pointer
 * @len:     number of bytes in the buffer
 * Context: any
 *
 * Returns the updated CRC7 value.
 */
static uint8_t crc7(uint8_t crc, const uint8_t *buffer, size_t len)
{
        while (len--)
                crc = crc7_byte(crc, *buffer++);
        return crc;
}


static int sd_tick(struct sd_state *state) {
	gpio_set_value(state->clk, 1);
	usleep(2);
	gpio_set_value(state->clk, 0);
	usleep(2);
	gpio_set_value(state->clk, 1);
	return 0;
}

static int sd_write_bit(struct sd_state *state, int bit) {
	gpio_set_value(state->data_out, !!bit);
	sd_tick(state);
	return 0;
}

#define MSB_FIRST
static int sd_xfer_byte(struct sd_state *state, uint8_t byte) {
	int bit;
	int out = 0;
	for (bit=0; bit<8; bit++) {
#ifdef MSB_FIRST
		sd_write_bit(state, (byte>>(7-bit))&1);
		out |= gpio_get_value(state->data_in)<<(7-bit);
#else
		sd_write_bit(state, (byte>>bit)&1);
		out |= gpio_get_value(state->data_in)<<(bit);
#endif
	}
	return out;
}

static uint8_t sd_read(struct sd_state *state) {
	return sd_xfer_byte(state, 0xff);
}

/* Read the first byte after a CMD (i.e. with a result not of 0xff) */
static uint8_t sd_read_first(struct sd_state *state) {
	uint8_t byte, tries;
	for (byte=0xff, tries=0; byte&0x80 && tries<10; tries++)
		byte = sd_xfer_byte(state, 0xff);
	return byte;
}

/* Wait for the "Start-of-Data" token (0xfe) */
static uint8_t sd_read_first_token(struct sd_state *state) {
	uint8_t byte, tries;
	for (byte=0xff, tries=0; byte!=0xfe && tries<10; tries++)
		byte = sd_xfer_byte(state, 0xff);
	return byte;
}


static int sd_read_array(struct sd_state *state, uint8_t *bytes, size_t sz) {
	int byte;
	uint8_t response;
	uint8_t crc[2];
	response = sd_read_first(state);
	if (response)
		return response;

	response = sd_read_first_token(state);
	if (response != 0xfe)
		return response;

	for (byte=0; byte<sz; byte++)
		bytes[byte] = sd_read(state);

	crc[0] = sd_read(state);
	crc[1] = sd_read(state);
	
	return 0;
}

static int sd_send_cmd(struct sd_state *state, uint8_t cmd, uint8_t args[4]) {
	uint8_t bytes[7];
	uint8_t byte;
	bytes[0] = 0xff;
	bytes[1] = 0x40 | (cmd&0x3f);
	memcpy(&bytes[2], args, sizeof(args));
	bytes[6] = (crc7(0, bytes+1, 5)<<1)|1;

	printf("Sending CMD%d {0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x}\n",
		bytes[1]&0x3f,
		bytes[0], bytes[1], bytes[2], bytes[3],
		bytes[4], bytes[5], bytes[6]);


	for (byte=0; byte<sizeof(bytes); byte++)
		sd_xfer_byte(state, bytes[byte]);
	
	return 0;
}

struct sd_state *sd_init(uint8_t data_in, uint8_t data_out,
			 uint8_t clk, uint8_t cs, uint8_t power) {
	struct sd_state *state = malloc(sizeof(struct sd_state));
	if (!state) {
		perror("Couldn't allocate memory for sd_state");
		return NULL;
	}

	state->data_in = data_in;
	state->data_out = data_out;
	state->clk = clk;
	state->cs = cs;
	state->power = power;

	if (gpio_export(state->data_in)) {
		perror("Unable to export DATA IN pin");
		sd_deinit(&state);
		return NULL;
	}
	gpio_set_direction(state->data_in, 0);


	if (gpio_export(state->data_out)) {
		perror("Unable to export DATA OUT pin");
		sd_deinit(&state);
		return NULL;
	}
	gpio_set_direction(state->data_out, 1);
	gpio_set_value(state->data_out, 1);


	if (gpio_export(state->clk)) {
		perror("Unable to export CLK pin");
		sd_deinit(&state);
		return NULL;
	}
	gpio_set_direction(state->clk, 1);
	gpio_set_value(state->clk, 1);


	if (gpio_export(state->cs)) {
		perror("Unable to export CS pin");
		sd_deinit(&state);
		return NULL;
	}
	gpio_set_direction(state->cs, 1);
	gpio_set_value(state->cs, 1);

	/* Power up the card */
	if (gpio_export(state->power)) {
		perror("Unable to export power pin");
		sd_deinit(&state);
		return NULL;
	}
	gpio_set_direction(state->power, 1);
	gpio_set_value(state->power, 1);


	return state;
}


void sd_deinit(struct sd_state **state) {
	gpio_set_value((*state)->cs, 1);
	gpio_set_value((*state)->power, 1);

	gpio_unexport((*state)->data_in);
	gpio_unexport((*state)->data_out);
	gpio_unexport((*state)->clk);
	gpio_unexport((*state)->cs);
	gpio_unexport((*state)->power);
	free(*state);
	*state = NULL;
}



int sd_reset(struct sd_state *state) {
	uint8_t args[4];
	int byte;
	int tries;
	int pulse;

	bzero(args, sizeof(args));

	printf("Beginning SD reset...\n");
	gpio_set_value(state->power, 1);
	usleep(50000);
	gpio_set_value(state->power, 0);
	usleep(50000);

	/* Send 80 clock pulses */
	gpio_set_value(state->cs, 1);
	for (pulse=0; pulse<80; pulse++)
		sd_tick(state);
	gpio_set_value(state->cs, 0);


	sd_send_cmd(state, SD_CMD0, args);
	byte = sd_read_first(state);
	printf("Reset SD card tries with result 0x%02x\n", byte);

	/* Repeatedly send CMD1 until IDLE is cleared */
	for (byte=0xff, tries=0; byte!=0 && tries<10; tries++) {
		sd_send_cmd(state, SD_CMD1, args);
		byte = sd_read_first(state);
		printf("Result of CMD1: %d\n", byte);
		usleep(20000);
	}
	printf("Sent CMD1 after %d tries with result 0x%02x\n", tries, byte);

	state->blklen = 512;

	return byte==0;
}

int sd_get_csd(struct sd_state *state, uint8_t csd[16]) {
	uint8_t args[4];
	bzero(args, sizeof(args));
	bzero(csd, 16);
	sd_send_cmd(state, SD_CMD9, args);
	
	return sd_read_array(state, csd, 16);
}


int sd_get_cid(struct sd_state *state, uint8_t cid[16]) {
	uint8_t args[4];
	bzero(args, sizeof(args));
	bzero(cid, 16);
	sd_send_cmd(state, SD_CMD10, args);
	sd_read_array(state, cid, 16);
	return 0;
}


int sd_get_sr(struct sd_state *state, uint8_t sr[6]) {
	uint8_t args[4];
	bzero(args, sizeof(args));
	bzero(sr, 6);
	sd_send_cmd(state, SD_CMD12, args);
	sd_send_cmd(state, SD_CMD13, args);
	sr[0] = sd_read(state);
	sr[1] = sd_read(state);
	sr[2] = sd_read(state);
	sr[3] = sd_read(state);
	sr[4] = sd_read(state);
	sr[5] = sd_read(state);
	return 0;
	return sd_read_array(state, sr, 6);
}

int sd_set_blocklength(struct sd_state *state, uint32_t blklen) {
	uint8_t args[4];
	uint32_t swapped = htobe32(blklen);
	int ret;
	memcpy(args, &swapped, sizeof(args));
	sd_send_cmd(state, SD_CMD16, args);
	ret = sd_read_first(state);

	if (!ret)
		state->blklen = blklen;
	return ret;
}

int sd_read_block(struct sd_state *state, uint32_t offset,
		  void *block, uint32_t count) {
	uint8_t args[4];
	uint32_t swapped = htobe32(offset);
	memcpy(args, &swapped, sizeof(args));
	sd_send_cmd(state, SD_CMD17, args);
	return sd_read_array(state, block, state->blklen * count);
}



int sd_get_ocr(struct sd_state *state, uint8_t ocr[4]) {
	uint8_t args[4];
	bzero(args, sizeof(args));
	bzero(ocr, sizeof(ocr));

	args[0] = 0x01;
	args[1] = 0xaa;
	sd_send_cmd(state, SD_CMD8, args);
	
	return sd_read_array(state, ocr, 4);
}
