/*------------------------------------------------------------------------/
/  Bitbanging MMCv3/SDv1/SDv2 (in SPI mode) control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2012, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/--------------------------------------------------------------------------/
 Features and Limitations:

 * Very Easy to Port
   It uses only 4 bit of GPIO port. No interrupt, no SPI port is used.

 * Platform Independent
   You need to modify only a few macros to control GPIO ports.

 * Low Speed
   The data transfer rate will be several times slower than hardware SPI.

/-------------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 20121221L
#define _XOPEN_SOURCE 700
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include "gpio.h"
#include "sd.h"

enum disk_status {
	STA_NO_INIT,
	STA_NOINIT,
};

enum their_results {
	RES_OK,
	RES_PARERR,
	RES_NOTRDY,
	RES_ERROR,
};

enum disk_ioctl_arg {
	CTRL_SYNC,
	GET_SECTOR_COUNT,
	GET_BLOCK_SIZE,
};



/*-------------------------------------------------------------------------*/
/* Platform dependent macros and functions needed to be modified           */
/*-------------------------------------------------------------------------*/



#define	INIT_PORT(state)	init_port(state)	/* Initialize MMC control port (CS=H, CLK=L, DI=H, DO=in) */
#define DLY_US(n)	my_usleep(n)	/* Delay n microseconds */

#define	CS_H()		gpio_set_value(state->sd_cs, CS_DESEL) /* Set MMC CS "high" */
#define	CS_L()		gpio_set_value(state->sd_cs, CS_SEL) /* Set MMC CS "low" */
#define	CK_H()		gpio_set_value(state->sd_clk, 1) /* Set MMC CLK "high" */
#define	CK_L()		gpio_set_value(state->sd_clk, 0) /* Set MMC CLK "low" */
#define	DI_H()		gpio_set_value(state->sd_mosi, 1) /* Set MMC DI "high" */
#define	DI_L()		gpio_set_value(state->sd_mosi, 0) /* Set MMC DI "low" */
#define DO		gpio_get_value(state->sd_miso)	/* Test for MMC DO ('H':true, 'L':false) */


/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command (SPI mode) */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define CMD13	(13)		/* SEND_STATUS */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD41	(41)		/* SEND_OP_COND (ACMD) */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */

/* Card type flags (CardType) */
#define CT_MMC		0x01		/* MMC ver 3 */
#define CT_SD1		0x02		/* SD ver 1 */
#define CT_SD2		0x04		/* SD ver 2 */
#define CT_SDC		0x06		/* SD */
#define CT_BLOCK	0x08		/* Block addressing */


static uint32_t Stat = STA_NO_INIT;	/* Disk status */

static uint8_t CardType;			/* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */


static int my_usleep(long long usecs) {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = usecs*1000;
	return nanosleep(&ts, NULL);
}

static int init_port(struct sd *state) {
        gpio_set_value(state->sd_power, SD_OFF);
	CS_H();
	my_usleep(300000);
        gpio_set_value(state->sd_power, SD_ON);
	my_usleep(10000);
	return 0;
}



/*-----------------------------------------------------------------------*/
/* Transmit bytes to the card (bitbanging)                               */
/*-----------------------------------------------------------------------*/

static
void xmit_mmc (
	struct sd *state,
	const uint8_t* buff,	/* Data to be sent */
	uint32_t bc				/* Number of bytes to send */
)
{
	uint8_t d;
	int count = 0;


	do {
		d = *buff++;	/* Get a byte to be sent */
		if (d & 0x80) DI_H(); else DI_L();	/* bit7 */
		CK_H(); CK_L();
		if (d & 0x40) DI_H(); else DI_L();	/* bit6 */
		CK_H(); CK_L();
		if (d & 0x20) DI_H(); else DI_L();	/* bit5 */
		CK_H(); CK_L();
		if (d & 0x10) DI_H(); else DI_L();	/* bit4 */
		CK_H(); CK_L();
		if (d & 0x08) DI_H(); else DI_L();	/* bit3 */
		CK_H(); CK_L();
		if (d & 0x04) DI_H(); else DI_L();	/* bit2 */
		CK_H(); CK_L();
		if (d & 0x02) DI_H(); else DI_L();	/* bit1 */
		CK_H(); CK_L();
		if (d & 0x01) DI_H(); else DI_L();	/* bit0 */
		CK_H(); CK_L();
		pkt_send_sd_cmd_arg(state, count++, d);
	} while (--bc);
}



