#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include "sd.h"

/* CHUMBY_BEND - 89
 * IR_DETECT - 102
 */

/* Pin connection:
 * SD  | MX233
 * 9   | 0
 * 1   | 1
 * 2   | 2
 * 3   | GND
 * DET | 3
 * 4   | [power switch]
 * 5   | 4
 * 6   | GND
 * 7   | 7
 * 8   | NC (was: 6)
 */

/** Definitions for Falconwing board
#define CS_PIN 1
#define DATA_IN_PIN 7
#define CLK_PIN 4
#define DATA_OUT_PIN 2
#define POWER_PIN 3
*/

/** Definitions for Kovan test jig */
#define CS_PIN 50
#define DATA_IN_PIN 62
#define CLK_PIN 46
#define DATA_OUT_PIN 48
#define POWER_PIN 55

static const unsigned int tran_exp[] = {
        10000,          100000,         1000000,        10000000,
        0,              0,              0,              0
};

static const unsigned char tran_mant[] = {
        0,      10,     12,     13,     15,     20,     25,     30,
        35,     40,     45,     50,     55,     60,     70,     80,
};

static const unsigned int tacc_exp[] = {
        1,      10,     100,    1000,   10000,  100000, 1000000, 10000000,
};

static const unsigned int tacc_mant[] = {
        0,      10,     12,     13,     15,     20,     25,     30,
        35,     40,     45,     50,     55,     60,     70,     80,
};


struct sd_csd {
	uint8_t csd_structure:2;
	uint8_t reserved1:6;

	uint8_t taac:8;
	uint8_t nsac:8;
	uint8_t tran_speed:8;
	uint16_t ccc:12;

	uint8_t read_bl_len:4;

	uint8_t read_blk_partial:1;
	uint8_t write_blk_misalign:1;
	uint8_t read_blk_misalign:1;
	uint8_t dsr_imp:1;
	uint8_t reserved3:2;

	uint16_t c_size:11;
	uint8_t vdd_r_curr_min:3;
	uint8_t vdd_r_curr_max:3;
	uint8_t vdd_w_curr_min:3;
	uint8_t vdd_w_curr_max:3;
	uint8_t c_size_mult:3;
	uint8_t erase_blk_en:1;
	uint8_t sector_size:7;
	uint8_t wp_grp_size:7;
	uint8_t wp_grp_enable:1;
	uint8_t reserved4:2;

	uint8_t r2w_factor:3;
	uint8_t write_bl_len:4;
	uint8_t write_bl_partial:1;
	uint8_t reserved5:5;

	uint8_t file_format_grp:1;
	uint8_t copy:1;
	uint8_t perm_write_protect:1;
	uint8_t tmp_write_protect:1;
	uint8_t file_format:2;
	uint8_t reserved6:2;

	uint8_t crc:7;
	uint8_t one:1;
};

