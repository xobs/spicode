#ifndef __SD_H__
#define __SD_H__
#include <stdint.h>
#include <arpa/inet.h>

#define SD_DEFAULT_BLKLEN 512

#define NET_PORT 7283
#define NET_MAX_CONNECTIONS 20
#define NET_PROMPT "cmd> "
#define NET_MAX_TRIES 20

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


enum sd_parse_mode {
    PARSE_MODE_BINARY,
    PARSE_MODE_LINE,
};

struct sd;

struct sd_syscmd {
    const uint8_t cmd[2];
    const uint32_t flags;
    const char *description;
    int(*handle_cmd)(struct sd *server, int arg);
};

/* Command as it travels over the wire in binary mode */
struct sd_cmd {
    uint8_t cmd[2]; /* `\                   */
                    /*    > Network packet  */
    uint32_t arg;   /* ,/                   */


    struct sd_syscmd *syscmd;
};


struct sd {
    enum sd_parse_mode    parse_mode;

    int                         net_socket;
    int                         net_fd;
    struct sockaddr_in          net_sockaddr;
    uint32_t                    net_buf_len;
    uint8_t                     net_bfr[512];
    uint32_t                    net_bfr_ptr;

    struct sd_syscmd     *cmds;

    /* Raw SD commands */
    uint8_t                     sd_registers[4];
    uint32_t                    sd_blklen;
    uint8_t                    *sd_buffer;

    /* GPIO pins */
    uint32_t                    sd_miso, sd_mosi;
    uint32_t                    sd_clk, sd_cs, sd_power;
};


#define CMD_FLAG_ARG 0x01 /* True if the command has an arg */

int parse_init(struct sd *server);
int parse_get_next_command(struct sd *server, struct sd_cmd *cmd);
int parse_set_mode(struct sd *server, enum sd_parse_mode mode);
int parse_deinit(struct sd *server);
int parse_set_hook(struct sd *server, char cmd[2], int
        (*hook)(struct sd *, int));

int net_init(struct sd *server);
int net_accept(struct sd *server);
int net_write(struct sd *server, char *txt);
int net_get_packet(struct sd *server, uint8_t **data);
int net_deinit(struct sd *server);


int sd_init(struct sd *server, uint8_t cmd_in, uint8_t cmd_out, uint8_t clk,
			 uint8_t cs, uint8_t power);
void sd_deinit(struct sd **state);

int sd_reset(struct sd *state);
int sd_get_ocr(struct sd *state, uint8_t ocr[4]);
int sd_get_cid(struct sd *state, uint8_t cid[16]);
int sd_get_csd(struct sd *state, uint8_t csd[16]);
int sd_get_sr(struct sd *state, uint8_t sr[6]);
int sd_set_blocklength(struct sd *state, uint32_t blklen);
int sd_read_block(struct sd *state, uint32_t offset, void *block, uint32_t count);
int sd_write_block(struct sd *state, uint32_t offset, const void *block, uint32_t count);


#endif /* __SD_H__ */
