#ifndef __SD_H__
#define __SD_H__
#include <stdint.h>


#define SD_CMD0 0
#define SD_CMD1 1
#define SD_CMD8 8
#define SD_CMD9 9
#define SD_CMD12 12
#define SD_CMD13 13
#define SD_CMD10 10
#define SD_CMD16 16
#define SD_CMD17 17

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


#endif /* __SD_H__ */