static void print_csd(void *csd_data) {
	struct sd_csd csd_val;
	struct sd_csd *csd = &csd_val;
	uint8_t *data = csd_data;

	//csd->c_size = be16toh(csd->c_size);

        /*
         * We only understand CSD structure v1.1 and v1.2.
         * v1.2 has extra information in bits 15, 11 and 10.
         */
        if (csd->csd_structure != 1 && csd->csd_structure != 2)
		printf("Warning: Unrecognized CSD structure version %d\n", csd->csd_structure);
	else
		printf("    CSD structure version %d\n", csd->csd_structure);

	csd->ccc = be16toh((data[4]<<4) | (data[5]>>4));
	csd->read_bl_len = (data[5]&0xf);
	csd->read_blk_partial = (data[6]&0x80)>>7;
	csd->write_blk_misalign = (data[6]&0x40)>>6;
	csd->read_blk_misalign = (data[6]&0x20)>>5;
	csd->dsr_imp = (data[6]&0x10)>>4;
	csd->c_size = ((data[6] & 0x03) << 10)
		    | ((data[7] & 0xff) << 2)
		    | ((data[8] & 0xc0) >> 6);
	csd->vdd_r_curr_min = ((data[8] >> 3) & 0x7);
	csd->vdd_r_curr_max = ((data[8] & 0x7) >> 0);
	csd->vdd_w_curr_min = ((data[9] >> 5) & 0x7);
	csd->vdd_w_curr_max = ((data[9] >> 2) & 0x7);
	csd->c_size_mult = ((data[9] & 0x3) << 1) | ((data[10] >> 7) & 1);
	csd->erase_blk_en = ((data[10] >> 6) & 1);
	csd->sector_size = ((data[10] & 0x3f) << 1) | ((data[11] >> 7) & 1);
	csd->wp_grp_size = ((data[11] & 0x3f));
	csd->r2w_factor = ((data[12] >> 2) & 0x7);
	csd->write_bl_len = ((data[12] & 0x3) << 2) | ((data[13] >> 5) & 0x3);
	printf("    c_size: %x\n", csd->c_size);
	printf("    c_size_mult: %d\n", csd->c_size_mult);
	printf("    write_bl_len: %d\n", 1 << csd->write_bl_len);
	printf("    read_bl_len: %d\n", 1 << csd->read_bl_len);
	uint32_t blocknr, block_len, mult;
	block_len = 1 << csd->read_bl_len;
	mult = 1<< (csd->c_size_mult+2);
	blocknr = (csd->c_size+1)*mult;
	printf("    block_len: %d\n", block_len);
	printf("    mult: %d\n", mult);
	printf("    blocknr: %d\n", blocknr);
	printf("    capacity: %d\n", blocknr * block_len);

/*
        printf("    MMCA version: %d\n", UNSTUFF_BITS(resp, 122, 4));
        m = UNSTUFF_BITS(resp, 115, 4);
        e = UNSTUFF_BITS(resp, 112, 3);
        printf("    tacc_ns: %d\n", (tacc_exp[e] * tacc_mant[m] + 9) / 10);
        printf("    tacc_clks: %d\n", UNSTUFF_BITS(resp, 104, 8) * 100);

        m = UNSTUFF_BITS(resp, 99, 4);
        e = UNSTUFF_BITS(resp, 96, 3);
        printf("    max_dtr: %d\n", tran_exp[e] * tran_mant[m]);
        printf("    cmdclass: %d\n", UNSTUFF_BITS(resp, 84, 12));

        e = UNSTUFF_BITS(resp, 47, 3);
        m = UNSTUFF_BITS(resp, 62, 12);
        printf("    capacity: %d\n", (1 + m) << (e + 2));

        printf("    read_blkbits: %d\n", UNSTUFF_BITS(resp, 80, 4));
        printf("    read_partial: %d\n", UNSTUFF_BITS(resp, 79, 1));
        printf("    write_misalign: %d\n", UNSTUFF_BITS(resp, 78, 1));
        printf("    read_misalign: %d\n", UNSTUFF_BITS(resp, 77, 1));
        printf("    r2w_factor: %d\n", UNSTUFF_BITS(resp, 26, 3));
        printf("    write_blkbits: %d\n", UNSTUFF_BITS(resp, 22, 4));
	printf("    Write partial: %d\n", UNSTUFF_BITS(resp, 21, 1));
*/
}

struct sd_cid {
	uint32_t mid;
	uint16_t oid;
	uint8_t name[8];
	uint8_t hwrev;
	uint8_t fwrev;
	uint32_t serial;
	uint16_t year;
	uint8_t month;
	uint8_t chksum;
};

