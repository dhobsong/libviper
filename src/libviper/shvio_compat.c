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

#include "entity_config.h"
#include <errno.h>
#include "log.h"
#include <poll.h>
#include <shvio/shvio.h>
#include <string.h>
#include "viper_internal.h"

struct vio_format {
	ren_vid_format_t format;
	uint32_t v4l_format;
	int v4l_planes;
};

struct SHVIO {
	struct viper_device *device;
	struct viper_pipeline *pipeline;
/* for bundle mode */
	struct viper_rpf_config rpf_set;
	struct viper_wpf_config wpf_set;
	struct viper_uds_config uds_set;
	int bundle_lines_remaining;
	int bundle_lines;
	int output_y_offset;
	int output_c_offset;
};

extern struct viper_context viper;

static int is_resize(const struct ren_vid_surface *src_surface,
		     const struct ren_vid_surface *dst_surface)
{
	return ((src_surface->w != dst_surface->w) ||
		(src_surface->h != dst_surface->h));
}

static int is_resize_rect(const struct ren_vid_surface *src)
{
	return ((src->blend_out.w && (src->w != src->blend_out.w)) ||
		(src->blend_out.h && (src->h != src->blend_out.h)));
}

static const struct vio_format format_table[] = {
        { REN_NV12, V4L2_PIX_FMT_NV12M, 2 },
        { REN_NV16, V4L2_PIX_FMT_NV16M, 2 },
/* TODO: Add support for these formats */
/*      REN_YV12
        REN_NV16,
        REN_YV16
        REN_UYVY */
        { REN_XRGB1555, V4L2_PIX_FMT_RGB555, 1 },
        { REN_RGB565, V4L2_PIX_FMT_RGB565, 1 },
        { REN_RGB24, V4L2_PIX_FMT_RGB24, 1 },
        { REN_BGR24, V4L2_PIX_FMT_BGR24, 1 },
        { REN_RGB32, V4L2_PIX_FMT_RGB32, 1 },
        { REN_BGR32, V4L2_PIX_FMT_RGB332, 1 },
        { REN_XRGB32, V4L2_PIX_FMT_BGR32, 1 },
        { REN_BGRA32, V4L2_PIX_FMT_RGB332, 1 },
        { REN_ARGB32, V4L2_PIX_FMT_BGR32, 1 },
};

static const struct vio_format * lookup_vio_color(ren_vid_format_t vio_color) {
	unsigned int i;
	for (i = 0; i < sizeof(format_table)/sizeof(format_table[0]); i++) {
		if (format_table[i].format == vio_color)
			return &format_table[i];
	}
	return NULL;
}

static const struct vio_format * lookup_v4l_color(uint32_t v4l_color) {
	unsigned int i;
	for (i = 0; i < sizeof(format_table)/sizeof(format_table[0]); i++) {
		if (format_table[i].v4l_format == v4l_color)
			return &format_table[i];
	}
	return NULL;
}

SHVIO *shvio_open_named(const char *name) {
	init_context();
	struct viper_device *device = viper.device_list;
	struct SHVIO *vio;


	if (!device) {
		viper_log("no device");
		return NULL;
	}

	if (!name) {
		vio = calloc(1, sizeof(struct SHVIO));
		vio->device = device;
		return vio;
	}

	while (device) {
		if (!strncasecmp(name, device->name, strlen(name))) {
			vio = calloc(1, sizeof(struct SHVIO));
			vio->device = device;
			return vio;
		}
		device = device->next;
	}
	return NULL;
}

SHVIO *shvio_open(void) {
	return shvio_open_named(NULL);
}

void shvio_close(SHVIO *vio) {
}

void
shvio_set_color_conversion(
        SHVIO *vio,
        int bt709,
        int full_range) { }

void
shvio_set_src(
	SHVIO *vio,
	void *src_py,
	void *src_pc)
{
	struct viper_pipeline *pipe = vio->pipeline;
	if (!pipe)
		return;
	pipe->input_addr[0][0] = src_py;
	pipe->input_addr[0][1] = src_pc;
}

