#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <vector>
#include <iostream>
#include <sys/mman.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>

#include "zygisk.hpp"
#include <lsplt.hpp>
#include <android/log.h>

#define LOG_TAG "MapHide"

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)


#define CONFIG_PATH "/data/adb/maphide"
#define PKG_LIST    CONFIG_PATH "/packages.list"
#define MAP_LIST   CONFIG_PATH "/mapname.list"

static void hide_from_maps(std::vector<lsplt::MapInfo> maps) {
    for (auto &info : maps) {
        LOGI("hide: %s\n", info.path.data());
        void *addr = reinterpret_cast<void *>(info.start);
        size_t size = info.end - info.start;
        void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if ((info.perms & PROT_READ) == 0) {
            mprotect(addr, size, PROT_READ);
        }
        memcpy(copy, addr, size);
        mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
        mprotect(addr, size, info.perms);
    }
}

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MapHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
    	if (args->uid > 1000) {
            DoHide();
    	}
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api;
    JNIEnv *env;

    void DoHide() {
    	auto maps = lsplt::MapInfo::Scan();
        struct stat st;
        if (stat("/data", &st)) return;

        // hide module file from maps
        // detection: https://github.com/vvb2060/MagiskDetector/blob/master/README_ZH.md
        // hide all maps with path is data partition but path is not /data/*
        
        for (auto iter = maps.begin(); iter != maps.end();) {
            LOGD("checking map: '%s'", iter->path.c_str());
            // 匹配所有包含 "pine" 的路径（包括 [anon:pine codes]、/memfd:pine、/data/pine.so 等）
            if (iter->path.find("[anon:") != std::string::npos) {
                LOGD("find pine: %s", iter->path.c_str());
                iter = maps.erase(iter);
                continue;
            }

            // 原有的过滤逻辑：不是 /data 分区 或是路径以 /data/ 开头
            if (iter->dev != st.st_dev || iter->path.starts_with("/data/")) {
                iter = maps.erase(iter);
            } else {
                ++iter;
            }
        }

        hide_from_maps(maps);
    }
};

static void companion_handler(int i) {
    return;
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
