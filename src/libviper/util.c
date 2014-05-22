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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "log.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/v4l2-mediabus.h>
#include <errno.h>
#include "entity_config.h"
#include "viper_internal.h"
#include <uiomux/uiomux.h>

#define DEBUG

#ifdef DEBUG
void dump();
#endif

UIOMux *uiomux;
struct viper_context viper = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.ref_cnt = 0,
	.device_list = NULL,
};


static int fgets_with_openclose(char *fname, char *buf, size_t maxlen)
{
        FILE *fp;
        char *s;

        if ((fp = fopen(fname, "r")) != NULL) {
                s = fgets(buf, maxlen, fp);
                fclose(fp);
                return (s != NULL) ? strlen(buf) : 0;
        } else {
                return -1;
        }
}

const struct entity_capability entity_cap_list[] = {
	{
		.name = "rpf",
		.caps = VIPER_CAPS_INPUT,
		.config = configure_rpf,
	},
	{
		.name = "wpf",
		.caps = VIPER_CAPS_OUTPUT,
		.config = configure_wpf,
	},
	{
		.name = "uds",
		.caps = VIPER_CAPS_RESIZE,
		.config = configure_uds,
	},
	{
		.name = "bru",
		.caps = VIPER_CAPS_BLEND,
		.config = configure_bru,
	},
};

const struct entity_capability * lookup_entity_caps(const char *name)
{
	int i;
	int num_caps = sizeof(entity_cap_list)/sizeof(entity_cap_list[0]);	
	for (i = 0; i < num_caps; i++)	{
		if (!strncmp(name, entity_cap_list[i].name,
				    strlen(entity_cap_list[i].name)))
			return &entity_cap_list[i];
	}
	return NULL;
}

struct viper_io_entity * lookup_io_entity(struct viper_device *device,
					  const char * name) {
	struct viper_io_entity *io_entity = device->io_entity_list;
	while (io_entity) {
		if (!strcmp(io_entity->name, name))
			return io_entity;
		io_entity = io_entity->next;
	}
	return NULL;
}

void device_add_io_entity(struct viper_device *device,
		          const char *name,
		          int id) {
	struct viper_io_entity *entity;
	char devfile[255];
	char *ptr;
	entity = calloc(1, sizeof(struct viper_io_entity));
	snprintf(devfile, 255, "/dev/video%d", id);
	entity->fd = open(devfile, O_RDWR);
	if (entity->fd < 0)
		return;
	entity->name = strdup(name);
	if ((ptr = strchr(entity->name, '\n')))
		*ptr = '\0';
	entity->next = device->io_entity_list;
	device->io_entity_list = entity;
}

void device_add_entity(struct viper_device *device,
		       const struct entity_capability *entity_caps,
		       const char *name,
		       int id) {

	struct viper_entity *entity;
	struct viper_io_entity *ent;
	char devfile[255];
	char *ptr;
	entity = calloc(1, sizeof(struct viper_entity));

	snprintf(devfile, 255, "/dev/v4l-subdev%d", id);

	entity->fd = open(devfile, O_RDWR);

	if (entity->fd < 0)
		return;

	entity->caps = entity_caps;
	entity->name = strdup(name);
	if ((ptr = strchr(entity->name, '\n')))
		*ptr = '\0';
	pthread_mutex_init(&entity->lock, NULL);
	entity->io_entity = lookup_io_entity(device, entity->name);
	entity->next = device->entity_list;
	device->entity_list = entity;
	
}

struct viper_device * find_device(struct viper_context *viper,
				const char *name)
{
	struct viper_device *dev = viper->device_list;
	while (dev) {
		if (!strcmp(name, dev->name))
			return dev;
		dev = dev->next;
	}
	return NULL;

}

void register_entity(struct viper_context *viper,
			 char *device_str,
			 char *entity_str,
			 bool io_entity,
			 int id)
{
	int i;
	struct viper_device *device;
	const struct entity_capability *entity_caps;

	entity_caps = lookup_entity_caps(entity_str);
	
	if (!entity_caps)
		return;

	if (!(device = find_device(viper, device_str))) {
		char path[255];
		int media_fd = -1;
		struct stat st;

		for (i=255; i>=0; i--) {
                	sprintf (path, "/sys/devices/platform/%s/media%d",
								device_str, i);
	                if (0 == stat (path, &st)) {
        	                sprintf (path, "/dev/media%d", i);
	                        media_fd = open (path, O_RDWR);
				break;
        	        }
	        }
		
		if (media_fd < 0)
			return;

		device = calloc(1, sizeof(struct viper_device));
		strncpy(device->name, device_str, 255);
		device->media_fd = media_fd;
		device->next = viper->device_list;
		viper->device_list = device;
	}

	if (io_entity)
		device_add_io_entity(device, entity_str, id);
	else
		device_add_entity(device, entity_caps, entity_str, id);
	
}