void
shvio_set_dst(
	SHVIO *vio,
	void *dst_py,
	void *dst_pc)
{
	struct viper_pipeline *pipe = vio->pipeline;
	if (!pipe)
		return;
	pipe->output_addr[0][0] = dst_py;
	pipe->output_addr[0][1] = dst_pc;
}

static int setup_rpf(struct viper_rpf_config *rpf_set,
		      const struct ren_vid_surface *surface,
		      ren_vid_format_t vio_color)
{
	const struct vio_format *fmt, *out_fmt;
	fmt = lookup_vio_color(surface->format);
	if (!fmt)
		return -1;
	if (!vio_color) {
		out_fmt = fmt;
	} else {
		 out_fmt = lookup_vio_color(vio_color);
		if (!out_fmt)
			return -1;
	}
	rpf_set->width = surface->w;
	rpf_set->height = surface->h;
	rpf_set->bpitch0 = size_y(fmt->format, surface->pitch,
		surface->bpitchy);
	rpf_set->bpitch1 = size_y(fmt->format, surface->pitch,
		surface->bpitchc);
	rpf_set->planes = fmt->v4l_planes;
	rpf_set->in_format = fmt->v4l_format;
	rpf_set->in_code = color_fmt_to_code(fmt->v4l_format);
	rpf_set->out_format = out_fmt->v4l_format;
	rpf_set->out_code = color_fmt_to_code(out_fmt->v4l_format);
	return fmt->v4l_planes;
}

static int setup_wpf(struct viper_wpf_config *wpf_set,
		     const struct ren_vid_surface *surface,
		     ren_vid_format_t vio_color)
{
	const struct vio_format *in_fmt, *out_fmt;
	in_fmt = lookup_vio_color(vio_color);
	if (!in_fmt)
		return -1;
	out_fmt = lookup_vio_color(surface->format);
	if (!out_fmt)
		return -1;
	wpf_set->width = surface->w;
	wpf_set->height = surface->h;
	wpf_set->bpitch0 = size_y(out_fmt->format, surface->pitch,
		surface->bpitchy);
	wpf_set->bpitch1 = size_y(out_fmt->format, surface->pitch,
		surface->bpitchc);
	wpf_set->planes = out_fmt->v4l_planes;
	wpf_set->in_format = in_fmt->v4l_format;
	wpf_set->in_code = color_fmt_to_code(in_fmt->v4l_format);
	wpf_set->out_format = out_fmt->v4l_format;
	wpf_set->out_code = color_fmt_to_code(out_fmt->v4l_format);
	return out_fmt->v4l_planes;
}


