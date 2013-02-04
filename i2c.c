#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "sd.h"

#define FPGA_I2C_BUS 0
#define FPGA_I2C_DEVICE 0x1e

int i2c_init(struct sd *sd) {
	char i2c_device[1024];
	snprintf(i2c_device, sizeof(i2c_device)-1, "/dev/i2c-%d", FPGA_I2C_BUS);
	if ((sd->i2c_fpga_fd = open(i2c_device, O_RDWR))==-1) {
		perror("Unable to open fpga device");
		sd->i2c_fpga_fd = 0;
		return 1;
	}

	sd->i2c_fpga_bus = FPGA_I2C_BUS;
	sd->i2c_fpga_device = FPGA_I2C_DEVICE;
	return 0;
}

int i2c_set_buffer(struct sd *sd, uint8_t addr, uint8_t count, uint8_t *buffer) {
	uint8_t data[count+1];
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[1];

	// Set the address we'll read to the start address.
	data[0] = addr;
	memcpy(data+1, buffer, count);

	messages[0].addr = sd->i2c_fpga_device;
	messages[0].flags = 0;
	messages[0].len = count+1;
	messages[0].buf = data;

	packets.msgs = messages;
	packets.nmsgs = 1;

	if(ioctl(sd->i2c_fpga_fd, I2C_RDWR, &packets) < 0) {
		perror("Unable to communicate with i2c device");
		return 1;
	}

	return 0;
}


int i2c_set_byte(struct sd *sd, uint8_t addr, uint8_t value) {
	return i2c_set_buffer(sd, addr, 1, &value);
}
