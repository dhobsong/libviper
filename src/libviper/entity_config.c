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
#include <string.h>
#include "log.h"
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include "entity_config.h"
#include "viper_internal.h"


int configure_rpf(struct viper_entity *entity, void *args)
{
	struct viper_rpf_config *rpf_conf = (struct viper_rpf_config *)args;
	struct v4l2_subdev_format sfmt;
	struct v4l2_format fmt;

	memset(&sfmt, 0, sizeof(struct v4l2_subdev_format));
	sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sfmt.pad = 0;
	sfmt.format.width = rpf_conf->width;
	sfmt.format.height = rpf_conf->height;
	sfmt.format.code = rpf_conf->code;
	sfmt.format.field = V4L2_FIELD_NONE;
	sfmt.format.colorspace = V4L2_COLORSPACE_SRGB;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}

	sfmt.pad = 1;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}

	memset(&fmt, 0, sizeof(struct v4l2_format));
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width = rpf_conf->width;
	fmt.fmt.pix_mp.height = rpf_conf->height;
	fmt.fmt.pix_mp.pixelformat = rpf_conf->format;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline = rpf_conf->bpitch0;
	fmt.fmt.pix_mp.plane_fmt[1].bytesperline = rpf_conf->bpitch1;
	fmt.fmt.pix_mp.num_planes = rpf_conf->planes;

	if (ioctl (entity->io_entity->fd, VIDIOC_S_FMT, &fmt)) {
		viper_log("%s: VIDIOC_S_FMT failed %d\n", __FUNCTION__);
		return -1;
	}
	return 0;
}


int configure_wpf(struct viper_entity *entity, void *args)
{
	struct viper_wpf_config *wpf_conf = (struct viper_wpf_config *)args;
	struct v4l2_subdev_format sfmt;
	struct v4l2_format fmt;

	memset(&sfmt, 0, sizeof(struct v4l2_subdev_format));
	sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sfmt.pad = 0;
	sfmt.format.width = wpf_conf->width;
	sfmt.format.height = wpf_conf->height;
	sfmt.format.code = wpf_conf->in_code;
	sfmt.format.field = V4L2_FIELD_NONE;
	sfmt.format.colorspace = V4L2_COLORSPACE_SRGB;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}

	sfmt.pad = 1;
	sfmt.format.code = wpf_conf->out_code;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}

	memset(&fmt, 0, sizeof(struct v4l2_format));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = wpf_conf->width;
	fmt.fmt.pix_mp.height = wpf_conf->height;
	fmt.fmt.pix_mp.pixelformat = wpf_conf->out_format;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline = wpf_conf->bpitch0;
	fmt.fmt.pix_mp.plane_fmt[1].bytesperline = wpf_conf->bpitch1;
	fmt.fmt.pix_mp.num_planes = wpf_conf->planes;

	if (ioctl (entity->io_entity->fd, VIDIOC_S_FMT, &fmt)) {
		viper_log("%s: VIDIOC_S_FMT failed %d\n", __FUNCTION__);
		return -1;
	}
	return 0;
}


int configure_uds(struct viper_entity *entity, void *args)
{
	struct viper_uds_config *uds_conf = (struct viper_uds_config *)args;
	struct v4l2_subdev_format sfmt;
	struct v4l2_format fmt;

	memset(&sfmt, 0, sizeof(struct v4l2_subdev_format));
	sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sfmt.pad = 0;
	sfmt.format.width = uds_conf->in_width;
	sfmt.format.height = uds_conf->in_height;
	sfmt.format.code = uds_conf->code;
	sfmt.format.field = V4L2_FIELD_NONE;
	sfmt.format.colorspace = V4L2_COLORSPACE_SRGB;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}

	sfmt.pad = 1;
	sfmt.format.width = uds_conf->out_width;
	sfmt.format.height = uds_conf->out_height;
	if (ioctl (entity->fd, VIDIOC_SUBDEV_S_FMT, &sfmt)) {
		viper_log("%s: VIDIOC_SUBDEV_S_FMT failed %d\n", __FUNCTION__,
			sfmt.pad);
		return -1;
	}
	return 0;
}