int shvio_setup(SHVIO *vio,
	const struct ren_vid_surface *src_surface,
        const struct ren_vid_surface *dst_surface,
        shvio_rotation_t rotate) {
	struct viper_pipeline *pipeline;
	int caps[3];
	void *args[3];
	int num_ents = 0;
	const struct vio_format *fmt;
	int input_planes, output_planes;

	struct viper_device *device = vio->device;

	input_planes = setup_rpf(&vio->rpf_set, src_surface,
		src_surface->format);

	caps[num_ents] = VIPER_CAPS_INPUT;
	args[num_ents] = &vio->rpf_set;
	num_ents++;
	output_planes = setup_wpf(&vio->wpf_set, dst_surface,
		src_surface->format);

	if (is_resize(src_surface, dst_surface)) {
		vio->uds_set.in_width = vio->rpf_set.width;
		vio->uds_set.in_height = vio->rpf_set.height;
		vio->uds_set.out_width = vio->wpf_set.width;
		vio->uds_set.out_height = vio->wpf_set.height;
		vio->uds_set.code = vio->rpf_set.out_code;
		caps[num_ents] = VIPER_CAPS_RESIZE;
		args[num_ents] = &vio->uds_set;
		num_ents++;
	}
	caps[num_ents] = VIPER_CAPS_OUTPUT;
	args[num_ents] = &vio->wpf_set;
	num_ents++;

	pipeline = create_pipeline(device, caps, args, num_ents);

	if (!pipeline) {
		viper_log("%s: pipeline config failed\n", __FUNCTION__);
		return -1;
	}

	pipeline->input_planes[0] = input_planes;

	if (start_io_device(pipeline->input_fds[0], true)) {
		viper_log("%s: cannot start input device\n", __FUNCTION__);
		goto err_out;
	}
	pipeline->input_addr[0][0] = src_surface->py;
	pipeline->input_size[0][0] = src_surface->h *
		size_y(src_surface->format, src_surface->pitch, 0);

	if (input_planes > 1) {
		pipeline->input_addr[0][1] = src_surface->pc;
		pipeline->input_size[0][1] = src_surface->h *
			size_c(src_surface->format, src_surface->pitch, 0);
	}

	pipeline->output_planes[0] = output_planes;

	if (start_io_device(pipeline->output_fds[0], false)) {
		viper_log("%s: cannot start output device\n", __FUNCTION__);
		goto err_out;
	}

	pipeline->output_addr[0][0] = dst_surface->py;
	pipeline->output_size[0][0] = dst_surface->h *
		size_y(dst_surface->format, dst_surface->pitch, 0);

	if (output_planes > 1) {
		pipeline->input_addr[0][1] = dst_surface->pc;
		pipeline->input_size[0][1] = dst_surface->h *
			size_c(dst_surface->format, dst_surface->pitch, 0);
	}
	vio->bundle_lines_remaining = src_surface->h;
	vio->bundle_lines = vio->bundle_lines_remaining;
	vio->pipeline = pipeline;
	return 0;

err_out:
	free_pipeline(device, pipeline);
	return -1;
}
int
shvio_setup_blend(
	SHVIO *vio,
	const struct ren_vid_rect *virt,
	const struct ren_vid_surface *const *src_list,
	int src_count,
	const struct ren_vid_surface *dst)
{

	int i, ret = 0;
	struct viper_rpf_config *rpf_set;
	struct viper_bru_config *bru_set;
	struct viper_uds_config *uds_set;
	struct viper_wpf_config wpf_set;
	int *input_planes, *caps;
	void **args;
	int output_planes;
	int num_ents = 0;
	struct viper_pipeline *pipeline;
	struct viper_device *device = vio->device;

	bru_set = calloc(1, sizeof (struct viper_bru_config));
	input_planes = calloc(src_count, sizeof (int));
	caps = calloc(src_count * 2 + 2, sizeof (int));
	args = calloc(src_count * 2 + 2, sizeof (void *));
	for (i = 0; i < src_count; i++) {
		rpf_set = calloc(1, sizeof (struct viper_rpf_config));
		input_planes[i] = setup_rpf(rpf_set, src_list[i], dst->format);		
		caps[num_ents] = VIPER_CAPS_INPUT;
		args[num_ents] = rpf_set;
		bru_set->in_lefts[i] = src_list[i]->blend_out.x;
		bru_set->in_tops[i] = src_list[i]->blend_out.y;
		bru_set->in_widths[i] = src_list[i]->w;
		bru_set->in_heights[i] = src_list[i]->h;
		num_ents++;
		
		if (is_resize_rect(src_list[i])) {
			uds_set = calloc(1, sizeof (struct viper_bru_config));
			uds_set->in_width = src_list[i]->w;
			uds_set->in_height = src_list[i]->h;
			uds_set->out_width = src_list[i]->blend_out.w;
			uds_set->out_height = src_list[i]->blend_out.h;
			uds_set->code = rpf_set->out_code;
			bru_set->in_widths[i] = src_list[i]->blend_out.w;
			bru_set->in_heights[i] = src_list[i]->blend_out.h;
			caps[num_ents] = VIPER_CAPS_RESIZE;
			args[num_ents] = uds_set;
			num_ents++;
		}
	}
	bru_set->inputs = src_count;
	bru_set->out_width = virt ? virt->w : dst->w;
	bru_set->out_height = virt ? virt->h : dst->h;
	caps[num_ents] = VIPER_CAPS_BLEND;
	args[num_ents] = bru_set;
	num_ents++;
	
	output_planes = setup_wpf(&wpf_set, dst, dst->format);
	wpf_set.width = bru_set->out_width;
	wpf_set.height = bru_set->out_height;
	wpf_set.bpitch0 = size_y(dst->format, dst->pitch, dst->bpitchy);
	if (virt)
		wpf_set.bpitch0 = size_y(dst->format, virt->w, 0);
	wpf_set.bpitch1 = wpf_set.bpitch0;
	caps[num_ents] = VIPER_CAPS_OUTPUT;
	args[num_ents] = &wpf_set;
	num_ents++;

	bru_set->code = wpf_set.in_code;

	pipeline = create_pipeline(device, caps, args, num_ents);

	if (!pipeline) {
		viper_log("%s: pipeline config failed\n", __FUNCTION__);
		ret = -1;
		goto end;
	}

	memcpy(pipeline->input_planes, input_planes, src_count * sizeof(int));
	for (i = 0; i < src_count; i++) {
		if (start_io_device(pipeline->input_fds[i], true)) {
			viper_log("%s: cannot start input device\n",
							__FUNCTION__);
			ret = -1;
			free_pipeline(device, pipeline);
			goto end;
		}
		pipeline->input_addr[i][0] = src_list[i]->py;
		pipeline->input_size[i][0] = src_list[i]->h *
			size_y(src_list[i]->format, src_list[i]->pitch, 0);

		if (input_planes > 1) {
			pipeline->input_addr[i][1] = src_list[i]->pc;
			pipeline->input_size[i][1] = src_list[i]->h *
			    size_c(src_list[i]->format, src_list[i]->pitch, 0);
		}
	}

	pipeline->output_planes[0] = output_planes;

	if (start_io_device(pipeline->output_fds[0], false)) {
		viper_log("%s: cannot start output device\n", __FUNCTION__);
		free_pipeline(device, pipeline);
		ret = -1;
		goto end;
	}

	pipeline->output_addr[0][0] = dst->py;
	pipeline->output_size[0][0] = wpf_set.height * wpf_set.bpitch0;

	if (output_planes > 1) {
		pipeline->input_addr[0][1] = dst->pc;
		pipeline->input_size[0][1] = wpf_set.height * wpf_set.bpitch1;
	}
	
	vio->pipeline = pipeline;
end:
	for (i = 0; i < num_ents - 1; i++)
		free(args[i]);
	free(args);
	free(caps);
	free(input_planes);
	return ret;
}

