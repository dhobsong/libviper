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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

UIOMux *uiomux;

static enum v4l2_mbus_pixelcode color_fmt_to_code(uint32_t format) {
	switch (format) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_UYVY:
		return V4L2_MBUS_FMT_AYUV8_1X32;
	default:
		return V4L2_MBUS_FMT_ARGB8888_1X32;
	}
}

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
		.caps = VIP_CAPS_INPUT,
		.config = configure_rpf,
	},
	{
		.name = "wpf",
		.caps = VIP_CAPS_OUTPUT,
		.config = configure_wpf,
	},
	{
		.name = "uds",
		.caps = VIP_CAPS_RESIZE,
		.config = configure_uds,
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
				register_entity(viper, device, token, io_entity, i);
				break;
			}
		} while (token = strtok(NULL, " "));
	}
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
}

int enum_media_entities(struct viper_context *viper)
{
	struct viper_device *dev = viper->device_list;
	while (dev) {
		if (enum_device_entities(dev))
			return -1;
		dev = dev->next;
	}
}
int init_context (struct viper_context *viper) {
	find_entities(viper, "/sys/class/video4linux/video%d/name", true);
	find_entities(viper, "/sys/class/video4linux/v4l-subdev%d/name", false);
	enum_media_entities(viper);
	return 0;
}
/*  ----------------------------------------------- */

static void entity_unlock(struct viper_entity *entity) {
	pthread_mutex_unlock(&entity->lock);
	flock(entity->fd, LOCK_UN);
}

static int try_entity_lock(struct viper_entity *entity) {
	if (!flock(entity->fd, LOCK_EX | LOCK_NB)) {
		if (!pthread_mutex_trylock(&entity->lock))
			return 0;
		else
			flock(entity->fd, LOCK_UN);
	}
	return -1;
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
	if (ret)
		goto done;
	for (i = 0; i < entity->links; i++) {
		if (links.links[i].flags & MEDIA_LNK_FL_ENABLED) {
			if (links.links[i].flags & MEDIA_LNK_FL_IMMUTABLE)
				continue;
			links.links[i].flags &= ~MEDIA_LNK_FL_ENABLED;
			if(ioctl(dev->media_fd, MEDIA_IOC_SETUP_LINK,
					&links.links[i]))
				printf("diable link failed - %d\n", errno);
			
		}
	}
done:

	free(links.links);
	return ret;
}

static int enable_link(struct viper_device *dev,
		struct viper_entity *from,
		struct viper_entity *to)
{
	int ret, i;
	struct media_links_enum links;

	memset(&links, 0, sizeof (struct media_links_enum));
	links.entity = from->media_id;
	links.pads = NULL;
	links.links = calloc(from->links, sizeof(struct media_link_desc));

