#ifndef __SD_H__
#define __SD_H__
#include <stdint.h>


enum sd_r1_states {
	SD_R1_IDLE_STATE = 0,
};

enum sd_cmds {
	SD_CMD0 = 0,
	SD_CMD1 = 1,
	SD_CMD8 = 8,
	SD_CMD9 = 9,
	SD_CMD12 = 12,
	SD_CMD13 = 13,
	SD_CMD10 = 10,
	SD_CMD16 = 16,
	SD_CMD17 = 17,
	SD_CMD41 = 41,
	SD_CMD55 = 55,
	SD_CMD58 = 58,
};

enum sd_value {
	SD_ON = 1,
	SD_OFF = 0,
};

enum sd_cs {
	CS_SEL = 0,
	CS_DESEL = 1,
};


struct sd_state;

struct sd_state *sd_init(uint8_t cmd_in, uint8_t cmd_out, uint8_t clk,
			 uint8_t cs, uint8_t power);
void sd_deinit(struct sd_state **state);

int sd_reset(struct sd_state *state);
int sd_get_ocr(struct sd_state *state, uint8_t ocr[4]);
int sd_get_cid(struct sd_state *state, uint8_t cid[16]);
int sd_get_csd(struct sd_state *state, uint8_t csd[16]);
int sd_get_sr(struct sd_state *state, uint8_t sr[6]);
int sd_set_blocklength(struct sd_state *state, uint32_t blklen);
int sd_read_block(struct sd_state *state, uint32_t offset, void *block, uint32_t count);
int sd_write_block(struct sd_state *state, uint32_t offset, const void *block, uint32_t count);


#endif /* __SD_H__ */