int shvio_rotate(SHVIO *vio,
	const struct ren_vid_surface *src_surface,
        const struct ren_vid_surface *dst_surface,
        shvio_rotation_t rotate) {
	shvio_setup(vio,src_surface,dst_surface,rotate);
	shvio_start(vio);
	shvio_wait(vio);
	return 0;
}

int shvio_resize(SHVIO *vio,
	const struct ren_vid_surface *src_surface,
        const struct ren_vid_surface *dst_surface) {
	shvio_setup(vio,src_surface,dst_surface,0);
	shvio_start(vio);
	shvio_wait(vio);
	return 0;
}

void shvio_start(SHVIO *vio)
{
	struct viper_pipeline *pipe = vio->pipeline;
	int i;
	int ret;

	if (!pipe)
		return;

	ret = 0;
	for (i = 0; i < pipe->num_inputs; i++) {
		ret |= queue_buffer(pipe->input_fds[i], pipe->input_addr[i],
			pipe->input_size[i], pipe->input_planes[i], true);
		if (ret)
			viper_log("%s: queue input buffer fail. %d\n",
				__FUNCTION__, errno);
	}

	ret = 0;
	for (i = 0; i < pipe->num_outputs; i++) {
		ret |= queue_buffer(pipe->output_fds[i], pipe->output_addr[i],
			pipe->output_size[i], pipe->input_planes[i], false);
		if (ret)
			viper_log("%s: queue output buffer fail. %d\n",
				__FUNCTION__, errno);
	}
}

