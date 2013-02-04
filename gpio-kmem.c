#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "sd.h"
#include "gpio.h"

static int fd = 0;
static volatile int   *mem_32 = 0;
static volatile short *mem_16 = 0;
static volatile char  *mem_8  = 0;
static int *prev_mem_range = 0;

#define GPIO_PATH "/sys/class/gpio"

static int map_offset(long offset, int virtualized) {
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
    return 0;
}
 
static volatile int read_kernel_memory(long offset, int virtualized, int size) {
    int result;
    map_offset(offset, virtualized);
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

static int overwrite_kernel_memory(long offset, long value, int virtualized, int size) {
    map_offset(offset, virtualized);
    int scaled_offset = (offset-(offset&~0xFFFF));
    if(size==1)
        mem_8[scaled_offset/sizeof(char)]   = value;
    else if(size==2)
        mem_16[scaled_offset/sizeof(short)] = value;
    else
        mem_32[scaled_offset/sizeof(long)]  = value;
    return 0;
}

volatile int gpio_get_bank(int bank) {
	uint32_t base;

	if (bank == 0)
		base = 0xd4019000;
	else if (bank == 1)
		base = 0xd4019004;
	else if (bank == 2)
		base = 0xd4019008;
	else if (bank == 3)
		base = 0xd4019100;
	else {
		fprintf(stderr, "Invalid GPIO bank: %d\n", bank);
		return -1;
	}
	return read_kernel_memory(base, 0, 4);
}

int gpio_export(int gpio) {
	return 0;
}

int gpio_unexport(int gpio) {
	return 0;
}

int gpio_set_direction(int gpio, int is_output) {
	uint32_t base;
	uint32_t offset;
	if (gpio < 32)
		base = 0xd4019000;
	else if (gpio < 64)
		base = 0xd4019004;
	else if (gpio < 96)
		base = 0xd4019008;
	else
		base = 0xd4019100;

	if (is_output)
		offset = 0x0054;
	else
		offset = 0x0060;

	write_kernel_memory(offset+base, 1<<(gpio&0x1f), 0, 4);
	return 0;
}

int gpio_set_value(int gpio, int value) {
	uint32_t base;
	uint32_t offset;
	if (gpio < 32)
		base = 0xd4019000;
	else if (gpio < 64)
		base = 0xd4019004;
	else if (gpio < 96)
		base = 0xd4019008;
	else
		base = 0xd4019100;

	if (value)
		offset = 0x0018;
	else
		offset = 0x0024;

	overwrite_kernel_memory(offset+base, 1<<(gpio&0x1f), 0, 4);
	return 0;
}

int gpio_get_value(int gpio) {
	uint32_t base;
	long offset;
	if (gpio < 32)
		base = 0xd4019000;
	else if (gpio < 64)
		base = 0xd4019004;
	else if (gpio < 96)
		base = 0xd4019008;
	else
		base = 0xd4019100;
	offset = 0;
	return !!(read_kernel_memory(base+offset, 0, 4) & (1<<(gpio & 0x1f)));
}


int gpio_set_edge(int gpio, int edge) {
	char gpio_path[256];
	int fd;
	int ret;
	char *edge_str;

	if (edge == GPIO_EDGE_NONE)
		edge_str = "none";
	else if (edge == GPIO_EDGE_RISING)
		edge_str = "rising";
	else if (edge == GPIO_EDGE_FALLING)
		edge_str = "falling";
	else if (edge == GPIO_EDGE_BOTH)
		edge_str = "both";
	else {
		fprintf(stderr, "Unrecognized edge type for gpio %d\n", gpio);
		return -1;
	}

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/edge", gpio);

	fd = open(gpio_path, O_WRONLY);
	if (fd == -1) {
		char errormsg[256];
		snprintf(errormsg, sizeof(errormsg)-1, "Edge file %s: %s\n",
				gpio_path, strerror(errno));
		fputs(errormsg, stderr);
		return -errno;
	}

	ret = write(fd, edge_str, strlen(edge_str));

	if (ret == -1) {
		fprintf(stderr, "Couldn't set GPIO %d edge: %s\n",
			gpio, strerror(errno));
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}
