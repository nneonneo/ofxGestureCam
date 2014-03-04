/*==============================================================================

    Copyright (c) 2014 Robert Xiao
 
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
 
==============================================================================*/
#include "ofxGestureCam.h"
#include "ofMain.h"

#include "GestureCam.h"
#include "Log.h"

#define CREATIVE_VID   0x041e
#define GESTURECAM_PID 0x4096

struct Bool {
    bool val;
    Bool(bool val=false) : val(val) {
    }

    Bool &operator=(bool other) {
        val = other;
        return *this;
    }

    operator bool() const {
        return val;
    }
};

template <typename T> struct TripleBufferedPixels {
    T front; /* Front buffer: for use by the main app */
    T back; /* Back buffer: ready to be written to any time */
    T pending; /* Pending buffer: ready to be swapped in as the new front */
    bool allocated;
    bool updated;

    TripleBufferedPixels() : allocated(false), updated(false) {
    }

    void allocate(int width, int height, int bpp) {
        if(allocated)
            return;

        allocated = true;
        front.allocate(width, height, bpp);
        back.allocate(width, height, bpp);
        pending.allocate(width, height, bpp);
        updated = false;
    }

    void clear() {
        if(!allocated)
            return;

        allocated = false;
        front.clear();
        back.clear();
        pending.clear();
        updated = false;
    }

    void swapBack() {
        swap(back, pending);
        updated = true;
    }

    void swapFront() {
        swap(front, pending);
        updated = false;
    }
};

class ofxGestureCamImpl {
    static UVCContext ctx;

    ofMutex mutex;
    CreativeGestureCam *cam;

    static const int video_width = ofxGestureCam::video_width;
    static const int video_height = ofxGestureCam::video_height;
    static const int depth_width = ofxGestureCam::depth_width;
    static const int depth_height = ofxGestureCam::depth_height;

public:
    ofxGestureCamImpl() : cam(NULL) {
    }

    ~ofxGestureCamImpl() {
        close();
    }

public:
    bool open_first() {
        ofMutex::ScopedLock lock(mutex);
        if(cam) {
            LOGE("open called with an existing open camera!");
            return false;
        }

        UVCDevice dev = UVCDevice::findDevice(ctx, CREATIVE_VID, GESTURECAM_PID);
        if(!dev) {
            return false;
        }

        cam = new CreativeGestureCam(dev);
        configure_streams();

        return true;
    }

    void close() {
        if(cam) {
            /* Stop streams before locking, to avoid a deadlock while waiting for
               the callbacks to complete. */
            cam->stop_video();
            cam->stop_depth();
            {
                ofMutex::ScopedLock lock(mutex);
                delete cam;
                cam = NULL;
            }
        }
    }

    bool isConnected() {
        return (cam != NULL);
    }

private:
    ofTexture depthTex;
    ofTexture videoTex;
    Bool depthTexEnabled;
    Bool videoTexEnabled;

private:
    TripleBufferedPixels<ofPixels> videoPx;
    TripleBufferedPixels<ofShortPixels> depthRawPx;

private:
    Bool isFrameNewDepth;
    Bool isFrameNewVideo;

public:
    void setEnableDepthTexture(bool use) {
        ofMutex::ScopedLock lock(mutex);
        if(use == depthTexEnabled)
            return;

        if(use) {
            depthTex.allocate(depth_width * 2, depth_height, GL_LUMINANCE16);
            depthTexEnabled = true;
        } else {
            depthTex.clear();
            depthTexEnabled = false;
        }
        configure_streams();
    }

    void setEnableVideoTexture(bool use) {
        ofMutex::ScopedLock lock(mutex);
        if(use == videoTexEnabled)
            return;

        if(use) {
            videoTex.allocate(video_width, video_height, GL_RGB);
            videoTexEnabled = true;
        } else {
            videoTex.clear();
            videoTexEnabled = false;
        }
        configure_streams();
    }

    void drawDepth(float x, float y, float w, float h) {
        if(depthTexEnabled)
            depthTex.draw(x, y, w, h);
    }

    void drawVideo(float x, float y, float w, float h) {
        if(videoTexEnabled)
            videoTex.draw(x, y, w, h);
    }