int find_entities(struct viper_context *viper, const char *path_str, bool io_entity) {
	int i;
	char subdev_name[256];
	char path[256];

	for (i = 255; i >= 0; i--) {
		char *device = NULL;
		char *token = NULL;	
		snprintf(path, 255, path_str, i);
		fgets_with_openclose(path, subdev_name, 255);
		token = strtok(subdev_name, " ");
		do {
			if (!device) {
				device = token;
			} else {
				register_entity(viper, device, token,
						io_entity, i);
				break;
			}
		} while ((token = strtok(NULL, " ")));
	}
	return 0;
}

int enum_device_entities(struct viper_device *dev) {
	struct media_entity_desc media_ent;
	struct viper_entity *entity = dev->entity_list;
	int last_id = 0;
	memset(&media_ent, 0, sizeof(struct media_entity_desc));
	media_ent.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (!ioctl(dev->media_fd, MEDIA_IOC_ENUM_ENTITIES, &media_ent)) {
		last_id = media_ent.id;
		entity = dev->entity_list;
		while (entity) {
			char check_name[255];
			snprintf(check_name, 255, "%s %s", dev->name,
				entity->name);
		
			if (!strcmp(media_ent.name, check_name)) {
				entity->media_id = last_id;
				entity->pads = media_ent.pads;
				entity->links = media_ent.links;
				break;
			}
			entity = entity->next;
		}
		memset(&media_ent, 0, sizeof(struct media_entity_desc));
		media_ent.id = last_id | MEDIA_ENT_ID_FLAG_NEXT;
	}
	return 0;
}

int enum_media_entities(struct viper_context *viper)
{
	struct viper_device *dev = viper->device_list;
	while (dev) {
		if (enum_device_entities(dev))
			return -1;
		dev = dev->next;
	}
	return 0;
}
int init_context () {
	pthread_mutex_lock(&viper.lock);
	if (viper.ref_cnt++) {
		pthread_mutex_unlock(&viper.lock);
		return 0;
	}

	find_entities(&viper, "/sys/class/video4linux/video%d/name", true);
	find_entities(&viper, "/sys/class/video4linux/v4l-subdev%d/name", false);
	enum_media_entities(&viper);

	pthread_mutex_unlock(&viper.lock);
	return 0;
}

int deinit_context() {
	struct viper_entity *entity;
	struct viper_device *device;
	void *tmp;
	pthread_mutex_lock(&viper.lock);
	if (--viper.ref_cnt) {
		pthread_mutex_unlock(&viper.lock);
		return 0;
	}

	device = viper.device_list;
	while (device) {
		entity = device->entity_list;
		while (entity) {
			if (entity->io_entity)
				close(entity->io_entity->fd);
			if (entity->name)
				free(entity->name);
			close(entity->fd);
			tmp = entity;
			entity = entity->next;
			free(tmp);
		}
		close(device->media_fd);
		tmp = device;
		device = device->next;
		free(tmp);
	}
	viper.device_list = NULL;
	pthread_mutex_unlock(&viper.lock);
	return 0;
}
/*  ----------------------------------------------- */

static void entity_unlock(struct viper_entity *entity) {
	flock(entity->fd, LOCK_UN);
	pthread_mutex_unlock(&entity->lock);
}

static int try_entity_lock(struct viper_entity *entity) {
	if (pthread_mutex_trylock(&entity->lock))
		return -1;

	if (flock(entity->fd, LOCK_EX | LOCK_NB)) {
		pthread_mutex_unlock(&entity->lock);
		return -1;
	}
	return 0;
}


static int disable_links(struct viper_device *dev,
                  struct viper_entity *entity)
{
	int ret, i;
	struct media_links_enum links;

