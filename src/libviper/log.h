#ifdef ANDROID
#define LOG_TAG "viper"
#include <cutils/log.h>
#define viper_log(...) ALOGD(__VA_ARGS__)
#else
#include <stdio.h>
#define viper_log(...) fprintf(stderr, __VA_ARGS__)
#endif
