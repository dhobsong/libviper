#define LOG_TAG "viper"
#include <cutils/log.h>

#ifdef ANDROID
#define viper_log(...) ALOGD(__VA_ARGS__)
#else
#define viper_log(...) fprintf(stderr, __VA_ARGS__)
#endif
