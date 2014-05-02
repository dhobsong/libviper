LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../../include \
	external/libuiomux/include

#LOCAL_CFLAGS := -DDEBUG

LOCAL_SRC_FILES := \
	entity_config.c shvio_compat.c util.c

LOCAL_SHARED_LIBRARIES := libcutils \
			  libuiomux
LOCAL_MODULE := libviper
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)
