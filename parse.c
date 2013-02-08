#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sd.h"

static int do_unknown_cmd(struct sd *server, int arg);

#define HELP_BLANK_LINE    {"  ", 0, "", do_unknown_cmd},

static struct sd_syscmd __cmds[] = {
    {"rc", 0, "Reset card, counters, and buffers"},
    {"bm", 0, "Switch to binary network mode"},
    {"lm", 0, "Switch to line network mode"},
    HELP_BLANK_LINE

    {"ci", 0, "Return card CID"},
    {"cs", 0, "Return card CSD"},
    HELP_BLANK_LINE

    {"so", CMD_FLAG_ARG, "Set sector offset to arg"},
    {"sz", CMD_FLAG_ARG, "Set sector size to arg"},
    {"go", 0, "Get sector offset"},
    {"gz", 0, "Get sector size"},
    HELP_BLANK_LINE

    {"rs", 0, "Read from current sector"},
    {"ws", 0, "Write to current sector"},
    HELP_BLANK_LINE

    {"rb", 0, "Reset write buffer pointer to offset 0"},
    {"sb", CMD_FLAG_ARG, "Set write buffer value to arg and increment the pointer"},
    {"bp", 0, "Get write buffer pointer offset"},
    {"bo", CMD_FLAG_ARG, "Set write buffer pointer offset to the specified arg"},
    {"bc", 0, "Return write buffer contents"},
    {"cb", 0, "Copy read buffer contents to write buffer"},
    {"ps", CMD_FLAG_ARG, "Select the pattern set specified in arg"},
    {"ib", CMD_FLAG_ARG, "Ignore the first [arg] packets"},
    HELP_BLANK_LINE

    {"c+", 0, "Enable clock auto-tick"},
    {"c-", 0, "Disable clock auto-tick"},
    {"tk", 0, "Tick clock once"},
    {"tc", CMD_FLAG_ARG, "Tick clock number of times specified by arg"},
    HELP_BLANK_LINE

    {"r0", CMD_FLAG_ARG, "Set SD register 0 to arg value"},
    {"r1", CMD_FLAG_ARG, "Set SD register 1 to arg value"},
    {"r2", CMD_FLAG_ARG, "Set SD register 2 to arg value"},
    {"r3", CMD_FLAG_ARG, "Set SD register 3 to arg value"},
    {"cd", CMD_FLAG_ARG, "Send raw SD command specified by the arg"},
    {"rr", 0, "Reset register values to 0"},
    HELP_BLANK_LINE

    {"p-", 0, "Turn card power off"},
    {"p+", 0, "Turn card power on and reset card"},
    HELP_BLANK_LINE

    {"ip", CMD_FLAG_ARG, "Set destination IPv4 address to arg"},
    {"up", CMD_FLAG_ARG, "Set destination UDP port to arg"},
    HELP_BLANK_LINE
    {"\0\0", 0, NULL},
};

static int do_unknown_cmd(struct sd *server, int arg) {
    pkt_send_error(server, MAKE_ERROR(SUBSYS_PARSE, PARSE_ERR_UNKNOWN_CMD, 0),
			"Unknown command");
    return 0;
}

static int do_error_cmd(struct sd *server, int arg) {
    pkt_send_error(server, MAKE_ERROR(SUBSYS_PARSE, PARSE_ERR_UNKNOWN, 0),
			"An unknown error occurred");
    return 0;
}

static struct sd_syscmd unknown_cmd =
    {"?!", 0, "Unknown command", do_unknown_cmd};


static struct sd_syscmd error_cmd =
    {"!!", 0, "An error occurred", do_error_cmd};


static struct sd_syscmd *get_syscmd(struct sd *server,
                                          const uint8_t txt[2]) {
    struct sd_syscmd *c = server->cmds;
    while(c && c->description) {
        if (!memcmp(c->cmd, txt, sizeof(c->cmd)))
            return c;
        c++;
    }
    return NULL;
}

static int is_valid_command(struct sd *server, struct sd_cmd *cmd) {
    struct sd_syscmd *syscmd;
    syscmd = get_syscmd(server, cmd->cmd);
    if (syscmd)
        return 1;
    return 0;
}