	memset(&links, 0, sizeof (struct media_links_enum));
	links.entity = entity->media_id;
	links.pads = NULL;
	links.links = calloc(entity->links, sizeof(struct media_link_desc));
	ret = ioctl(dev->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
	if (ret) {
		viper_log("enum links failed - %d\n", errno);
		goto done;
	}
	for (i = 0; i < entity->links; i++) {
		if (links.links[i].flags & MEDIA_LNK_FL_ENABLED) {
			if (links.links[i].flags & MEDIA_LNK_FL_IMMUTABLE)
				continue;
			links.links[i].flags &= ~MEDIA_LNK_FL_ENABLED;
			if(ioctl(dev->media_fd, MEDIA_IOC_SETUP_LINK,
					&links.links[i]))
				viper_log("diable link failed - %d\n", errno);
			
		}
	}
done:

	free(links.links);
	return ret;
}

static int enable_links(struct viper_device *dev,
		struct viper_pipeline *pipe,
		struct viper_entity *to,
		int num_suppipes)
{
	int ret =-2, i;
	struct viper_entity *from;
	struct media_links_enum links;
	int pipe_index;


	if (to->caps->caps & VIPER_CAPS_INPUT)
		pipe->active_subpipe++;

	if (pipe->active_subpipe < 1)
		return -1;

	if (to->caps->caps & VIPER_CAPS_BLEND)
		pipe_index = 0;
	else
		pipe_index = pipe->active_subpipe - 1;

	do {
		from = pipe->subpipe_final[pipe_index];
		if (!from) {
			ret = 0;
			goto no_link;
		}

		memset(&links, 0, sizeof (struct media_links_enum));
		links.entity = from->media_id;
		links.pads = NULL;
		links.links = calloc(from->links,
					sizeof(struct media_link_desc));

		ret = ioctl(dev->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
		if (ret) {
			viper_log("enum link failed - %d\n", errno);
			goto links_done;
		}

		for (i = 0; i < from->links; i++) {
			if (links.links[i].sink.entity == to->media_id) {
				if (to->caps->caps & VIPER_CAPS_BLEND &&
						links.links[i].sink.index !=
						 pipe_index)
					continue;
				struct media_link_desc *update_link;
				update_link = &links.links[i];
				update_link->flags |= MEDIA_LNK_FL_ENABLED;
				ret = ioctl(dev->media_fd,
					MEDIA_IOC_SETUP_LINK, update_link);
				if (!ret)
					break;
				else if (errno == EBUSY)
					continue;
				else
					return -1;
			}
		}
		free(links.links);
	} while ((to->caps->caps & VIPER_CAPS_BLEND) &&
				(++pipe_index < pipe->active_subpipe));

	if (to->caps->caps & VIPER_CAPS_BLEND)
		pipe->active_subpipe = 1;

links_done:
no_link:
	if (to->caps->caps & VIPER_CAPS_OUTPUT) {
		pipe->subpipe_final[pipe->active_subpipe - 1] = NULL;
		pipe->active_subpipe--;
	} else {
		pipe->subpipe_final[pipe->active_subpipe - 1] = to;
	}

	return ret;
}

static struct viper_entity * get_free_entity(struct viper_device *dev,
				    struct viper_pipeline *pipe,
		  		    int caps)
{
	struct viper_entity *entity;

	entity = dev->entity_list;
	while (entity) {
		if (entity->caps->caps & caps) {
			if (!try_entity_lock(entity)) {
				entity->next_locked = pipe->locked_entities;
				disable_links(dev, entity);
				pipe->locked_entities = entity;
				return entity;
			}
		}
		entity = entity->next;
	}
	return NULL;
}

void free_pipeline(struct viper_device *dev, struct viper_pipeline *pipe)
{
	struct viper_entity *entity = pipe->locked_entities;
	while (entity) {
		disable_links(dev, entity);
		entity = entity->next_locked;
	}
	entity = pipe->locked_entities;
	while (entity) {
		entity_unlock(entity);
		entity = entity->next_locked;
	}
	free(pipe);
	
}

struct viper_pipeline * create_pipeline(struct viper_device *dev,
		int *caps_list, void **args_list, int length) {
#if 0
		struct viper_pipeline *pipeline, int length,
		int *in_fd, int *out_fd) {
#endif
	int i;
	struct viper_entity *entity;
	struct viper_pipeline *pipe;
	
	/* need to seach for appropriate device */
	pipe = calloc(1, sizeof(struct viper_pipeline));

	for (i = 0; i < length; i++) {
		entity = get_free_entity(dev, pipe, caps_list[i]);
		if (!entity) {
			viper_log("%s: No free enities for cap type %d",
				__FUNCTION__, caps_list[i]);
			goto error_out;
		}
		if (entity->caps->config(entity, args_list[i])) {
			viper_log("%s: entity config error - %s",
				__FUNCTION__, entity->name);
			goto error_out;
		}
		if (caps_list[i] & VIPER_CAPS_INPUT)
			pipe->input_fds[pipe->num_inputs++] = 
				entity->io_entity->fd;
		if (caps_list[i] & VIPER_CAPS_OUTPUT)
			pipe->output_fds[pipe->num_outputs++] = 
				entity->io_entity->fd;
		if (enable_links(dev, pipe, entity, 1)) {
			viper_log("%s: Entity link failed. caps=%d",
				__FUNCTION__, caps_list[i]);
			goto error_out;
		}
	}

	return pipe;

error_out:
#ifdef DEBUG
			dump();
#endif
	free_pipeline(dev, pipe);
	return NULL;
}

int stop_io_device(int fd, bool input) {
	struct v4l2_requestbuffers reqbuf;
	enum v4l2_buf_type buftype;
	if (input)
		buftype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ioctl(fd, VIDIOC_STREAMOFF, &buftype);

	reqbuf.count = 0;
	reqbuf.type = buftype;
	reqbuf.memory = V4L2_MEMORY_USERPTR;
	if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
		printf("reqbufs 0 failed for %s on %d - %d\n",
			input ? "input" : "output", fd, errno);
		return -1;
	}
	return 0;
}

int start_io_device(int fd, bool input) {
	struct v4l2_requestbuffers reqbuf;
	enum v4l2_buf_type buftype;
	memset(&reqbuf, 0, sizeof(reqbuf));

	if (input)
		buftype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	reqbuf.count = 1;
	reqbuf.type = buftype;
	reqbuf.memory = V4L2_MEMORY_USERPTR;


	if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
		viper_log("reqbufs failed for %s stream on %d - %d\n",
			input ? "input" : "output", fd, errno);
		return -1;
	}

