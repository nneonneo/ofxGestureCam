#pragma once

#include <libuvc/libuvc.h>
#include <string>

#include "Log.h"

#define uvc_perror(res, msg) LOGE("%s: %s (%d)", (msg), (uvc_strerror(res)), (res))

struct UVCContext {
    uvc_context_t *ctx;

    UVCContext() {
        uvc_error_t res = uvc_init(&ctx, NULL);
        if(res < 0) {
            uvc_perror(res, "uvc_init");
            ctx = NULL;
        }
    }

    ~UVCContext() {
        if(ctx)
            uvc_exit(ctx);
    }

private:
    /* Forbid copying */
    /* Copy constructor */
    UVCContext(const UVCContext &that);
    /* Copy assignment */
    UVCContext& operator=(UVCContext that);
};

struct UVCDevice {
    uvc_device_t *dev;

    UVCDevice(uvc_device_t *dev=NULL) : dev(dev) {
        if(dev)
            uvc_ref_device(dev);
    }

    /* Copy constructor */
    UVCDevice(const UVCDevice &that) : dev(that.dev) {
        if(dev)
            uvc_ref_device(dev);
    }

    /* Destructor */
    ~UVCDevice() {
        if(dev)
            uvc_unref_device(dev);
    }

    /* Swap */
    friend void swap(UVCDevice &first, UVCDevice &second) {
        using std::swap;
        swap(first.dev, second.dev);
    }

    /* Copy assignment */
    UVCDevice& operator=(UVCDevice that) {
        swap(*this, that);
        return *this;
    }

    operator bool() const {
        return (dev != NULL);
    }

    static vector<UVCDevice> getDeviceList(UVCContext &ctx) {
        uvc_device_t **list;
        uvc_error_t res;
        vector<UVCDevice> ret;

        res = uvc_get_device_list(ctx.ctx, &list);
        if(res != UVC_SUCCESS) {
            uvc_perror(res, "uvc_get_device_list");
            return ret;
        }

        for(uvc_device_t **dev = list; *dev; dev++) {
            ret.push_back(UVCDevice(*dev));
        }

        uvc_free_device_list(list, 1);
        return ret;
    }

    static UVCDevice findDevice(UVCContext &ctx, int vid=0, int pid=0, std::string serial="") {
        const char *cserial = serial.c_str();
        if(serial.empty())
            cserial = NULL;

        uvc_device_t *dev;
        uvc_error_t res;

        res = uvc_find_device(ctx.ctx, &dev, vid, pid, cserial);
        if(res != UVC_SUCCESS) {
            uvc_perror(res, "uvc_find_device");
            return UVCDevice();
        }

        UVCDevice ret(dev);
        uvc_unref_device(dev);
        return ret;
    }
};
