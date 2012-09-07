#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <ctype.h>
#include "sd.h"

/* CHUMBY_BEND - 89
 * IR_DETECT - 102
 */

#define CS_PIN 89
#define DATA_IN_PIN 45
#define CLK_PIN 46
#define DATA_OUT_PIN 102

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


#define UNSTUFF_BITS(resp,start,size)                                   \
        ({                                                              \
                const int __size = size;                                \
                const uint32_t __mask = (__size < 32 ? 1 << __size : 0) - 1; \
                const int __off = 3 - ((start) / 32);                   \
                const int __shft = (start) & 31;                        \
                uint32_t __res;                                              \
                                                                        \
                __res = resp[__off] >> __shft;                          \
                if (__size + __shft > 32)                               \
                        __res |= resp[__off-1] << ((32 - __shft) % 32); \
                __res & __mask;                                         \
        })

static void print_csd(uint8_t csd[16]) {
        unsigned int e, m, csd_struct;
        uint32_t *resp = (uint32_t *)csd;

        /*
         * We only understand CSD structure v1.1 and v1.2.
         * v1.2 has extra information in bits 15, 11 and 10.
         */
        csd_struct = UNSTUFF_BITS(resp, 126, 2);
        if (csd_struct != 1 && csd_struct != 2)
		printf("Warning: Unrecognized CSD structure version %d\n", csd_struct);

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
}

static void print_cid(uint8_t cid[16]) {
	uint32_t *resp = (uint32_t *)cid;
	printf("    manfid: 0x%02x\n", UNSTUFF_BITS(resp, 120, 8));
	printf("    oemid: 0x%04x\n", be16toh(UNSTUFF_BITS(resp, 104, 16)));
	printf("    prod_name[0]: %c\n", UNSTUFF_BITS(resp, 96, 8));
	printf("    prod_name[1]: %c\n", UNSTUFF_BITS(resp, 88, 8));
	printf("    prod_name[2]: %c\n", UNSTUFF_BITS(resp, 80, 8));
	printf("    prod_name[3]: %c\n", UNSTUFF_BITS(resp, 72, 8));
	printf("    prod_name[4]: %c\n", UNSTUFF_BITS(resp, 64, 8));
	printf("    prod_name[5]: %c\n", UNSTUFF_BITS(resp, 56, 8));
	printf("    serial: 0x%08x\n", UNSTUFF_BITS(resp, 16, 32));
	printf("    month: %d\n", UNSTUFF_BITS(resp, 12, 4));
	printf("    year: %d\n", UNSTUFF_BITS(resp, 8, 4) + 1997);
}


int main(int argc, char **argv) {
	struct sd_state *state;
	int i;
	uint8_t ocr[4];
	uint8_t cid[16];
	uint8_t csd[16];
	
	state = sd_init(DATA_IN_PIN, DATA_OUT_PIN, CLK_PIN, CS_PIN);
	if (!state)
		return 1;

	/* Initialize the card, waiting for a response of "1" */
	sd_reset(state);

	/* Read the OCR register */
	/*
	sd_get_ocr(state, ocr);
	printf("OCR:\n");
	for (i=0; i<sizeof(ocr); i++)
		printf("    0x%02x %c\n", ocr[i], isprint(ocr[i])?ocr[i]:'.');
	*/

	//printf("Setting block length: 0x%02x\n", sd_set_blocklength(state, 512));

	if (sd_get_csd(state, csd)) {
		printf("Unable to get CSD\n");
	}
	else {
		print_csd(csd);
		printf("CSD:\n");
		for (i=0; i<sizeof(csd); i++)
			printf("    0x%02x %c\n", csd[i], isprint(csd[i])?csd[i]:'.');
	}
	

	if (sd_get_cid(state, cid)) {
		printf("Unable to get CID\n");
	}
	else {
		printf("CID:\n");
		print_cid(cid);
		for (i=0; i<sizeof(cid); i++)
			printf("    0x%02x %c\n", cid[i], isprint(cid[i])?cid[i]:'.');
	}


	sd_deinit(&state);

	return 0;
}