/*-----------------------------------------------------------------------*/
/* Receive bytes from the card (bitbanging)                              */
/*-----------------------------------------------------------------------*/

static
void rcvr_mmc (
	struct sd *state,
	uint8_t *buff,	/* Pointer to read buffer */
	uint32_t bc		/* Number of bytes to receive */
)
{
	uint8_t r;


	DI_H();	/* Send 0xFF */

	do {
		r = 0;	 if (DO) r++;	/* bit7 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit6 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit5 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit4 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit3 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit2 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit1 */
		CK_H(); CK_L();
		r <<= 1; if (DO) r++;	/* bit0 */
		CK_H(); CK_L();
		*buff++ = r;			/* Store a received byte */
	} while (--bc);
}



/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (		/* 1:OK, 0:Timeout */
	struct sd *state
)
{
	uint8_t d;
	uint32_t tmr;


	for (tmr = 5000; tmr; tmr--) {	/* Wait for ready in timeout of 500ms */
		rcvr_mmc(state, &d, 1);
		if (d == 0xFF) break;
		DLY_US(100);
	}

	return tmr ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/

static
void sd_end (
	struct sd *state
)
{
	uint8_t d;

	CS_H();
	rcvr_mmc(state, &d, 1);	/* Dummy clock (force DO hi-z for multiple slave SPI) */
}



/*-----------------------------------------------------------------------*/
/* Select the card and wait for ready                                    */
/*-----------------------------------------------------------------------*/

static
int sd_begin (	/* 1:OK, 0:Timeout */
	struct sd *state
)
{
	uint8_t d;

	CS_L();
	rcvr_mmc(state, &d, 1);	/* Dummy clock (force DO enabled) */

	if (wait_ready(state)) return 1;	/* OK */
	fprintf(stderr, "Card never came ready\n");
	sd_end(state);
	return 0;			/* Failed */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the card                                   */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock (	/* 1:OK, 0:Failed */
	struct sd *state,
	uint8_t *buff,			/* Data buffer to store received data */
	uint32_t btr			/* Byte count */
)
{
	uint8_t d[2];
	uint32_t tmr;


	for (tmr = 1000; tmr; tmr--) {	/* Wait for data packet in timeout of 100ms */
		rcvr_mmc(state, d, 1);
		if (d[0] != 0xFF) break;
		DLY_US(100);
	}
	if (d[0] != 0xFE) return 0;		/* If not valid data token, return with error */

	rcvr_mmc(state, buff, btr);			/* Receive the data block into buffer */
	rcvr_mmc(state, d, 2);					/* Discard CRC */

	return 1;						/* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to the card                                        */
/*-----------------------------------------------------------------------*/

static
int xmit_datablock (	/* 1:OK, 0:Failed */
	struct sd *state,
	const uint8_t *buff,	/* 512 byte data block to be transmitted */
	uint8_t token			/* Data/Stop token */
)
{
	uint8_t d[2];


	if (!wait_ready(state)) return 0;

	d[0] = token;
	xmit_mmc(state, d, 1);				/* Xmit a token */
	if (token != 0xFD) {		/* Is it data token? */
		xmit_mmc(state, buff, 512);	/* Xmit the 512 byte data block to MMC */
		rcvr_mmc(state, d, 2);			/* Xmit dummy CRC (0xFF,0xFF) */
		rcvr_mmc(state, d, 1);			/* Receive data response */
		if ((d[0] & 0x1F) != 0x05)	/* If not accepted, return with error */
			return 0;
	}

	return 1;
}



/*-----------------------------------------------------------------------*/
/* Send a command packet to the card                                     */
/*-----------------------------------------------------------------------*/

static
uint8_t send_cmd (		/* Returns command response (bit7==1:Send failed)*/
	struct sd *state,
	uint8_t cmd,		/* Command byte */
	int32_t arg		/* Argument */
)
{
	uint8_t n, d, buf[6];

	if (cmd & 0x80) {	/* ACMD<n> is the command sequense of CMD55-CMD<n> */
		cmd &= 0x7F;
		n = send_cmd(state, CMD55, 0);
		if (n > 1) return n;
	}

	/* Select the card and wait for ready */
	sd_end(state);
	if (!sd_begin(state)) return 0xFF;

	/* Send a command packet */
	buf[0] = 0x40 | cmd;			/* Start + Command index */
	buf[1] = (uint8_t)(arg >> 24);		/* Argument[31..24] */
	buf[2] = (uint8_t)(arg >> 16);		/* Argument[23..16] */
	buf[3] = (uint8_t)(arg >> 8);		/* Argument[15..8] */
	buf[4] = (uint8_t)arg;				/* Argument[7..0] */
	n = 0x01;						/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;		/* (valid CRC for CMD0(0)) */
	if (cmd == CMD8) n = 0x87;		/* (valid CRC for CMD8(0x1AA)) */
	buf[5] = n;

	xmit_mmc(state, buf, 6);

	/* Receive command response */
	if (cmd == CMD12) rcvr_mmc(state, &d, 1);	/* Skip a stuff byte when stop reading */
	n = 10;								/* Wait for a valid response in timeout of 10 attempts */
	do
		rcvr_mmc(state, &d, 1);
	while ((d & 0x80) && --n);

	pkt_send_sd_response(state, d);
	return d;			/* Return with the response value */
}



/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

int disk_status (
	struct sd *state
)
{
	int s;
	uint8_t d;


	/* Check if the card is kept initialized */
	s = Stat;
	if (!(s & STA_NOINIT)) {
		if (send_cmd(state, CMD13, 0))	/* Read card status */ {
			s = STA_NOINIT;
			fprintf(stderr, "Card status returned STA_NOINIT\n");
		}
		rcvr_mmc(state, &d, 1);		/* Receive following half of R2 */
		sd_end(state);
	}
	Stat = s;

	return s;
}



/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

static int sd_net_power_on(struct sd *state, int arg) {
	gpio_set_value(state->sd_power, SD_ON);
	return 0;
}

static int sd_net_power_off(struct sd *state, int arg) {
	gpio_set_value(state->sd_power, SD_OFF);
	return 0;
}

static int sd_net_do_reset(struct sd *state, int arg) {
	return sd_reset(state);
}

static int sd_net_read_current_sector(struct sd *state, int arg) {
	int ret;
	ret = sd_read_block(state, state->sd_sector, state->sd_read_bfr, 1);
	if (ret) {
		fprintf(stderr, "Couldn't read: %d\n", ret);
		return ret;
	}
	pkt_send_sd_data(state, state->sd_read_bfr);
	return ret;
}

static int sd_net_write_current_sector(struct sd *state, int arg) {
	int ret;
	ret = sd_write_block(state, state->sd_sector, state->sd_write_bfr, 1);
	return ret;
}

static int sd_net_set_current_sector(struct sd *state, int arg) {
	state->sd_sector = arg;
	return 0;
}

static int sd_net_get_current_sector(struct sd *state, int arg) {
	pkt_send_buffer_offset(state, SECTOR_OFFSET, state->sd_sector);
	return 0;
}

static int sd_net_get_cid(struct sd *state, int arg) {
	int ret;
	uint8_t cid[16];
	ret = sd_get_cid(state, cid);
	if (ret) {
		pkt_send_error(state, MAKE_ERROR(SUBSYS_SD, SD_ERR_CID, ret),
				"Unable to request card CID");
		return -1;
	}

	pkt_send_sd_cid(state, cid);
	return 0;
}

static int sd_net_get_csd(struct sd *state, int arg) {
	int ret;
	uint8_t csd[16];
	ret = sd_get_csd(state, csd);
	if (ret) {
		pkt_send_error(state, MAKE_ERROR(SUBSYS_SD, SD_ERR_CSD, ret),
				"Unable to request card CSD");
		return -1;
	}

	pkt_send_sd_csd(state, csd);
	return 0;
}


static int sd_net_reset_buffer(struct sd *state, int arg) {
	state->sd_write_buffer_offset = 0;
	return 0;
}

static int sd_net_set_buffer_offset(struct sd *state, int arg) {
	state->sd_write_buffer_offset = arg;
	return 0;
}

static int sd_net_get_buffer_offset(struct sd *state, int arg) {
	pkt_send_buffer_offset(state, BUFFER_WRITE, state->sd_write_buffer_offset);
	return 0;
}

static int sd_net_set_buffer_value(struct sd *state, int arg) {
	state->sd_write_bfr[state->sd_write_buffer_offset++] = arg;
	return 0;
}

static int sd_net_get_buffer_contents(struct sd *state, int arg) {
	pkt_send_buffer_contents(state, 2, state->sd_write_bfr);
	return 0;
}

static int sd_net_copy_read_to_write_buffer(struct sd *state, int arg) {
	memcpy(state->sd_write_bfr,
	       state->sd_read_bfr,
	       sizeof(state->sd_write_bfr));
	return 0;
}

static int sd_net_pattern_select(struct sd *state, int arg) {
	int i, max;
	int sz = sizeof(state->sd_write_bfr);
	uint8_t *sd_write_bfr_8 = (uint8_t *)state->sd_write_bfr;
	uint16_t *sd_write_bfr_16 = (uint16_t *)state->sd_write_bfr;
	uint32_t *sd_write_bfr_32 = (uint32_t *)state->sd_write_bfr;

	/* All zeroes */
	if (arg == 0) {
		memset(sd_write_bfr_8, 0, sz);
	}

	/* All ones */
	else if (arg == 1) {
		memset(sd_write_bfr_8, 0xff, sz);
	}


	/* Walking zeroes */
	else if (arg == 2) {
		max = 8;
		for (i=0; i<sz/1; i++)
			sd_write_bfr_8[i] = ~(1<<(i&(max-1)));
	}
	else if (arg == 3) {
		max = 16;
		for (i=0; i<sz/2; i++)
			sd_write_bfr_16[i] = ~(1<<(i&(max-1)));
	}
	else if (arg == 4) {
		max = 32;
		for (i=0; i<sz/4; i++)
			sd_write_bfr_32[i] = ~(1<<(i&(max-1)));
	}


	/* Walking ones */
	else if (arg == 5) {
		max = 8;
		for (i=0; i<sz/1; i++)
			sd_write_bfr_8[i] = (1<<(i&(max-1)));
	}
	else if (arg == 6) {
		max = 16;
		for (i=0; i<sz/2; i++)
			sd_write_bfr_16[i] = (1<<(i&(max-1)));
	}
	else if (arg == 7) {
		max = 32;
		for (i=0; i<sz/4; i++)
			sd_write_bfr_32[i] = (1<<(i&(max-1)));
	}

	return 0;
}

int sd_get_elapsed(struct sd *state, time_t *tv_sec, long *tv_nsec) {
	struct timespec now;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &now);
	if (ret == -1) {
		perror("Couldn't get time");
		return ret;
	}

	if (now.tv_nsec < state->fpga_starttime.tv_nsec)
		now.tv_nsec += 1000000000;
	now.tv_nsec -= state->fpga_starttime.tv_nsec;
	now.tv_sec -= state->fpga_starttime.tv_sec;

	*tv_sec = now.tv_sec;
	*tv_nsec = now.tv_nsec;
	return 0;
}


static int install_hooks(struct sd *state) {
	parse_set_hook(state, "p+", sd_net_power_on);
	parse_set_hook(state, "p-", sd_net_power_off);
	parse_set_hook(state, "rc", sd_net_do_reset);
	parse_set_hook(state, "ci", sd_net_get_cid);
	parse_set_hook(state, "cs", sd_net_get_csd);
	parse_set_hook(state, "rs", sd_net_read_current_sector);
	parse_set_hook(state, "ws", sd_net_write_current_sector);
	parse_set_hook(state, "so", sd_net_set_current_sector);
	parse_set_hook(state, "go", sd_net_get_current_sector);

	parse_set_hook(state, "rb", sd_net_reset_buffer);
	parse_set_hook(state, "sb", sd_net_set_buffer_value);
	parse_set_hook(state, "bp", sd_net_get_buffer_offset);
	parse_set_hook(state, "bo", sd_net_set_buffer_offset);
	parse_set_hook(state, "bc", sd_net_get_buffer_contents);
	parse_set_hook(state, "cb", sd_net_copy_read_to_write_buffer);

	parse_set_hook(state, "ps", sd_net_pattern_select);
	return 0;
}

int sd_init(struct sd *state, uint8_t miso, uint8_t mosi,
	    uint8_t clk, uint8_t cs, uint8_t power, uint8_t fpga_reset) {

	state->sd_miso = miso;
	state->sd_mosi = mosi;
	state->sd_clk = clk;
	state->sd_cs = cs;
	state->sd_power = power;
	state->fpga_reset_clock = fpga_reset;

	if (gpio_export(state->sd_miso)) {
		perror("Unable to export DATA IN pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->sd_miso, GPIO_IN);


	if (gpio_export(state->sd_mosi)) {
		perror("Unable to export DATA OUT pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->sd_mosi, GPIO_OUT);
	gpio_set_value(state->sd_mosi, 1);


	if (gpio_export(state->sd_clk)) {
		perror("Unable to export CLK pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->sd_clk, GPIO_OUT);
	gpio_set_value(state->sd_clk, 1);


	/* Grab the chip select pin and deassert it */
	if (gpio_export(state->sd_cs)) {
		perror("Unable to export CS pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->sd_cs, GPIO_OUT);
	gpio_set_value(state->sd_cs, CS_DESEL);

	/* Power down the card */
	if (gpio_export(state->sd_power)) {
		perror("Unable to export power pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->sd_power, GPIO_OUT);
	gpio_set_value(state->sd_power, SD_OFF);

	/* Request the pin to reset the FPGA's clock */
	if (gpio_export(state->fpga_reset_clock)) {
		perror("Unable to export clock reset pin");
		sd_deinit(&state);
		return -1;
	}
	gpio_set_direction(state->fpga_reset_clock, GPIO_OUT);
	gpio_set_value(state->fpga_reset_clock, 1);

	install_hooks(state);

	return 0;
}


int sd_reset(struct sd *state) {
	uint8_t n, ty, cmd, buf[4];
	uint32_t tmr;
	int s;

	gpio_set_value(state->fpga_reset_clock, 1);
	state->sd_sector = 0;
	INIT_PORT(state);				/* Initialize control port */
	gpio_set_value(state->fpga_reset_clock, 0);
	if (state->fpga_ignore_blocks)
		i2c_set_buffer(state, 0x10,
                        sizeof(state->fpga_ignore_blocks), 
                        &state->fpga_ignore_blocks);

	fpga_reset_ticks(state);
	clock_gettime(CLOCK_MONOTONIC, &state->fpga_starttime);
	for (n = 10; n; n--) rcvr_mmc(state, buf, 1);	/* 80 dummy clocks */

	ty = 0;
	if (send_cmd(state, CMD0, 0) == 1) {			/* Enter Idle state */
		if (send_cmd(state, CMD8, 0x1AA) == 1) {	/* SDv2? */
			rcvr_mmc(state, buf, 4);							/* Get trailing return value of R7 resp */
			if (buf[2] == 0x01 && buf[3] == 0xAA) {		/* The card can work at vdd range of 2.7-3.6V */
				for (tmr = 1000; tmr; tmr--) {			/* Wait for leaving idle state (ACMD41 with HCS bit) */
					if (send_cmd(state, ACMD41, 1UL << 30) == 0) break;
					DLY_US(1000);
				}
				if (tmr && send_cmd(state, CMD58, 0) == 0) {	/* Check CCS bit in the OCR */
					rcvr_mmc(state, buf, 4);
					ty = (buf[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* SDv2 */
				}
			}
		} else {							/* SDv1 or MMCv3 */
			if (send_cmd(state, ACMD41, 0) <= 1) 	{
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 */
			}
			for (tmr = 1000; tmr; tmr--) {			/* Wait for leaving idle state */
				if (send_cmd(state, cmd, 0) == 0) break;
				DLY_US(1000);
			}
			if (!tmr || send_cmd(state, CMD16, 512) != 0)	/* Set R/W block length to 512 */
				ty = 0;
		}
	}
	CardType = ty;
	s = ty ? 0 : STA_NOINIT;
	if (s == STA_NOINIT)
		fprintf(stderr, "Type of %d, not initted\n", ty);
	Stat = s;

	sd_end(state);

	pkt_send_reset(state);
	return s;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

int sd_read_block (
	struct sd *state,	/* Physical drive nmuber (0) */
	uint32_t sector,	/* Start sector number (LBA) */
	uint8_t *buff,		/* Pointer to the data buffer to store read data */
	uint32_t count		/* Sector count (1..128) */
)
{
	memset(buff, 0, count*512);
	if (disk_status(state) & STA_NOINIT) return RES_NOTRDY;
	if (!count) return RES_PARERR;
	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert LBA to byte address if needed */

	if (count == 1) {	/* Single block read */
		if ((send_cmd(state, CMD17, sector) == 0)	/* READ_SINGLE_BLOCK */
			&& rcvr_datablock(state, buff, 512))
			count = 0;
	}
	else {				/* Multiple block read */
		if (send_cmd(state, CMD18, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (!rcvr_datablock(state, buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(state, CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	sd_end(state);

	return count ? RES_ERROR : RES_OK;
}

void sd_deinit(struct sd **state) {
	gpio_set_value((*state)->sd_cs, CS_DESEL);
	gpio_set_value((*state)->sd_power, SD_OFF);

	gpio_unexport((*state)->sd_miso);
	gpio_unexport((*state)->sd_mosi);
	gpio_unexport((*state)->sd_clk);
	gpio_unexport((*state)->sd_cs);
	gpio_unexport((*state)->sd_power);
	gpio_unexport((*state)->fpga_reset_clock);
	free(*state);
	*state = NULL;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

int sd_write_block (
	struct sd *state,
	uint32_t sector,		/* Start sector number (LBA) */
	const uint8_t *buff,	/* Pointer to the data to be written */
	uint32_t count			/* Sector count (1..128) */
)
{
	if (disk_status(state) & STA_NOINIT) return RES_NOTRDY;
	if (!count) return RES_PARERR;
	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert LBA to byte address if needed */

	if (count == 1) {	/* Single block write */
		if ((send_cmd(state, CMD24, sector) == 0)	/* WRITE_BLOCK */
			&& xmit_datablock(state, buff, 0xFE))
			count = 0;
	}
	else {				/* Multiple block write */
		if (CardType & CT_SDC) send_cmd(state, ACMD23, count);
		if (send_cmd(state, CMD25, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!xmit_datablock(state, buff, 0xFC)) break;
				buff += 512;
			} while (--count);
			if (!xmit_datablock(state, 0, 0xFD))	/* STOP_TRAN token */
				count = 1;
		}
	}
	sd_end(state);

	return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

int sd_get_csd(struct sd *state, uint8_t *csd) {
	int ret;
	memset(csd, 0, 16);
	ret = send_cmd(state, CMD9, 0);
	if (ret)
		return ret;
	return !rcvr_datablock(state, csd, 16);
}

int sd_get_cid(struct sd *state, uint8_t *cid) {
	int ret;
	memset(cid, 0, 16);
	ret = send_cmd(state, CMD10, 0);
	if (ret)
		return ret;
	return !rcvr_datablock(state, cid, 16);
}


int disk_ioctl (
	struct sd *state,
	uint8_t ctrl,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	int res;
	uint8_t n, csd[16];
	int32_t cs;


	if (disk_status(state) & STA_NOINIT) return RES_NOTRDY;	/* Check if card is in the socket */

	res = RES_ERROR;
	switch (ctrl) {
		case CTRL_SYNC :		/* Make sure that no pending write process */
			if (sd_begin(state)) {
				sd_end(state);
				res = RES_OK;
			}
			break;

		case GET_SECTOR_COUNT :	/* Get number of sectors on the disk
(int32_t) */
			if ((send_cmd(state, CMD9, 0) == 0) && rcvr_datablock(state, csd, 16)) {
				if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
					cs = csd[9] + ((uint16_t)csd[8] << 8) +
((int32_t)(csd[7] & 63) << 8) + 1;
					*(int32_t*)buff = cs << 10;
				} else {					/* SDC ver 1.XX or MMC */
					n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
					cs = (csd[8] >> 6) +
((uint16_t)csd[7] << 2) + ((uint16_t)(csd[6] & 3) << 10) + 1;
					*(int32_t*)buff = cs << (n - 9);
				}
				res = RES_OK;
			}
			break;

		case GET_BLOCK_SIZE :	/* Get erase block size in unit of
sector (int32_t) */
			*(int32_t*)buff = 128;
			res = RES_OK;
			break;

		default:
			res = RES_PARERR;
	}

	sd_end(state);

	return res;
}



/*-----------------------------------------------------------------------*/
/* This function is defined for only project compatibility               */

void disk_timerproc (void)
{
	/* Nothing to do */
}