	if(ioctl(fd, VIDIOC_STREAMON, &buftype)) {
		viper_log("stream on failed for %s stream on %d - %d\n",
			input ? "input" : "output", fd, errno);
		stop_io_device(fd, input);
		return -1;
	}
	return 0;
}

int dequeue_buffer(int fd, bool input)
{
	struct v4l2_buffer buf;
	enum v4l2_buf_type buftype;

	if (input)
		buftype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	memset(&buf, 0, sizeof(buf));
	buf.type = buftype;
	buf.memory = V4L2_MEMORY_USERPTR;
	return ioctl(fd, VIDIOC_DQBUF, &buf);
}

int queue_buffer(int fd, void **buffer, int *size, int count, bool input)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	enum v4l2_buf_type buftype;
	int ret, i;

	if (input)
		buftype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	planes = calloc(count, sizeof(struct v4l2_plane));

	memset(&buf, 0, sizeof(buf));
	buf.type = buftype;
	buf.index = 0;
	buf.field = V4L2_FIELD_NONE;
	buf.memory = V4L2_MEMORY_USERPTR;
	buf.m.planes = planes;
	buf.length = count;


	for (i = 0; i < count; i++) {
		planes[i].length = size[i];
		planes[i].m.userptr = (unsigned long) buffer[i];
		if (input)
			planes[i].bytesused = size[i];
	}

	ret =  ioctl(fd, VIDIOC_QBUF, &buf);
	free(planes);
	return ret;
}
#if 0
int resize_pipeline(struct viper_context *viper) {
	struct viper_device *dev = viper->device_list;

	struct viper_rpf_config rpf_set;
	struct viper_wpf_config wpf_set;
	struct viper_uds_config uds_set;
	struct viper_pipeline *pipeline;
	
	enum v4l2_buf_type buftype;
	void *tmpbuf;
	int caps[3];
	void *args[3];

	int code = color_fmt_to_code(V4L2_PIX_FMT_RGB24);

	rpf_set.width = uds_set.in_width = 100;
	rpf_set.height = uds_set.in_height = 100;

	wpf_set.width = uds_set.out_width = 100;
	wpf_set.height = uds_set.out_height = 100;

	rpf_set.code = wpf_set.code = uds_set.code = code;
	rpf_set.format = wpf_set.format = V4L2_PIX_FMT_RGB24;
	
	caps[0] = VIPER_CAPS_INPUT;
	caps[1] = VIPER_CAPS_RESIZE;
	caps[2] = VIPER_CAPS_OUTPUT;

	args[0] = &rpf_set;
	args[1] = &uds_set;
	args[2] = &wpf_set;

	pipeline = create_pipeline(dev, caps, args, 3);

	start_io_device(pipeline->input_fds[0], true);
	start_io_device(pipeline->output_fds[0], false);

	tmpbuf = uiomux_malloc(uiomux, 1, 1024*1024, 1);
/*	queue_buffer(pipeline->input_fds[0], tmpbuf, 40000, true);
	queue_buffer(pipeline->input_fds[1], tmpbuf + 40000, 40000, false);*/

	printf("All good\n");
	return 0;
}
#endif
const char *names[] = {
	"VPU",
	NULL,
};

