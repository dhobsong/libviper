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
#ifndef ENTITY_CONF_H
#define ENTITY_CONF_H
#include "viper_internal.h"
struct viper_rpf_config {
	int width;
	int height;
	int bpitch0;
	int bpitch1;
	int bpitch2;
	int planes;
	uint32_t format;
	enum v4l2_mbus_pixelcode code;
};
int configure_rpf(struct viper_entity *entity, void *args);

struct viper_wpf_config {
	int width;
	int height;
	int bpitch0;
	int bpitch1;
	int bpitch2;
	int planes;
	uint32_t in_format;
	enum v4l2_mbus_pixelcode in_code;
	uint32_t out_format;
	enum v4l2_mbus_pixelcode out_code;
};
int configure_wpf(struct viper_entity *entity, void *args);

struct viper_uds_config {
	int in_width;
	int in_height;
	int out_width;
	int out_height;
	enum v4l2_mbus_pixelcode code;
};

int configure_uds(struct viper_entity *entity, void *args);

#define BRU_MAX_INPUTS 4

struct viper_bru_config {
	int in_widths[BRU_MAX_INPUTS];
	int in_heights[BRU_MAX_INPUTS];
	int in_tops[BRU_MAX_INPUTS];
	int in_lefts[BRU_MAX_INPUTS];
	int out_width;
	int out_height;
	int inputs;
	enum v4l2_mbus_pixelcode code;
};

int configure_bru(struct viper_entity *entity, void *args);
#endif
