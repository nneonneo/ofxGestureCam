#pragma once
#include "UVC.h"

/* Vendor UUID: dd880f8a-1cba-4954-8a25-f7875967f0f7 */
static uint8_t depthcam_ext_unit_guid[] = {0x8A, 0x0F, 0x88, 0xDD, 0xBA, 0x1C, 0x54, 0x49, 0x8A, 0x25, 0xF7, 0x87, 0x59, 0x67, 0xF0, 0xF7};

/* Utility functions */
static inline void write_le16(uint8_t *buf, uint16_t val) {
    buf[0] = val;
    buf[1] = val >> 8;
}
static inline void write_le32(uint8_t *buf, uint32_t val) {
    buf[0] = val;
    buf[1] = val >> 8;
    buf[2] = val >> 16;
    buf[3] = val >> 24;
}
static inline uint16_t read_le16(uint8_t *buf) {
    return buf[0] | (buf[1] << 8);
}
static inline uint32_t read_le32(uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

class CreativeGestureCam {
    uvc_device_handle_t *devh;
    uvc_stream_handle_t *depth_stream;
    uvc_stream_handle_t *video_stream;
    int ext_unit;

public:
    CreativeGestureCam(UVCDevice &dev) {
        uvc_error_t res = uvc_open(dev.dev, &devh);
        if(res < 0) {
            uvc_perror(res, "uvc_open");
            devh = NULL;
            return;
        }

        ext_unit = -1;
        const uvc_extension_unit_t *ext = uvc_get_extension_units(devh);
        for(; ext; ext = ext->next) {
            if(memcmp(ext->guidExtensionCode, depthcam_ext_unit_guid, sizeof(depthcam_ext_unit_guid)) == 0) {
                ext_unit = ext->bUnitID;
                break;
            }
        }

        if(ext_unit < 0) {
            LOGE("Could not find Creative extension unit on supplied device!");
            uvc_close(devh);
            devh = NULL;
        }

        depth_stream = NULL;
        video_stream = NULL;
    }

    ~CreativeGestureCam() {
        if(devh) {
            uvc_stop_streaming(devh);
            uvc_close(devh);
            devh = NULL;
        }
    }

private:
    /* Forbid copying */
    /* Copy constructor */
    CreativeGestureCam(const CreativeGestureCam &that);
    /* Copy assignment */
    CreativeGestureCam& operator=(CreativeGestureCam that);

public:
    uvc_error_t start_depth(uvc_frame_callback_t cb, void *userdata, int fps=60) {
        if(depth_stream) {
            return UVC_ERROR_INVALID_MODE;
        }

        uvc_stream_ctrl_t ctrl;
        uvc_error_t res = UVC_SUCCESS;

        res = uvc_get_stream_ctrl_format_size(
            devh, &ctrl,
            /* format, width, height, fps */
            UVC_FRAME_FORMAT_UNCOMPRESSED, 640, 240, fps
        );
        if(res < 0) {
            uvc_perror(res, "depth: uvc_get_stream_ctrl_format_size");
            goto done;
        }

        res = uvc_stream_open_ctrl(devh, &depth_stream, &ctrl);
        if (res < 0) {
            uvc_perror(res, "depth: uvc_stream_open_ctrl");
            goto done;
        }

        res = uvc_stream_start_iso(depth_stream, cb, userdata);
        if (res < 0) {
            uvc_perror(res, "depth: uvc_stream_start_iso");
            goto done;
        }

        init_depthcam(fps);

        return res;

    done:
        if(depth_stream) {
            uvc_stream_close(depth_stream);
            depth_stream = NULL;
        }
        return res;
    }

    uvc_error_t start_video(uvc_frame_callback_t cb, void *userdata, int width=1280, int height=720, int fps=30) {
        if(video_stream) {
            return UVC_ERROR_INVALID_MODE;
        }

        uvc_stream_ctrl_t ctrl;
        uvc_error_t res = UVC_SUCCESS;

        res = uvc_get_stream_ctrl_format_size(
            devh, &ctrl,
            UVC_FRAME_FORMAT_ANY, width, height, fps
        );
        if(res < 0) {
            uvc_perror(res, "video: uvc_get_stream_ctrl_format_size");
            goto done;
        }

        res = uvc_stream_open_ctrl(devh, &video_stream, &ctrl);
        if (res < 0) {
            uvc_perror(res, "video: uvc_stream_open_ctrl");
            goto done;
        }

        res = uvc_stream_start_iso(video_stream, cb, userdata);
        if (res < 0) {
            uvc_perror(res, "video: uvc_stream_start_iso");
            goto done;
        }

        return res;

    done:
        if(video_stream) {
            uvc_stream_close(video_stream);
            video_stream = NULL;
        }
        return res;
    }

    void stop_depth() {
        if(depth_stream) {
            uvc_stream_close(depth_stream);
            depth_stream = NULL;
        }
    }

    void stop_video() {
        if(video_stream) {
            uvc_stream_close(video_stream);
            video_stream = NULL;
        }
    }

private:
    uvc_error_t read_rom(uint8_t *buf, uint16_t startaddr, int len) {
        uint8_t cmdbuf[33];
        uvc_error_t res;
        int ctrl_len;

        ctrl_len = uvc_get_ctrl_len(devh, ext_unit, 0x03);
        if(ctrl_len < 0) {
            res = (uvc_error_t)ctrl_len;
            uvc_perror(res, "cg_read_rom get len fail");
            return res;
        } else if(ctrl_len != sizeof(cmdbuf)) {
            LOGE("cg_read_rom wrong ctrl len: got %d, expected %ld\n",
                ctrl_len, sizeof(cmdbuf));
            return UVC_ERROR_INVALID_DEVICE;
        }

        memset(cmdbuf, 0, sizeof(cmdbuf));
        cmdbuf[0] = 0x01;
        write_le16(cmdbuf+1, startaddr);
        write_le16(cmdbuf+3, startaddr+len-1);

        res = (uvc_error_t)uvc_set_ctrl(devh, ext_unit, 0x03, cmdbuf, sizeof(cmdbuf));
        if(res < 0) {
            uvc_perror(res, "cg_read_rom set 0x01 fail");
            return res;
        }

        res = (uvc_error_t)uvc_get_ctrl(devh, ext_unit, 0x03, cmdbuf, sizeof(cmdbuf), UVC_GET_CUR);
        if(res < 0) {
            uvc_perror(res, "cg_read_rom get 0x01 fail");
            return res;
        }

        if(cmdbuf[0] != 0x01 || read_le16(cmdbuf+1) != 0xffff || read_le16(cmdbuf+3) != (uint16_t)~startaddr) {
            LOGE("cg_read_rom unexpected 0x01 response: got %02x %04x %04x, expected %02x %04x %04x\n",
                cmdbuf[0], read_le16(cmdbuf+1), read_le16(cmdbuf+3),
                0x01, 0xffff, (uint16_t)~startaddr);
            return UVC_ERROR_INVALID_DEVICE;
        }

        memset(cmdbuf, 0, sizeof(cmdbuf));
        cmdbuf[0] = 0x02;
        res = (uvc_error_t)uvc_set_ctrl(devh, ext_unit, 0x03, cmdbuf, sizeof(cmdbuf));
        if(res < 0) {
            uvc_perror(res, "cg_read_rom set 0x02 fail");
            return res;
        }

        memset(cmdbuf, 0, sizeof(cmdbuf));
        cmdbuf[0] = 0x03;
        res = (uvc_error_t)uvc_set_ctrl(devh, ext_unit, 0x03, cmdbuf, sizeof(cmdbuf));
        if(res < 0) {
            uvc_perror(res, "cg_read_rom set 0x03 fail");
            return res;
        }

        while(len) {
            int readlen = (len < 32) ? len : 32;
            res = (uvc_error_t)uvc_get_ctrl(devh, ext_unit, 0x03, cmdbuf, sizeof(cmdbuf), UVC_GET_CUR);
            if(res < 0) {
                uvc_perror(res, "cg_read_rom get 0x03 fail");
                return res;
            }
            memcpy(buf, cmdbuf+1, readlen);
            buf += readlen;
            len -= readlen;
        }

        return UVC_SUCCESS;
    }

    uvc_error_t _read_op(uint8_t op, uint16_t reg, uint16_t *ret) {
        uint8_t cmdbuf[7];
        uvc_error_t res;

        memset(cmdbuf, 0, sizeof(cmdbuf));
        cmdbuf[0] = op;
        write_le16(cmdbuf+1, reg);

        res = (uvc_error_t)uvc_set_ctrl(devh, ext_unit, 0x02, cmdbuf, sizeof(cmdbuf));
        if(res < 0)
            return res;

        res = (uvc_error_t)uvc_get_ctrl(devh, ext_unit, 0x02, cmdbuf, sizeof(cmdbuf), UVC_GET_CUR);
        if(res < 0)
            return res;

        if(read_le16(cmdbuf+1) != reg) {
            LOGE("warning: _cg_read_op register mismatch: got %04x, expected %04x\n",
                read_le16(cmdbuf+1), reg);
        }

        *ret = read_le16(cmdbuf+3);
        return UVC_SUCCESS;
    }

    uvc_error_t _write_op(uint8_t op, uint16_t reg, uint16_t val) {
        uint8_t cmdbuf[7];

        memset(cmdbuf, 0, sizeof(cmdbuf));
        cmdbuf[0] = op;
        write_le16(cmdbuf+1, reg);
        write_le16(cmdbuf+3, val);

        return (uvc_error_t)uvc_set_ctrl(devh, ext_unit, 0x02, cmdbuf, sizeof(cmdbuf));
    }

    int get_fpga_state() {
        uint16_t state = 0;
        uvc_error_t res;

        res = _read_op(0x86, 0, &state);
        if(res < 0) {
            uvc_perror(res, "cg_get_fpga_state fail");
            return (int)res;
        }
        return state;
    }

    uvc_error_t read_reg(uint16_t reg, uint16_t *val) {
        uvc_error_t res = _read_op(0x92, reg, val);
        if(res < 0) {
            LOGE("read_reg(%04x) failed!\n", reg);
        }
        return res;
    }

    uvc_error_t write_reg(uint16_t reg, uint16_t val) {
        uvc_error_t res = _write_op(0x12, reg, val);
        if(res < 0) {
            LOGE("write_reg(%04x, %04x) failed!\n", reg, val);
        }
        return res;    
    }

    void init_depthcam(int fps=60) {
        int state;

        while((state = get_fpga_state()) != 2) {
            printf("waiting for device (state=%d)\n", state);
            usleep(5000);
        }

        /* No, I don't know what most of these do. */
        /* This code somehow also enables the accelerometer... */
        write_reg(0x1a, 0x0000);
        write_reg(0x1b, 0x0000);
        write_reg(0x13, 0x0004);
        write_reg(0x14, 0x2c00);
        write_reg(0x15, 0x0001);
        write_reg(0x16, 0x0000);
        write_reg(0x17, 0x00ef);
        write_reg(0x18, 0x0000);
        write_reg(0x19, 0x013f);
        write_reg(0x1a, 0x0400);
        write_reg(0x1b, 0x0100);
        write_reg(0x1b, 0x0500);
        write_reg(0x1b, 0x0d00);
        write_reg(0x1c, 0x0005);
        write_reg(0x20, 0x04b0);
        write_reg(0x27, 0x0106);
        write_reg(0x28, 0x014d);
        write_reg(0x29, 0x00f0);
        write_reg(0x2a, 0x014d);
        write_reg(0x30, 0x0000);
        write_reg(0x31, 0x0000);
        write_reg(0x32, 0x0000);
        write_reg(0x3c, 0x002f);
        write_reg(0x3d, 0x03e7);
        write_reg(0x3e, 0x000f);
        write_reg(0x3f, 0x000f);
        write_reg(0x40, 0x03e8);
        write_reg(0x43, 0x0109);
        write_reg(0x1e, 0x8209);
        write_reg(0x1d, 0x0119);
        write_reg(0x44, 0x001e);
        write_reg(0x1b, 0x0d00);
        write_reg(0x1b, 0x4d00);
        write_reg(0x45, 0x0101);
        write_reg(0x46, 0x0002);
        write_reg(0x47, 0x0032);
        write_reg(0x2f, 0x0060);
        write_reg(0x00, 0x0c0c);
        write_reg(0x01, 0x0c0c);
        write_reg(0x2f, 0x0060);
        write_reg(0x03, 0x0000);
        write_reg(0x04, 0x0030);
        write_reg(0x05, 0x0060);
        write_reg(0x06, 0x0090);
        write_reg(0x07, 0x0000);
        write_reg(0x08, 0x0000);
        write_reg(0x09, 0x0000);
        write_reg(0x0a, 0x0000);
        write_reg(0x02, 0x0000);
        write_reg(0x0b, 0xea60);
        write_reg(0x0c, 0x0000);
        write_reg(0x0d, 0x4740);
        write_reg(0x0e, 0x0000);
        write_reg(0x0f, 0x0000);
        write_reg(0x10, 0x0000);
        write_reg(0x11, 0x01e0);
        write_reg(0x12, (fps == 60) ? 2 : 4); // 2 for 60fps, 4 for 30fps
        write_reg(0x1a, 0x1400);
        write_reg(0x33, 0x70f0);
        write_reg(0x4a, 0x0002);
        write_reg(0x1a, 0x1480);
        write_reg(0x1a, 0x14c0);
    }

    void deinit_depthcam() {
        if(get_fpga_state() != 2)
            return;

        write_reg(0x1a, 0);
        write_reg(0x1b, 0);
        write_reg(0x4b, 0);
    }

    void print_accel() {
        int16_t vals[3];

        read_reg(0x38, (uint16_t*)&vals[0]);
        read_reg(0x39, (uint16_t*)&vals[1]);
        read_reg(0x3a, (uint16_t*)&vals[2]);
        LOGD("accel: %d %d %d\n", vals[0], vals[1], vals[2]);
    }
};
