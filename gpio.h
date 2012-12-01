#ifndef __GPIO_H__
#define __GPIO_H__

enum gpio_dir {
	GPIO_IN = 0,
	GPIO_OUT = 1,
};

enum gpio_edge {
	GPIO_EDGE_NONE,
	GPIO_EDGE_RISING,
	GPIO_EDGE_FALLING,
	GPIO_EDGE_BOTH,
};

int gpio_export(int gpio);
int gpio_unexport(int gpio);
int gpio_set_direction(int gpio, int is_output);
int gpio_set_value(int gpio, int value);
int gpio_get_value(int gpio);
int gpio_set_edge(int gpio, int edge);
#endif /* __GPIO_H__ */
