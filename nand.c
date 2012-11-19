#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include "sd.h"
#include "gpio.h"

static int fd = 0;
static volatile int   *mem_32 = 0;
static volatile short *mem_16 = 0;
static volatile char  *mem_8  = 0;
static int *prev_mem_range = 0;

static int data_pins[] = {45, 44, 42, 41, 40, 68, 38, 37, 63, 64, 65, 66, 67};
#define DATA_READY_PIN 61
#define GPIO_PATH "/sys/class/gpio"
#define GET_NEW_SAMPLE_PIN 54

static int read_kernel_memory(long offset, int virtualized, int size) {
    int result;

    int *mem_range = (int *)(offset & ~0xFFFF);
    if( mem_range != prev_mem_range ) {
        prev_mem_range = mem_range;

        if(mem_32)
            munmap((void *)mem_32, 0xFFFF);
        if(fd)
            close(fd);

        if(virtualized) {
            fd = open("/dev/kmem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/kmem");
                fd = 0;
                return -1;
            }
        }
        else {
            fd = open("/dev/mem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/mem");
                fd = 0;
                return -1;
            }
        }

        mem_32 = mmap(0, 0xffff, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset&~0xFFFF);
        if( -1 == (long)mem_32 ) {
            perror("Unable to mmap file");

            if( -1 == close(fd) )
                perror("Also couldn't close file");

            fd=0;
            return -1;
        }
        mem_16 = (short *)mem_32;
        mem_8  = (char  *)mem_32;
    }

    int scaled_offset = (offset-(offset&~0xFFFF));
    if(size==1)
        result = mem_8[scaled_offset/sizeof(char)];
    else if(size==2)
        result = mem_16[scaled_offset/sizeof(short)];
    else
        result = mem_32[scaled_offset/sizeof(long)];

    return result;
}

static int write_kernel_memory(long offset, long value, int virtualized, int size) {
    int old_value = read_kernel_memory(offset, virtualized, size);
    int scaled_offset = (offset-(offset&~0xFFFF));
    if(size==1)
        mem_8[scaled_offset/sizeof(char)]   = value;
    else if(size==2)
        mem_16[scaled_offset/sizeof(short)] = value;
    else
        mem_32[scaled_offset/sizeof(long)]  = value;
    return old_value;
}

static int get_gpio(int gpio) {
	long gpio_offset;
	if (gpio < 32)
		gpio_offset = 0xd4019000;
	else if (gpio < 64)
		gpio_offset = 0xd4019004;
	else if (gpio < 96)
		gpio_offset = 0xd4019008;
	else
		gpio_offset = 0xd4019100;
	return !!(read_kernel_memory(gpio_offset, 0, 4) & (1<<(gpio & 0x1f)));
}

int nand_get_new_sample(struct sd *st, uint8_t bytes[2]) {
	uint8_t data[13];
	long set_register = 0xd401901c;
	long clr_register = 0xd4019028;
	int tries;
	int i;
	int ret;
	usleep(2);
	write_kernel_memory(clr_register, 1<<(GET_NEW_SAMPLE_PIN&0x1f), 0, 4);
	usleep(2);
	write_kernel_memory(set_register, 1<<(GET_NEW_SAMPLE_PIN&0x1f), 0, 4);

	ret = -1;
	for (tries=0; tries<2; tries++) {
		if (get_gpio(60)) {
			DBG("Gpio went high after %d tries", tries);
			ret = 0;
			break;
		}
		usleep(1);
	}
	DBG("New sample never went ready!");

	for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++)
		data[i] = get_gpio(data_pins[i]);

	bytes[0] = data[0] | data[1]<<1 | data[2]<<2 | data[3]<<3 | data[4]<<4 | data[5]<<5 | data[6]<<6 | data[7]<<7;
	bytes[1] = data[8] | data[9]<<1 | data[10]<<2 | data[11]<<3 | data[12]<<4;
	return ret;
}

int nand_data_avail(struct sd *st) {
	struct pollfd fdset[1];
	fdset[0].fd = st->nand_fd;
	fdset[0].events = POLLPRI;
//	if (-1 == poll(fdset, 1, 3*1000)) {
//		perror("Error polling");
//	}
	return get_gpio(DATA_READY_PIN);
}


int nand_init(struct sd *sd) {
	int i;
	char str[256];

	gpio_export(DATA_READY_PIN);
	gpio_set_direction(DATA_READY_PIN, GPIO_IN);
	snprintf(str, sizeof(str)-1, "%s/gpio%d/value", GPIO_PATH, DATA_READY_PIN);
	sd->nand_fd = open(str, O_RDONLY | O_NONBLOCK);
	if (sd->nand_fd == -1)
		return -1;

	for (i=0; i<sizeof(data_pins)/sizeof(*data_pins); i++) {
		gpio_export(data_pins[i]);
		gpio_set_direction(data_pins[i], GPIO_IN);
	}
	return 0;
}


void *nand_thread(void *arg) {
	struct sd *sd = arg;
	while (!sd->should_exit) {
		uint8_t data[3];

		if (!nand_data_avail(sd))
			continue;
	
		/* Obtain the new sample and send it over the wire */
		nand_get_new_sample(sd, data+1);
		data[0] = NET_DATA_NAND;
		net_write_data(sd, data, sizeof(data));
	}

	return NULL;
}

