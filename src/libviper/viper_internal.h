/*
 * libviper: A library for controlling Renesas Video Image Processing
 * Copyright (C) 2014 Igel Co., Ltd.
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef VIP_INTERNAL_H
#define VIP_INTERNAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/v4l2-mediabus.h>

#define VIP_CAPS_INPUT 		0x1
#define VIP_CAPS_OUTPUT 	0x2
#define VIP_CAPS_RESIZE 	0x4

struct viper_entity;

struct entity_capability {
	char 		name[255];
	bool		io_entity;
	uint32_t 	caps;
	int (*config) (struct viper_entity *entity, void *args);
};

struct viper_io_entity {
	char *name;
	int fd;	
	struct viper_io_entity *next;
};


struct viper_entity {
	const struct entity_capability *caps;
	char *name;
	int pads;
	int links;
	pthread_mutex_t	lock;
	int fd;
	int media_id;
	struct viper_io_entity *io_entity;
	struct viper_entity *next;
	struct viper_entity *next_locked;
};

struct viper_device {
	char name[255];
	int media_fd;
	struct viper_entity *entity_list;
	struct viper_io_entity *io_entity_list;
	struct viper_device *next;
};

struct viper_context {
	struct viper_device *device_list;
	struct viper_entity *locked_entities;
	bool pipeline_complete;
};
#endif