#ifdef DEBUG
void dump_entity_and_links(struct viper_device *dev,
			   int media_id, char *out) {
	int i;
	struct media_links_enum links;
	struct media_entity_desc media_ent;
	char add[254];
	memset(&media_ent, 0, sizeof(struct media_entity_desc));
	media_ent.id = media_id;
	if (ioctl(dev->media_fd, MEDIA_IOC_ENUM_ENTITIES, &media_ent)) {
		viper_log("error\n");
		return;
	}

	snprintf(add, 254, "%s -> ", media_ent.name);
	strncat(out, add, 254);

	memset(&links, 0, sizeof (struct media_links_enum));
	links.entity = media_id;
	links.pads = NULL;
	links.links = calloc(media_ent.links,
			sizeof(struct media_link_desc));
	ioctl(dev->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
	for (i = 0; i < media_ent.links; i ++) {
		if (links.links[i].flags & MEDIA_LNK_FL_ENABLED) {
			dump_entity_and_links(dev,
					links.links[i].sink.entity, out);
		}
			
	}
}

void dump_links(struct viper_device *dev,
		struct viper_io_entity *io_entity) {
	struct media_entity_desc media_ent;
	char test[255];
	char out[255];
	out[0] = '\0';

	memset(&media_ent, 0, sizeof(struct media_entity_desc));
	media_ent.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (!ioctl(dev->media_fd, MEDIA_IOC_ENUM_ENTITIES, &media_ent)) {
		snprintf(test, 255, "%s %s input", dev->name, io_entity->name);
		if (!strcmp(test, media_ent.name)) {
			dump_entity_and_links(dev, media_ent.id, out);
			viper_log("%s \n", out);
			return;
		}
		media_ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}
	viper_log("Cannot find media id for %s\n", io_entity->name);
}

void dump()
{
	struct viper_io_entity *io_entity;
	io_entity = viper.device_list->io_entity_list;
	while(io_entity) {
		dump_links(viper.device_list, io_entity);
		io_entity = io_entity->next;
	}
}
#endif
#if 0
#include <shvio/shvio.h>

main () {
	struct viper_io_entity *io_entity;
	SHVIO *shvio;
	struct ren_vid_surface src, dst;
	char *tmpbuf;
	uiomux = uiomux_open_named(names);
#if 0
	memset(&viper, 0, sizeof(struct viper_context));
	init_context();
	resize_pipeline(&viper);
#else
	shvio = shvio_open();
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	tmpbuf = uiomux_malloc(uiomux, 1, 1024*1024, 1);
	src.format = REN_BGR32;
	src.w = 100;
	src.h = 100;
	src.pitch = 100;
	src.py = tmpbuf;

	dst.format = REN_BGR32;
	dst.w = 100;
	dst.h = 100;
	dst.pitch = 100;
	dst.py = tmpbuf + 40000;

	shvio_setup(shvio, &src, &dst, 0);
	shvio_start(shvio);
	shvio_wait(shvio);
#endif 
#ifdef DEBUG
	io_entity = viper.device_list->io_entity_list;	
	while(io_entity) {
		dump_links(viper.device_list, io_entity);
		io_entity = io_entity->next;
	}
		
	
#endif

	
}

#endif

