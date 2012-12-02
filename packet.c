#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "sd.h"

enum FPGAFrequency {
	FPGA_FREQUENCY = 125000000,
};

enum PacketType {
	PACKET_UNKNOWN = 0,
	PACKET_ERROR = 1,
	PACKET_NAND_CYCLE = 2,
	PACKET_SD_DATA = 3,
	PACKET_SD_CMD_ARG = 4,
	PACKET_SD_RESPONSE = 5,
	PACKET_SD_CID = 6,
	PACKET_SD_CSD = 7,
	PACKET_BUFFER_OFFSET = 8,
	PACKET_BUFFER_CONTENTS = 9,
	PACKET_COMMAND = 10,
};


/* Generic packet header
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  1   | Packet type (as defined in WPacketType)
 *     1   |  4   | Seconds since reset
 *     5   |  4   | Nanoseconds since reset
 */
static int pkt_set_header(struct sd *sd, char *pkt, int type) {
	long sec, nsec;
	pkt[0] = type;
	sd_get_elapsed(sd, &sec, &nsec);
	sec = htonl(sec);
	nsec = htonl(nsec);
	memcpy(pkt+1, &sec, sizeof(sec));
	memcpy(pkt+5, &nsec, sizeof(nsec));
	return 0;
}


/* Generic packet header (for FPGA ticks)
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  1   | Packet type (as defined in WPacketType)
 *     1   |  4   | Seconds since reset
 *     5   |  4   | Nanoseconds since reset
 */
static int pkt_set_header_fpga(struct sd *sd, char *pkt, uint32_t fpga_counter, int type) {
	uint32_t ticks = fpga_ticks(sd);
	long long total_ticks = ticks*0x100000000LL + fpga_counter;
	long long total_secs;
	long long nsec_ticks;
	uint32_t sec, nsec;

	total_secs = total_ticks / FPGA_FREQUENCY;
	nsec_ticks = total_ticks - (total_secs * FPGA_FREQUENCY);
	sec = total_secs;
	nsec = (nsec_ticks * 1000000000) / FPGA_FREQUENCY;
//	fprintf(stderr, "After %d ticks, we have %d sec and %d nsec\n", ticks, sec, nsec);

	pkt[0] = type;
	sec = htonl(sec);
	nsec = htonl(nsec);
	memcpy(pkt+1, &sec, sizeof(sec));
	memcpy(pkt+5, &nsec, sizeof(nsec));
	return 0;
}


/* PKT_ERROR
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  4   | Error code
 *    13   | 512  | Textual error message (NULL-padded)
 */
int pkt_send_error(struct sd *sd, uint32_t code, char *msg) {
	char pkt[9+4+512];
	uint32_t real_code;

	bzero(pkt, sizeof(pkt));

	pkt_set_header(sd, pkt, PACKET_ERROR);
	real_code = htonl(code);
	memcpy(pkt+9, &real_code, sizeof(real_code));

	strncpy(pkt+13, msg, 512-1);
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_NAND_CYCLE format (FPGA):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  1   | Data/Command pins
 *    10   |  1   | Bits [0..4] are ALE, CLE, WE, RE, and CS (in order)
 *    11   |  2   | Bits [0..9] are the unknown pins
 */
int pkt_send_nand_cycle(struct sd *sd, uint32_t fpga_counter, uint8_t data, uint8_t ctrl, uint8_t unk[2]) {
	char pkt[9+1+1+2];
	pkt_set_header_fpga(sd, pkt, fpga_counter, PACKET_NAND_CYCLE);
	pkt[9] = data;
	pkt[10] = ctrl;
	pkt[11] = unk[0];
	pkt[12] = unk[1];
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_SD_DATA format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   | 512  | One block of data from the card
 */
int pkt_send_sd_data(struct sd *sd, uint8_t *block) {
	char pkt[9+512];
	pkt_set_header(sd, pkt, PACKET_SD_DATA);
	memcpy(pkt+9, block, 512);
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_SD_CMD_ARG format (FPGA):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  1   | Register number (1, 2, 3, or 4), or 0 for the CMD byte
 *    10   |  1   | Value of the register or CMD number
 */

int pkt_send_sd_cmd_arg(struct sd *sd, uint32_t fpga_counter, uint8_t regnum, uint8_t val) {
	char pkt[9+1+1];
	pkt_set_header_fpga(sd, pkt, fpga_counter, PACKET_SD_CMD_ARG);
	pkt[9] = regnum;
	pkt[10] = val;
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_SD_RESPONSE format (FPGA):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  1   | The contents of the first byte that the card answered with
 */
int pkt_send_sd_response(struct sd *sd, uint32_t fpga_counter, uint8_t byte) {
	char pkt[9+1];
	pkt_set_header_fpga(sd, pkt, fpga_counter, PACKET_SD_RESPONSE);
	pkt[9] = byte;
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_SD_CID format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  16  | Contents of the card's CID
 */
int pkt_send_sd_cid(struct sd *sd, uint8_t cid[16]) {
	char pkt[9+16];
	pkt_set_header(sd, pkt, PACKET_SD_CID);
	memcpy(pkt+9, cid, 16);
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_SD_CSD format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  16  | Contents of the card's CSD
 */
int pkt_send_sd_csd(struct sd *sd, uint8_t csd[16]) {
	char pkt[9+16];
	pkt_set_header(sd, pkt, PACKET_SD_CSD);
	memcpy(pkt+9, csd, 16);
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_BUFFER_OFFSET format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  1   | 1 if this is the read buffer, 2 if it's write
 *    10   |  4   | Offset of the current buffer pointer
 */
int pkt_send_buffer_offset(struct sd *sd, uint8_t buffertype, uint32_t offset) {
	char pkt[9+1+4];
	uint32_t real_offset;
	pkt_set_header(sd, pkt, PACKET_BUFFER_OFFSET);
	pkt[9] = buffertype;
	real_offset = htonl(offset);
	memcpy(pkt+10, &real_offset, sizeof(real_offset));
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_BUFFER_CONTENTS format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  1   | 1 if this is the read buffer, 2 if it's write
 *    10   | 512  | Contents of the buffer
 */
int pkt_sent_buffer_contents(struct sd *sd, uint8_t buffertype, uint8_t *buffer) {
	char pkt[9+1+512];
	pkt_set_header(sd, pkt, PACKET_BUFFER_CONTENTS);
	pkt[9] = buffertype;
	memcpy(pkt+10, buffer, 512);
	return net_write_data(sd, pkt, sizeof(pkt));
}


/*
 * PACKET_COMMAND format (CPU):
 *  Offset | Size | Description
 * --------+------+-------------
 *     0   |  9   | Header
 *     9   |  2   | Two-character command code
 *    11   |  4   | 32-bit command argument
 */
int pkt_send_command(struct sd *sd, struct sd_cmd *cmd) {
	char pkt[9+2+4];
	uint32_t arg;
	pkt_set_header(sd, pkt, PACKET_COMMAND);
	pkt[9] = cmd->cmd[0];
	pkt[10] = cmd->cmd[1];
	arg = htonl(cmd->arg);
	memcpy(pkt+9+2, &arg, sizeof(arg));
	return net_write_data(sd, pkt, sizeof(pkt));
}