/* Get one line, without exceeding either the output or input buffer lengths */
static int getnnline(uint8_t *output, int output_len,
                     uint8_t *input, int input_len) {
    int sz;

    for (sz=0; sz<(output_len-1) && sz<(input_len-1); sz++) {
        output[sz] = input[sz];
    }
    output[sz] = '\0';
    return sz;
}


static int real_parse_cmd(struct sd *server, struct sd_cmd *cmd,
                          uint8_t *buf, int len) {
    int size_copied;

    if (server->parse_mode == PARSE_MODE_BINARY) {
        /* Make sure there's at least enough space for the command */
        if (len < sizeof(cmd->cmd)) {
            errno = EMSGSIZE;
            memcpy(cmd->cmd, error_cmd.cmd, sizeof(error_cmd.cmd));
            cmd->syscmd = &error_cmd;
            return -1;
        }

        bzero(cmd, sizeof(*cmd));

        if (sizeof(*cmd) < len)
            size_copied = sizeof(*cmd);
        else
            size_copied = len;

        memcpy(cmd, buf, size_copied);
        cmd->syscmd = get_syscmd(server, cmd->cmd);
    }

    else if (server->parse_mode == PARSE_MODE_LINE) {
        uint8_t line[BUFSIZ];
        int offset;
        struct sd_syscmd *syscmd;

        size_copied = getnnline(line, sizeof(line)-1, buf, len);

        offset = 0;
        while(isspace(line[offset]) && line[offset] != '\0')
            offset++;
        if (line[offset] == '\0') {
            errno = EINVAL;
            return -1;
        }
        cmd->cmd[0] = line[offset++];
        if (line[offset] == '\0') {
            errno = EINVAL;
            return -1;
        }
        cmd->cmd[1] = line[offset++];

        syscmd = get_syscmd(server, cmd->cmd);
        if (!syscmd) {
            errno = EINVAL;
            size_copied = -1;
            cmd->arg = 0;
        }
        else {
            /* If an arg is required, parse that */
            if (syscmd->flags & CMD_FLAG_ARG) {
                while(isspace(line[offset]) && line[offset] != '\0')
                    offset++;
                cmd->arg = strtoul((char *)&line[offset], NULL, 0);
            }
            cmd->syscmd = syscmd;
        }
    }

    else {
        errno = EINVAL;
        size_copied = -1;
        memcpy(cmd->cmd, unknown_cmd.cmd, sizeof(unknown_cmd.cmd));
        cmd->syscmd = &unknown_cmd;
    }

    if (!is_valid_command(server, cmd))
        size_copied = -1;

    return size_copied;
}

int parse_write_prompt(struct sd *server) {
    return 0;
}

int parse_get_next_command(struct sd *server, struct sd_cmd *cmd) {
    uint8_t *buf;
    int ret;

    ret = net_get_packet(server, &buf);
    if (ret <= 0)
        return -1;

    /* Got a command.  Was it valid? */
    ret = real_parse_cmd(server, cmd, buf, ret);

    /* Invalid command */
    if (ret < 0) {
        memcpy(cmd->cmd, unknown_cmd.cmd, sizeof(unknown_cmd.cmd));
        cmd->syscmd = &unknown_cmd;
        ret = 0;
    }

    return ret;
}

int parse_set_hook(struct sd *server, char cmd[2], int
        (*hook)(struct sd *, int)) {
    struct sd_syscmd *syscmd = get_syscmd(server, (uint8_t *)cmd);
    if (!syscmd)
        return -1;
    syscmd->handle_cmd = hook;
    return 0;
}

int parse_set_mode(struct sd *server, enum sd_parse_mode mode) {
    server->parse_mode = mode;
    return 0;
}

int parse_init(struct sd *server) {
    server->parse_mode = PARSE_MODE_LINE;
    server->cmds = malloc(sizeof(__cmds));
    memcpy(server->cmds, __cmds, sizeof(__cmds));
    return 0;
}

int parse_deinit(struct sd *server) {
    free(server->cmds);
    return 0;
}