	ret = ioctl(dev->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
	if (ret)
		goto done;

	for (i = 0; i < from->links; i++) {
		if (links.links[i].sink.entity == to->media_id) {
			struct media_link_desc *update_link;
			update_link = &links.links[i];
			update_link->flags |= MEDIA_LNK_FL_ENABLED;
			ret = ioctl(dev->media_fd, MEDIA_IOC_SETUP_LINK,
				update_link);
			if (!ret)
				break;
			else if (errno == EBUSY)
				continue;
			else
				goto done;
		}
	}

done:

	free(links.links);
	return ret;
}

static struct viper_entity * get_free_entity(struct viper_context *viper,
 		   	            struct viper_device *device,
		  		    int caps)
{
	struct viper_device *dev = viper->device_list;
	struct viper_entity *entity;
	while (dev) {
		if (!device) {
			/* need to seach for appropriate device */
			break;
		}
		if (device && dev == device)
			break;
		dev = dev->next;
	}

	entity = dev->entity_list;
	while (entity) {
		if (entity->caps->caps & caps) {
			if (!try_entity_lock(entity)) {
				entity->next_locked = viper->locked_entities;
				viper->locked_entities = entity;
				disable_links(dev, entity);
				return entity;
			}
		}
		entity = entity->next;
	}
	return NULL;
}

void free_pipeline(struct viper_context *viper)
{
	struct viper_entity *entity = viper->locked_entities;
	while (entity) {
		entity_unlock(entity);
		entity = entity->next;
	}
	
} 


int resize_pipeline(struct viper_context *viper) {
	struct viper_device *dev = viper->device_list;
	struct viper_entity *rpf_entity;
	struct viper_entity *uds_entity;
	struct viper_entity *wpf_entity;

	struct viper_rpf_config rpf_set;
	struct viper_wpf_config wpf_set;
	struct viper_uds_config uds_set;
	
	enum v4l2_buf_type buftype;
	void *tmpbuf;

	int code = color_fmt_to_code(V4L2_PIX_FMT_RGB24);

	rpf_set.width = uds_set.in_width = 100;
	rpf_set.height = uds_set.in_height = 100;

	wpf_set.width = uds_set.out_width = 200;
	wpf_set.height = uds_set.out_height = 200;

	rpf_set.code = wpf_set.code = uds_set.code = code;
	rpf_set.format = wpf_set.format = V4L2_PIX_FMT_RGB24;


	rpf_entity = get_free_entity(viper, NULL, VIP_CAPS_INPUT);
	if (!rpf_entity || rpf_entity->caps->config(rpf_entity, &rpf_set))
		goto error_out;

	uds_entity = get_free_entity(viper, NULL, VIP_CAPS_RESIZE);
	if (!uds_entity || uds_entity->caps->config(uds_entity, &uds_set))
		goto error_out;

	if (enable_link(dev, rpf_entity, uds_entity))
		goto error_out;

	wpf_entity = get_free_entity(viper, NULL, VIP_CAPS_OUTPUT);
	if (!wpf_entity || wpf_entity->caps->config(wpf_entity, &wpf_set))
		goto error_out;

	if (enable_link(dev, uds_entity, wpf_entity))
		goto error_out;

	tmpbuf = uiomux_malloc(uiomux, 1, 1024*1024, 1);

	buftype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	{
		struct v4l2_requestbuffers reqbuf;
		memset(&reqbuf, 0, sizeof(reqbuf));
		reqbuf.count = 1;
		reqbuf.type = buftype;
		reqbuf.memory = V4L2_MEMORY_USERPTR;

		if(ioctl(rpf_entity->io_entity->fd, VIDIOC_REQBUFS, &reqbuf))
			return -1;
	}
	{
		struct v4l2_buffer buf;
		struct v4l2_plane plane;
		memset(&buf, 0, sizeof(buf));
		memset(&plane, 0, sizeof(plane));
		buf.type = buftype;
		buf.index = 0;
		buf.bytesused = 40000;
		buf.field = V4L2_FIELD_NONE;
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.m.planes = &plane;
		buf.length = 1;

		plane.bytesused = 40000;
		plane.length = 40000;
		plane.m.userptr = tmpbuf;

		if(ioctl(rpf_entity->io_entity->fd, VIDIOC_QBUF, &buf))
			return -1;
	}
	
	if (ioctl(rpf_entity->io_entity->fd, VIDIOC_STREAMON, &buftype)) {
		printf("############rpf Stream on failed %d\n", errno);
		return -1;
	}
	
	buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	{
		struct v4l2_requestbuffers reqbuf;
		memset(&reqbuf, 0, sizeof(reqbuf));
		reqbuf.count = 1;
		reqbuf.type = buftype;
		reqbuf.memory = V4L2_MEMORY_USERPTR;

		if(ioctl(wpf_entity->io_entity->fd, VIDIOC_REQBUFS, &reqbuf))
			return -1;
	}
	{
		struct v4l2_buffer buf;
		struct v4l2_plane plane;
		memset(&plane, 0, sizeof(plane));
		memset(&buf, 0, sizeof(buf));
		buf.type = buftype;
		buf.index = 0;
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.field = V4L2_FIELD_NONE;
		buf.m.planes = &plane;
		buf.length = 1;

		plane.length = 160000;
		plane.m.userptr = tmpbuf + 50000;
		if(ioctl(wpf_entity->io_entity->fd, VIDIOC_QBUF, &buf))
			return -1;
	}
	
	if(ioctl(wpf_entity->io_entity->fd, VIDIOC_STREAMON, &buftype)) {
		printf("############wpf Stream on failed %d\n", errno);
		return -1;
	}
	
	printf("All good\n");
	return 0;

error_out:
	free_pipeline(viper);
	return -1;
}

const char *names[] = {
	"VPU",
	NULL,
};

#ifdef DEBUG
void dump_entity_and_links(struct viper_device *dev,
			   int media_id) {
	int i;
	struct media_links_enum links;
	struct media_entity_desc media_ent;
	memset(&media_ent, 0, sizeof(struct media_entity_desc));
	media_ent.id = media_id;
	if (ioctl(dev->media_fd, MEDIA_IOC_ENUM_ENTITIES, &media_ent)) {
		printf("error\n");
		return;
	}

	printf("%s -> ", media_ent.name);
	memset(&links, 0, sizeof (struct media_links_enum));
	links.entity = media_id;
	links.pads = NULL;
	links.links = calloc(media_ent.links,
			sizeof(struct media_link_desc));
	ioctl(dev->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
	for (i = 0; i < media_ent.links; i ++) {
		if (links.links[i].flags & MEDIA_LNK_FL_ENABLED) {
			dump_entity_and_links(dev,
					links.links[i].sink.entity);
		}
			
	}
}

void dump_links(struct viper_device *dev,
		struct viper_io_entity *io_entity) {
	struct media_entity_desc media_ent;
	char test[255];

	memset(&media_ent, 0, sizeof(struct media_entity_desc));
	media_ent.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (!ioctl(dev->media_fd, MEDIA_IOC_ENUM_ENTITIES, &media_ent)) {
		snprintf(test, 255, "%s %s input", dev->name, io_entity->name);
		if (!strcmp(test, media_ent.name)) {
			printf("\n");
			dump_entity_and_links(dev, media_ent.id);
			return;
		}
		media_ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}
	printf("Cannot find media id for %s\n", io_entity->name);
}
#endif

main () {
	struct viper_io_entity *io_entity;
	struct viper_context viper;
	uiomux = uiomux_open_named(names);
	memset(&viper, 0, sizeof(struct viper_context));
	init_context(&viper);
	resize_pipeline(&viper);
#ifdef DEBUG
	io_entity = viper.device_list->io_entity_list;	
	while(io_entity) {
		dump_links(viper.device_list, io_entity);
		io_entity = io_entity->next;
	}
		
	
#endif
	

	
}


