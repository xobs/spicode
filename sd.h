#ifndef __SD_H__
#define __SD_H__
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SD_DEFAULT_BLKLEN 512

#define NET_PORT 7283
#define NET_DATA_PORT 17283
#define NET_MAX_CONNECTIONS 20
#define NET_PROMPT "cmd> "
#define NET_MAX_TRIES 20

#ifdef DEBUG
#define DBG(fmt, ...) \
        fprintf(stderr, "%s:%s:%d " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(...)
#endif

#define MAKE_ERROR(x, y, z) (((x<<24)&0xff000000) | ((y<<16)&0x00ff0000) | ((z<<0)&0x0000ffff))

/*
 * Error codes are 32-bit values consisting of:
 *
 * XX YY ZZZZ
 *
 * XX = subsystem ID
 * YY = error code within the subsystem
 * ZZZZ = Subsystem-specific code
 */

enum subsystem_ids {
	SUBSYS_NONE = 0,
	SUBSYS_SD = 1,
	SUBSYS_NET = 2,
	SUBSYS_FPGA = 3,
	SUBSYS_PARSE = 4,
	SUBSYS_PKT = 5,
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
	int			should_exit;

	enum sd_parse_mode	parse_mode;

	int			net_socket;
	int			net_socket_data;
	int			net_fd;
	struct sockaddr_in	net_sockaddr;
	struct sockaddr_in	net_sockaddr_data;
	uint32_t		net_buf_len;
	uint8_t			net_bfr[512];
	uint32_t		net_bfr_ptr;
	int			net_port;
	int			net_data_port;

	struct sd_syscmd	*cmds;

	/* Raw SD commands */
	uint8_t			sd_registers[4];
	uint32_t		sd_blklen;
	uint8_t			*sd_buffer;

	/* GPIO pins */
	uint32_t		sd_miso, sd_mosi;
	uint32_t		sd_clk, sd_cs, sd_power;
	uint32_t		sd_sector; /* Current sector */
	uint32_t		sd_write_buffer_offset;
	uint8_t			sd_read_bfr[512];
	uint8_t			sd_write_bfr[512];

	/* FPGA communications */
	int			fpga_ready_fd, fpga_overflow_fd;
	struct timespec		fpga_starttime;
	/* Number of times FPGA clock has wrapped */
	uint32_t		fpga_reset_clock;
	int			fpga_wraps;
	uint32_t		fpga_overflow_pin;
	uint32_t		fpga_overflow_pin_value;
	uint32_t		fpga_clock_ticks;
	pthread_t		fpga_overflow_thread;
	pthread_t		fpga_data_available_thread;
	pthread_mutex_t		fpga_overflow_mutex;
};


enum cmd_flags {
	CMD_FLAG_ARG = 1, /* True if the command has an arg */
};

int parse_init(struct sd *server);
int parse_get_next_command(struct sd *server, struct sd_cmd *cmd);
int parse_set_mode(struct sd *server, enum sd_parse_mode mode);
int parse_deinit(struct sd *server);
int parse_set_hook(struct sd *server, char cmd[2], int
        (*hook)(struct sd *, int));
int parse_write_prompt(struct sd *server);


int net_init(struct sd *server);
int net_accept(struct sd *server);
int net_write_line(struct sd *server, char *txt);
int net_write_data(struct sd *server, void *data, size_t count);
int net_get_packet(struct sd *server, uint8_t **data);
int net_fd(struct sd *server);
int net_deinit(struct sd *server);


int sd_init(struct sd *server, uint8_t cmd_in, uint8_t cmd_out, uint8_t clk,
			 uint8_t cs, uint8_t power, uint8_t reset_clock);
void sd_deinit(struct sd **state);
int sd_reset(struct sd *state);
int sd_get_ocr(struct sd *state, uint8_t ocr[4]);
int sd_get_cid(struct sd *state, uint8_t cid[16]);
int sd_get_csd(struct sd *state, uint8_t csd[16]);
int sd_get_sr(struct sd *state, uint8_t sr[6]);
int sd_set_blocklength(struct sd *state, uint32_t blklen);
int sd_read_block(struct sd *state, uint32_t offset, void *block, uint32_t count);
int sd_write_block(struct sd *state, uint32_t offset, const void *block, uint32_t count);
int sd_get_elapsed(struct sd *state, time_t *tv_sec, long *tv_nsec);


enum fpga_errs {
	FPGA_ERR_UNKNOWN_PKT,
	FPGA_ERR_OVERFLOW,
};
int fpga_init(struct sd *st);
int fpga_data_avail(struct sd *st);
int fpga_get_new_sample(struct sd *st, uint8_t data[13]);
int fpga_read_data(struct sd *st);
int fpga_ready_fd(struct sd *st);
int fpga_overflow_fd(struct sd *st);
int fpga_tick_clock_maybe(struct sd *sd);
int fpga_reset_ticks(struct sd *sd);
uint32_t fpga_ticks(struct sd *sd);


int pkt_send_error(struct sd *sd, uint32_t code, char *msg);
int pkt_send_nand_cycle(struct sd *sd, uint32_t fpga_counter, uint8_t data, uint8_t ctrl, uint8_t unk[2]);
int pkt_send_sd_data(struct sd *sd, uint8_t *block);
int pkt_send_sd_cmd_arg(struct sd *sd, uint8_t regnum, uint8_t val);
int pkt_send_sd_cmd_arg_fpga(struct sd *sd, uint32_t fpga_counter, uint8_t regnum, uint8_t val);
int pkt_send_sd_response(struct sd *sd, uint8_t byte);
int pkt_send_sd_response_fpga(struct sd *sd, uint32_t fpga_counter, uint8_t byte);
int pkt_send_sd_cid(struct sd *sd, uint8_t cid[16]);
int pkt_send_sd_csd(struct sd *sd, uint8_t csd[16]);
int pkt_send_buffer_offset(struct sd *sd, uint8_t buffertype, uint32_t offset);
int pkt_send_buffer_contents(struct sd *sd, uint8_t buffertype, uint8_t *buffer);
int pkt_send_command(struct sd *sd, struct sd_cmd *cmd);
int pkt_send_reset(struct sd *sd);


#endif /* __SD_H__ */
