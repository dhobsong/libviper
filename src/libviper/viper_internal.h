/*
 * libviper: A library for controlling Renesas Video Image Processing
 * Copyright (C) 2014 IGEL Co., Ltd.
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

#ifndef VIPER_INTERNAL_H
#define VIPER_INTERNAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/v4l2-mediabus.h>

#define VIPER_CAPS_INPUT 		0x1
#define VIPER_CAPS_OUTPUT 	0x2
#define VIPER_CAPS_RESIZE 	0x4


#define MAX_SUBPIPES 4

struct viper_entity;

struct entity_capability {
	char 		name[255];
	bool		io_entity;
	unsigned int 	caps;
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
	unsigned int media_id;
	struct viper_io_entity *io_entity;
	struct viper_entity *next;
	struct viper_entity *next_locked;
};

struct viper_device {
	char name[255];
	int media_fd;
	struct viper_entity *entity_list;
	struct viper_io_entity *io_entity_list;
	int active_subpipe;
	struct viper_entity *subpipe_final[MAX_SUBPIPES];
//	struct viper_entity *locked_entities;
	struct viper_device *next;
};

struct viper_context {
	struct viper_device *device_list;
	pthread_mutex_t	lock;
	int ref_cnt;
};

#define MAX_INPUT_BUFFERS 4
#define MAX_OUTPUT_BUFFERS 4
#define MAX_PLANES 2

struct viper_pipeline {
	int	num_inputs;
	int	num_outputs;
	struct viper_entity *locked_entities;

	int	input_fds[MAX_INPUT_BUFFERS];
	void	*input_addr[MAX_INPUT_BUFFERS][MAX_PLANES];
	int	input_size[MAX_INPUT_BUFFERS][MAX_PLANES];
	int	input_planes[MAX_INPUT_BUFFERS];

	int	output_fds[MAX_OUTPUT_BUFFERS];
	void	*output_addr[MAX_OUTPUT_BUFFERS][MAX_PLANES];
	int	output_size[MAX_OUTPUT_BUFFERS][MAX_PLANES];
	int	output_planes[MAX_OUTPUT_BUFFERS];
};

struct viper_pipeline * create_pipeline(struct viper_device *dev,
		int *caps_list, void **args_list, int length);

int queue_buffer(int fd, void **buffer, int *size, int count, bool input);

static enum v4l2_mbus_pixelcode color_fmt_to_code(uint32_t format) {
	switch (format) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_UYVY:
		return V4L2_MBUS_FMT_AYUV8_1X32;
	default:
		return V4L2_MBUS_FMT_ARGB8888_1X32;
	}
}
int init_context ();
#endif
