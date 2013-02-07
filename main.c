#define DEBUG
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include "sd.h"
#include "gpio.h"

/** Definitions for Kovan test jig */
#define CS_PIN 50
#define MISO_PIN 62
#define CLK_PIN 46
#define MOSI_PIN 48
#define POWER_PIN 55
#define CLOCK_RESET_PIN 59

/* Time out after 250 ms, in order to check for e.g. FPGA data available */
#define POLL_TIMEOUT 250


static int set_binmode(struct sd *server, int arg) {
	parse_set_mode(server, PARSE_MODE_BINARY);
	return 0;
}

static int set_linemode(struct sd *server, int arg) {
	parse_set_mode(server, PARSE_MODE_LINE);
	return 0;
}


static int get_net_command(struct sd *server, struct sd_cmd *cmd) {
	int ret;

	ret = parse_get_next_command(server, cmd);
	if (ret < 0) {
		perror("Quitting");
		return -1;
	}

#ifdef DEBUG
	fprintf(stderr, "Got command: %c%c - %s", cmd->cmd[0], cmd->cmd[1],
		cmd->syscmd->description);
	if (cmd->syscmd->flags & CMD_FLAG_ARG)
		fprintf(stderr, " - arg: %d", cmd->arg);
	fprintf(stderr, "\n");
#endif
	return 0;
}

static int handle_net_command(struct sd *server, struct sd_cmd *cmd) {
	/* In reality, all commands should have a handle routine */
	if (cmd->syscmd->handle_cmd)
		cmd->syscmd->handle_cmd(server, cmd->arg);
	else
		fprintf(stderr, "WARNING: Command %c%c missing handle_cmd\n",
			cmd->cmd[0], cmd->cmd[1]);
	return 0;
}



static void *clock_overflow_thread(void *arg) {
	struct sd *server = arg;
	int ret;

	while (!server->should_exit) {
		struct pollfd handles[1];

		bzero(handles, sizeof(handles));
		handles[0].fd     = fpga_overflow_fd(server);
		handles[0].events = POLLPRI;

		ret = poll(handles, sizeof(handles)/sizeof(*handles), POLL_TIMEOUT);
		if (ret < 0) {
			perror("Couldn't poll");
			return NULL;
		}

		if (fpga_tick_clock_maybe(server))
			fprintf(stderr, "Clock wrapped\n");
	}
	return NULL;
}


static void *data_available_thread(void *arg) {
	struct sd *server = arg;
	int ret;

	while (!server->should_exit) {
		struct pollfd handles[1];

		bzero(handles, sizeof(handles));
		handles[0].fd     = fpga_ready_fd(server);
		handles[0].events = POLLPRI;

		ret = poll(handles, sizeof(handles)/sizeof(*handles), POLL_TIMEOUT);
		if (ret < 0) {
			perror("Couldn't poll");
			return NULL;
		}

		while(fpga_data_avail(server)) {
			fprintf(stderr, "Got FPGA data, draining...\n");
			pkt_send_buffer_drain(server, PKT_BUFFER_DRAIN_START);
			fpga_drain(server);
			pkt_send_buffer_drain(server, PKT_BUFFER_DRAIN_STOP);
			fprintf(stderr, "Done draining\n");
		}
	}
	return NULL;
}



int main(int argc, char **argv) {
	struct sd server;
	int ret;


	bzero(&server, sizeof(server));

	ret = parse_init(&server);
	if (ret < 0) {
		perror("Couldn't initialize parser");
		return 1;
	}

	ret = net_init(&server);
	if (ret < 0) {
		perror("Couldn't initialize network");
		return 1;
	}

	ret = i2c_init(&server);
	if (ret < 0) {
		perror("Couldn't initialize i2c");
		return 1;
	}

	ret = fpga_init(&server);
	if (ret < 0) {
		perror("Couldn't initialize NAND");
		return 1;
	}

	
	ret = sd_init(&server,
		      MISO_PIN, MOSI_PIN, CLK_PIN, CS_PIN,
		      POWER_PIN, CLOCK_RESET_PIN);
        if (ret < 0) {
                return 1;
		perror("Couldn't initialize SD");
		return 1;
	}


	ret = net_accept(&server);
	if (ret < 0) {
		perror("Couldn't accept network connections");
		return 1;
	}

	parse_set_hook(&server, "bm", set_binmode);
	parse_set_hook(&server, "lm", set_linemode);

	pkt_send_hello(&server);
	parse_write_prompt(&server);

	pthread_create(&server.fpga_overflow_thread, NULL,
		       clock_overflow_thread, &server);
	pthread_create(&server.fpga_data_available_thread, NULL,
		       data_available_thread, &server);
	while (1) {
		struct pollfd handles[1];

		bzero(handles, sizeof(handles));
		handles[0].fd     = net_fd(&server);
		handles[0].events = POLLIN | POLLHUP;

		ret = poll(handles, sizeof(handles)/sizeof(*handles), POLL_TIMEOUT);
		if (ret < 0) {
			perror("Couldn't poll");
			break;
		}

		if (handles[0].revents & POLLHUP) {
			printf("Remote side disconnected.  Quitting.\n");
			break;
		}
		if (handles[0].revents & POLLIN) {
			int ret;
			struct sd_cmd cmd;
			ret = get_net_command(&server, &cmd);
			if (ret)
				break;

			pkt_send_command(&server, &cmd, CMD_START);
			ret = handle_net_command(&server, &cmd);
			pkt_send_command(&server, &cmd, CMD_END);

			if (ret)
				break;
			parse_write_prompt(&server);
		}
	}
	server.should_exit = 1;
	net_deinit(&server);
	parse_deinit(&server);
	return 0;
}