    void update() {
        ofMutex::ScopedLock lock(mutex);
        if(depthRawPx.updated) {
            depthRawPx.swapFront();

            /* TODO */
            if(depthTexEnabled) {
                depthTex.loadData(depthRawPx.front.getPixels(), depth_width * 2, depth_height, GL_LUMINANCE);
            }
            isFrameNewDepth = true;
        } else {
            isFrameNewDepth = false;
        }

        if(videoPx.updated) {
            videoPx.swapFront();

            if(videoTexEnabled) {
                videoTex.loadData(videoPx.front.getPixels(), video_width, video_height, GL_RGB);
            }
            isFrameNewVideo = true;
        } else {
            isFrameNewVideo = false;
        }
    }

    void clear() {
        ofMutex::ScopedLock lock(mutex);

        if(cam != NULL) {
            LOGE("clear() called while camera is still active");
            return;
        }

        depthTex.clear();
        videoTex.clear();
        videoPx.clear();
        depthRawPx.clear();
    }

private:
    void video_cb(uvc_frame_t *frame) {
        uvc_frame_t rgb = {
            videoPx.back.getPixels(),
            video_width*video_height*3,
            video_width,
            video_height,
            UVC_FRAME_FORMAT_RGB,
            video_width*3,
            0
        };

        if(frame->frame_format == UVC_FRAME_FORMAT_MJPEG)
            uvc_mjpeg2rgb(frame, &rgb);
        else
            uvc_any2rgb(frame, &rgb);

        {
            ofMutex::ScopedLock lock(mutex);
            videoPx.swapBack();
        }
    }

    static void static_video_cb(uvc_frame_t *frame, void *userdata) {
        return reinterpret_cast<ofxGestureCamImpl *>(userdata)->video_cb(frame);
    }

    void depth_cb(uvc_frame_t *frame) {
        memcpy(depthRawPx.back.getPixels(), frame->data, frame->data_bytes);
        /* TODO */
        {
            ofMutex::ScopedLock lock(mutex);
            depthRawPx.swapBack();
        }
    }

    static void static_depth_cb(uvc_frame_t *frame, void *userdata) {
        return reinterpret_cast<ofxGestureCamImpl *>(userdata)->depth_cb(frame);
    }

private:
    void configure_streams() {
        if(!cam)
            return;

        if(depthTexEnabled) {
            depthRawPx.allocate(depth_width, depth_height, 2);
            start_depth();
        } else {
            depthRawPx.clear();
            stop_depth();
        }

        if(videoTexEnabled) {
            videoPx.allocate(video_width, video_height, 3);
            start_video(video_width, video_height);
        } else {
            videoPx.clear();
            stop_video();
        }
    }

    /* These must be called with the lock held. */
    void start_depth(int fps=60) {
        if(cam)
            cam->start_depth(static_depth_cb, reinterpret_cast<void *>(this), fps);
        else
            LOGE("start_depth called without cam being active!");
    }

    void start_video(int width=1280, int height=720, int fps=30) {
        if(cam)
            cam->start_video(static_video_cb, reinterpret_cast<void *>(this), width, height, fps);
        else
            LOGE("start_depth called without cam being active!");
    }

    void stop_depth() {
        if(cam)
            cam->stop_depth();
    }

    void stop_video() {
        if(cam)
            cam->stop_video();
    }
};

UVCContext ofxGestureCamImpl::ctx;

/// ofxGestureCam functions

ofxGestureCam::ofxGestureCam() : impl(new ofxGestureCamImpl) {
}

ofxGestureCam::~ofxGestureCam() {
    delete impl;
}

void ofxGestureCam::clear() {
    impl->clear();
}

bool ofxGestureCam::open(int id) {
    if(id != -1) {
        /* TODO */
        LOGE("open(id) not supported");
        return false;
    }

    return impl->open_first();
}

void ofxGestureCam::setEnableDepthTexture(bool use) {
    impl->setEnableDepthTexture(use);
}

void ofxGestureCam::setEnableVideoTexture(bool use) {
    impl->setEnableVideoTexture(use);
}

void ofxGestureCam::close() {
    impl->close();
}

bool ofxGestureCam::isConnected() {
    return impl->isConnected();
}

void ofxGestureCam::update() {
    impl->update();
}

void ofxGestureCam::drawVideo(float x, float y, float w, float h) {
    impl->drawVideo(x, y, w, h);
}

void ofxGestureCam::drawDepth(float x, float y, float w, float h) {
    impl->drawDepth(x, y, w, h);
}