static void print_cid(void *cid) {
	uint8_t *data = cid;
	struct sd_cid sd_cid;
	bzero(&sd_cid, sizeof(sd_cid));

	sd_cid.mid	= data[0];
	sd_cid.oid 	= (data[1] << 8) | (data[2]);
	sd_cid.name[0]	= data[3];
	sd_cid.name[1]	= data[4];
	sd_cid.name[2]	= data[5];
	sd_cid.name[3]	= data[6];
	sd_cid.name[4]	= data[7];
	sd_cid.name[5]	= '\0';
	sd_cid.hwrev	= (data[8]>>4)&0xf;
	sd_cid.fwrev	= data[8]&0xf;
	sd_cid.serial	= ((data[9] << 24) | (data[10] << 16)
			|  (data[11] << 8) | (data[12] << 0));
	sd_cid.month	= data[14] & 0x0f;
	sd_cid.year	= ((data[14] & 0xf0)>>4) | ((data[13] & 0x01) << 4);

	sd_cid.year  += 2000; /* SD cards year offset */

	printf("    manfid: 0x%02x\n", sd_cid.mid);
	printf("    oemid: 0x%04x\n", sd_cid.oid);
	printf("    prod_name[0]: %c\n", sd_cid.name[0]);
	printf("    prod_name[1]: %c\n", sd_cid.name[1]);
	printf("    prod_name[2]: %c\n", sd_cid.name[2]);
	printf("    prod_name[3]: %c\n", sd_cid.name[3]);
	printf("    prod_name[4]: %c\n", sd_cid.name[4]);
	printf("    prod_name: %s\n", sd_cid.name);
	printf("    hwrev: %x\n", sd_cid.hwrev);
	printf("    fwrev: %x\n", sd_cid.fwrev);
	printf("    serial: 0x%08x\n", sd_cid.serial);
	printf("    month: %d\n", sd_cid.month);
	printf("    year: %d\n", sd_cid.year);
}


static void print_sr(void *sr) {
	char *data = sr;
	uint32_t status;
	memcpy(&status, &data[1], sizeof(status));
	printf("    Command index: %x\n", data[5]&0x1f);
	printf("    Status: %x\n", status);
}


int main(int argc, char **argv) {
	struct sd_state *state;
	int i;
	int ret;
	uint8_t ocr[4];
	uint8_t csd[16];
	uint8_t cid[16];
	uint8_t sr[6];
	uint8_t block[512];
	
	state = sd_init(DATA_IN_PIN, DATA_OUT_PIN, CLK_PIN, CS_PIN, POWER_PIN);
	if (!state)
		return 1;

	/* Initialize the card, waiting for a response of "1" */
	sd_reset(state);

	/* Read the OCR register */
	sd_get_ocr(state, ocr);
	printf("OCR:\n");
	for (i=0; i<sizeof(ocr); i++)
		printf("    0x%02x %c\n", ocr[i], isprint(ocr[i])?ocr[i]:'.');
	//printf("Setting block length: 0x%02x\n", sd_set_blocklength(state, 512));

	if ((ret=sd_get_csd(state, csd))) {
		printf("Unable to get CSD\n");
	}
	else {
		print_csd(csd);
		printf("CSD:\n");
		for (i=0; i<sizeof(csd); i++)
			printf("    0x%02x %c\n", csd[i], isprint(csd[i])?csd[i]:'.');
	}
	
	if ((ret=sd_get_cid(state, cid))) {
		printf("Unable to get CID\n");
	}
	else {
		printf("CID:\n");
		print_cid(cid);
	}


	if ((ret=sd_get_sr(state, sr))) {
		printf("Unable to get SR: %d\n", ret);
	}
	else {
		printf("SR:\n");
		print_sr(sr);
	}


	if ((ret = sd_set_blocklength(state, 512)))
		printf("Unable to set block length: %d\n", ret);



	if ((ret = sd_read_block(state, 0, block, 1)))
		printf("Unable to read data: %d\n", ret);
	else {
		uint8_t *offset = block;
		for (i=0; i<sizeof(block); i++) {
			printf("%02x ", block[i]);
			if (!((i+1)&0x7))
				printf(" ");
			if (!((i+1)&0xf)) {
				int j;
				printf("|");
				for (j=0; j<16; j++)
					printf("%c", isprint(offset[j])?offset[j]:'.');
				printf("|\n");
				offset += 16;
			}
		}
	}


	sd_deinit(&state);

	return 0;
}