static int reconfig_pipeline(struct viper_pipeline *pipe,
			     unsigned int *caps,
			     void **args,
			     int count)
{
	struct viper_entity *entity;
	int i;
	for (i = 0; i < count; i++) {
		entity = pipe->locked_entities;
		while(entity) {
			if (entity->caps->caps == caps[i])
				entity->caps->config(entity, args[i]);

			entity = entity->next_locked;
		}
	}
	return 0;
}

void shvio_start_bundle(SHVIO *vio, int bundle_lines)
{
	struct viper_pipeline *pipe = vio->pipeline;
	if (!pipe)
		return;

	if (bundle_lines > vio->bundle_lines_remaining)
		bundle_lines = vio->bundle_lines_remaining;

	if (bundle_lines != vio->bundle_lines) {
		int caps[3];
		void *args[3];
		int pipe_count = 0;
		int wpf_lines = bundle_lines * vio->wpf_set.height / vio->rpf_set.height;
		const struct vio_format *fmt;
		vio->rpf_set.height = bundle_lines;
		vio->wpf_set.height = wpf_lines;
		caps[pipe_count] = VIPER_CAPS_INPUT;
		args[pipe_count] = &vio->rpf_set;
		pipe_count++;
		if (wpf_lines != bundle_lines) {
			vio->uds_set.in_height = bundle_lines;
			vio->uds_set.out_height = wpf_lines;
			caps[pipe_count] = VIPER_CAPS_RESIZE;
			args[pipe_count] = &vio->uds_set;
			pipe_count++;
		}
		caps[pipe_count] = VIPER_CAPS_OUTPUT;
		args[pipe_count] = &vio->wpf_set;
		pipe_count++;
		vio->bundle_lines = bundle_lines;

		stop_io_device(pipe->input_fds[0], true);
		stop_io_device(pipe->output_fds[0], false);
		reconfig_pipeline(pipe, caps, args, pipe_count);
		start_io_device(pipe->input_fds[0], true);
		start_io_device(pipe->output_fds[0], false);

		vio->output_y_offset = vio->wpf_set.bpitch0 * wpf_lines;
		vio->output_c_offset = vio->wpf_set.bpitch1 * wpf_lines;
	}

	if (queue_buffer(pipe->input_fds[0], pipe->input_addr[0],
			pipe->input_size[0], pipe->input_planes[0], true))
		viper_log("%s: queue input buffer fail. %d\n", __FUNCTION__,
			errno);

	if (queue_buffer(pipe->output_fds[0], pipe->output_addr[0],
		pipe->output_size[0], pipe->input_planes[0], false))
		viper_log("%s: queue output buffer fail. %d\n", __FUNCTION__,
									errno);

	pipe->output_addr[0][0] += vio->output_y_offset;
	pipe->output_addr[0][1] += vio->output_c_offset;
}

int shvio_wait(SHVIO *vio)
{
	struct viper_pipeline *pipe = vio->pipeline;
	int i;
	int ret = 0;

	if (!pipe)
		return -1;

	vio->bundle_lines_remaining -= vio->bundle_lines;
	if (vio->bundle_lines_remaining <= 0) {
		for (i = 0; i < pipe->num_inputs; i++)
			stop_io_device(pipe->input_fds[i], true);

		for (i = 0; i < pipe->num_outputs; i++)
			stop_io_device(pipe->output_fds[i], false);

		free_pipeline(vio->device, pipe);
	} else {
		for (i = 0; i < pipe->num_inputs; i++)
			dequeue_buffer(pipe->input_fds[i], true);

		for (i = 0; i < pipe->num_outputs; i++)
			dequeue_buffer(pipe->output_fds[i], false);
	}

	return ret;
}
