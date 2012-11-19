#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sd.h"

static int do_help(struct sd *server, int arg);

static struct sd_syscmd __cmds[] = {
    {"rc", 0, "Resets card, counters, and buffers"},
    {"bm", 0, "Switches to binary network mode"},
    {"lm", 0, "Switches to line network mode"},
    {"  ", 0, "", do_help},

    {"ci", 0, "Returns card CID"},
    {"cs", 0, "Returns card CSD"},
    {"  ", 0, "", do_help},

    {"so", CMD_FLAG_ARG, "Sets sector offset to arg"},
    {"sz", CMD_FLAG_ARG, "Sets sector size to arg"},
    {"go", 0, "Gets sector offset"},
    {"gz", 0, "Gets sector size"},
    {"  ", 0, "", do_help},

    {"rs", 0, "Reads from current sector"},
    {"ws", 0, "Writes to current sector"},
    {"  ", 0, "", do_help},

    {"rb", 0, "Resets write buffer pointer to offset 0"},
    {"sb", CMD_FLAG_ARG, "Sets write buffer value to arg and increments the pointer"},
    {"bp", 0, "Returns write buffer pointer offset"},
    {"ps", CMD_FLAG_ARG, "Sets pattern seed to arg"},
    {"bc", 0, "Returns write buffer contents"},
    {"cb", 0, "Copies the read buffer to the write buffer"},
    {"  ", 0, "", do_help},

    {"c+", 0, "Enable clock auto-tick"},
    {"c-", 0, "Disable clock auto-tick"},
    {"tk", 0, "Tick clock once"},
    {"tc", CMD_FLAG_ARG, "Tick clock number of times specified by arg"},
    {"  ", 0, "", do_help},

    {"r0", CMD_FLAG_ARG, "Set SD register 0 to arg value"},
    {"r1", CMD_FLAG_ARG, "Set SD register 1 to arg value"},
    {"r2", CMD_FLAG_ARG, "Set SD register 2 to arg value"},
    {"r3", CMD_FLAG_ARG, "Set SD register 3 to arg value"},
    {"cd", CMD_FLAG_ARG, "Send raw SD command specified by the arg"},
    {"rr", 0, "Resets register values to 0"},
    {"  ", 0, "", do_help},

    {"p-", 0, "Turns card power off"},
    {"p+", 0, "Turns card power on and resets card"},
    {"  ", 0, "", do_help},

    {"ip", CMD_FLAG_ARG, "Set destination IPv4 address to arg"},
    {"up", CMD_FLAG_ARG, "Set destination UDP port to arg"},
    {"  ", 0, "", do_help},

    {"he", 0, "Print this help message", do_help},
    {"??", 0, "Print this help message", do_help},
    {"\0\0", 0, NULL},
};

static int do_help(struct sd *server, int arg) {
    struct sd_syscmd *c = server->cmds;
    net_write_line(server, "Commands:\n");
    while (c->description) {
        char help_line[512];
        snprintf(help_line, sizeof(help_line)-1, "    %c%c %s %c %s\n", 
                c->cmd[0], c->cmd[1], (c->flags&CMD_FLAG_ARG)?"arg":"   ",
                c->handle_cmd?' ':'*', c->description);
        net_write_line(server, help_line);
        c++;
    }
    return 0;
}

static int do_unknown_cmd(struct sd *server, int arg) {
    printf("Unrecognized command\n");
    net_write_line(server, "? Unrecognized command\n");
    return 0;
}

static int do_error_cmd(struct sd *server, int arg) {
    printf("Error\n");
    net_write_line(server, "Error occurred\n");
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

int parse_get_next_command(struct sd *server, struct sd_cmd *cmd) {
    uint8_t *buf;
    int ret;

    if (server->parse_mode == PARSE_MODE_LINE)
        net_write_line(server, NET_PROMPT);
    else
        fprintf(stderr, "Parse mode is %d\n", server->parse_mode);

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
