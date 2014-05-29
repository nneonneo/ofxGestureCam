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

#include "FastAtan2.h"
#include "GestureCam.h"
#include "Log.h"

#include <cstdlib>

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

struct DepthColors {
    ofColor noConfidence;
    ofColor colorMap[65536];

    DepthColors() : noConfidence(0, 0, 0) {
        for(int i=0; i<65536; i++) {
            colorMap[i] = ofColor::fromHsb((i >> 4) & 0xff, 255, 255);
        }
        colorMap[0x7fff + 32767] = ofColor(255, 255, 255);
    }

    ofColor &getColor(int16_t phase, uint16_t confidence) {
        if(confidence < 50)
            return noConfidence;
        return colorMap[phase + 32767];
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
#ifdef ANDROID
        /* On rooted devices, this gives us unrestricted access to USB devices.
        Note: This won't work if you plug in a USB device while the app is running.
        So, make sure you have your USB devices plugged in before the app is started. */
        system("su -c 'chmod 666 /dev/bus/usb/*/*'");
#endif
    }

    ~ofxGestureCamImpl() {
        close();
    }

private:
    void open_dev(UVCDevice &dev) {
        cam = new CreativeGestureCam(dev);

        uvc_device_descriptor_t *desc;
        if(uvc_get_device_descriptor(dev.dev, &desc) == UVC_SUCCESS) {
            if(desc->serialNumber)
                deviceSerial = desc->serialNumber;
            uvc_free_device_descriptor(desc);
        }

        if(depthStreamEnabled)
            start_depth();
        if(videoStreamEnabled)
            start_video();
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

        open_dev(dev);

        return true;
    }

    bool open_nth(int index) {
        vector<UVCDevice> devices = UVCDevice::getDeviceList(ctx);
        int count = 0;
        for(vector<UVCDevice>::iterator it = devices.begin(); it != devices.end(); it++) {
            uvc_device_descriptor_t *desc;
            if(uvc_get_device_descriptor(it->dev, &desc) != UVC_SUCCESS)
                continue;

            if(desc->idVendor == CREATIVE_VID && desc->idProduct == GESTURECAM_PID) {
                if(count == index) {
                    open_dev(*it);
                    uvc_free_device_descriptor(desc);
                    return true;
                }

                count++;
            }

            uvc_free_device_descriptor(desc);
        }

        if(count == 0) {
            LOGE("No devices found.");
        } else {
            LOGE("Device index %d is out of range (only have %d devices)", index, count);
        }
        return false;
    }

    void close() {
        if(cam) {
            /* Stop streams before locking, to avoid a deadlock while waiting for
               the callbacks to complete. */
            stop_video();
            stop_depth();
            {
                ofMutex::ScopedLock lock(mutex);
                delete cam;
                cam = NULL;
                deviceSerial = "";
            }
        }
    }

    bool isConnected() {
        return (cam != NULL);
    }

private:
    void video_cb(uvc_frame_t *frame) {
        uvc_frame_t rgb = {
            videoStreamPx.back.getPixels(),
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
            videoStreamPx.swapBack();
        }
    }

    static void static_video_cb(uvc_frame_t *frame, void *userdata) {
        return reinterpret_cast<ofxGestureCamImpl *>(userdata)->video_cb(frame);
    }

    void depth_cb(uvc_frame_t *frame) {
        if(frame->data_bytes < depth_width * depth_height * 4) {
            LOGE("%s: invalid frame! Got size=%lu, expected %d",
                 __func__, frame->data_bytes, depth_width * depth_height * 4);
            return;
        }
        memcpy(depthStreamPx.back.getPixels(), frame->data, depth_width * depth_height * 4);
        {
            ofMutex::ScopedLock lock(mutex);
            depthStreamPx.swapBack();
        }
    }

    static void static_depth_cb(uvc_frame_t *frame, void *userdata) {
        return reinterpret_cast<ofxGestureCamImpl *>(userdata)->depth_cb(frame);
    }

private:
    /* These must be called with the lock held. */
    void start_depth(int fps=60) {
        if(cam)
            cam->start_depth(static_depth_cb, reinterpret_cast<void *>(this), fps);
    }

    void start_video(int width=1280, int height=720, int fps=30) {
        if(cam)
            cam->start_video(static_video_cb, reinterpret_cast<void *>(this), width, height, fps);
    }

    void stop_depth() {
        if(cam)
            cam->stop_depth();
    }

    void stop_video() {
        if(cam)
            cam->stop_video();
    }

private:
    TripleBufferedPixels<ofPixels> videoStreamPx;
    TripleBufferedPixels<ofShortPixels> depthStreamPx;

public:
    ofShortPixels phaseMap;
    ofShortPixels confidenceMap;
    ofFloatPixels UVMap;
    ofShortPixels distanceMap;
    ofShortPixels rawIRIMap, rawIRQMap;
    ofPixels rawIRIMap8, rawIRQMap8;
    ofPixels depthRGBMap;
    // no videoMap: videoStream is used directly

    ofTexture depthTex;
    ofTexture videoTex;
    ofTexture rawIRITex, rawIRQTex;

private:
    Bool depthStreamEnabled;
    Bool videoStreamEnabled;

    Bool phaseMapEnabled;
    Bool confidenceMapEnabled;
    Bool UVMapEnabled;
    Bool distanceMapEnabled;
    Bool rawIRMapsEnabled;
    Bool videoMapEnabled;

    Bool depthTextureEnabled;
    Bool videoTextureEnabled;
    Bool rawIRTexturesEnabled;

private:
    FastAtan2 fastAtan;
    DepthColors depthColors;

    Bool frameNewDepth;
    Bool frameNewVideo;

public:
    void setEnableDepthStream(bool use) {
        if(use == depthStreamEnabled)
            return;

        if(use) {
            {
                ofMutex::ScopedLock lock(mutex);
                depthStreamPx.allocate(depth_width * 2, depth_height, 1);
            }
            start_depth();
        } else {
            stop_depth();
            {
                ofMutex::ScopedLock lock(mutex);
                depthStreamPx.clear();
            }
        }
        depthStreamEnabled = use;
    }

    void setEnableVideoStream(bool use) {
        if(use == videoStreamEnabled)
            return;

        if(use) {
            {
                ofMutex::ScopedLock lock(mutex);
                videoStreamPx.allocate(video_width, video_height, 3);
            }
            start_video();
        } else {
            stop_video();
            {
                ofMutex::ScopedLock lock(mutex);
                videoStreamPx.clear();
            }
        }
        videoStreamEnabled = use;
    }

    void setEnablePhaseMap(bool use) {
        if(use == phaseMapEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            phaseMap.allocate(depth_width, depth_height, 1);
        } else {
            phaseMap.clear();
        }
        phaseMapEnabled = use;
    }

    void setEnableConfidenceMap(bool use) {
        if(use == confidenceMapEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            confidenceMap.allocate(depth_width, depth_height, 1);
        } else {
            confidenceMap.clear();
        }
        confidenceMapEnabled = use;
    }

    void setEnableUVMap(bool use) {
        if(use == UVMapEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            UVMap.allocate(depth_width, depth_height, 2);
        } else {
            UVMap.clear();
        }
        UVMapEnabled = use;
    }

    void setEnableDistanceMap(bool use) {
        if(use == distanceMapEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            distanceMap.allocate(depth_width, depth_height, 1);
        } else {
            distanceMap.clear();
        }
        distanceMapEnabled = use;
    }

    void setEnableRawIRMaps(bool use) {
    	if(use == rawIRMapsEnabled)
    		return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            rawIRIMap.allocate(depth_width, depth_height, 1);
            rawIRQMap.allocate(depth_width, depth_height, 1);
        } else {
            rawIRIMap.clear();
            rawIRQMap.clear();
        }
        rawIRMapsEnabled = use;
    }

    void setEnableVideoMap(bool use) {
        /* no-op since video map uses the same pixels as the video stream */
        videoMapEnabled = use;
    }

    void setEnableDepthTexture(bool use) {
        if(use == depthTextureEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            depthRGBMap.allocate(depth_width, depth_height, 3);
            depthTex.allocate(depth_width, depth_height, GL_RGB);
        } else {
            depthRGBMap.clear();
            depthTex.clear();
        }
        depthTextureEnabled = use;
    }

    void setEnableVideoTexture(bool use) {
        if(use == videoTextureEnabled)
            return;

        ofMutex::ScopedLock lock(mutex);

        if(use) {
            videoTex.allocate(video_width, video_height, GL_RGB);
        } else {
            videoTex.clear();
        }
        videoTextureEnabled = use;
    }

    void setEnableRawIRTextures(bool use) {
		if(use == rawIRTexturesEnabled)
			return;

		ofMutex::ScopedLock lock(mutex);

		if(use) {
			rawIRIMap8.allocate(depth_width, depth_height, 1);
			rawIRQMap8.allocate(depth_width, depth_height, 1);
            rawIRITex.allocate(depth_width, depth_height, GL_LUMINANCE);
            rawIRQTex.allocate(depth_width, depth_height, GL_LUMINANCE);
		} else {
			rawIRIMap8.clear();
			rawIRQMap8.clear();
			rawIRITex.clear();
			rawIRQTex.clear();
		}
		rawIRTexturesEnabled = use;
	}
public:

    bool isDepthStreamNeeded() {
        return phaseMapEnabled || confidenceMapEnabled || UVMapEnabled || distanceMapEnabled ||
        		rawIRMapsEnabled || depthTextureEnabled || rawIRTexturesEnabled;
    }

    bool isVideoStreamNeeded() {
        return videoMapEnabled || videoTextureEnabled;
    }

    bool isFrameNewDepth() {
        return frameNewDepth;
    }

    bool isFrameNewVideo() {
        return frameNewVideo;
    }

    void drawDepth(float x, float y, float w, float h) {
        if(cam != NULL && depthStreamEnabled && depthTextureEnabled)
            depthTex.draw(x, y, w, h);
    }

    void drawVideo(float x, float y, float w, float h) {
        if(cam != NULL && videoStreamEnabled && videoTextureEnabled)
            videoTex.draw(x, y, w, h);
    }

    void drawRawIRI(float x, float y, float w, float h) {
        if(cam != NULL && depthStreamEnabled && rawIRTexturesEnabled)
            rawIRITex.draw(x, y, w, h);
    }

    void drawRawIRQ(float x, float y, float w, float h) {
        if(cam != NULL && depthStreamEnabled && rawIRTexturesEnabled)
            rawIRQTex.draw(x, y, w, h);
    }

    void update() {
        if(cam == NULL)
            return;

        if(depthStreamPx.updated) {
            {
                ofMutex::ScopedLock lock(mutex);
                depthStreamPx.swapFront();
            }

            const int16_t *rawPx = (int16_t *)depthStreamPx.front.getPixels();

            int16_t *phasePx = (int16_t *)phaseMap.getPixels();
            uint16_t *confidencePx = confidenceMap.getPixels();
            /* TODO: UV */
            uint16_t *distancePx = distanceMap.getPixels();
            int16_t *rawIRIPx = (int16_t *)rawIRIMap.getPixels();
            int16_t *rawIRQPx = (int16_t *)rawIRQMap.getPixels();
            uint8_t *rawIRI8Px = rawIRIMap8.getPixels();
            uint8_t *rawIRQ8Px = rawIRQMap8.getPixels();
            uint8_t *rgbPx = depthRGBMap.getPixels();
            for(int y=0; y<240; y++) {
                for(int x=0; x<320; x+=8) {
                    for(int j=0; j<8; j++) {
                        int16_t I = rawPx[640*y + 2*x + j];
                        int16_t Q = rawPx[640*y + 2*x + 8 + j];
                        int16_t phase = (Q == 0x7fff) ? 0x7fff : fastAtan.atan2_16(Q, I);
                        uint16_t confidence = ((I < 0) ? -I : I) + ((Q < 0) ? -Q : Q);

                        if(phaseMapEnabled)
                            *phasePx++ = phase;
                        if(confidenceMapEnabled)
                            *confidencePx++ = confidence;
                        if(distanceMapEnabled) {
                            /* TODO: Correct the distance calculation! */
                            *distancePx++ = (phase + 32767) / 16;
                        }
                        if(rawIRMapsEnabled) {
                        	*rawIRIPx++ = I;
                        	*rawIRQPx++ = Q;
                        }
                        if(depthTextureEnabled) {
                            ofColor c = depthColors.getColor(phase, confidence);
                            rgbPx[0] = c.r;
                            rgbPx[1] = c.g;
                            rgbPx[2] = c.b;
                            rgbPx += 3;
                        }
                        if(rawIRTexturesEnabled) {
                        	*rawIRI8Px++ = (I >> 1) + 128;
                        	*rawIRQ8Px++ = (Q >> 1) + 128;
                        }
                    }
                }
            }

            if(depthTextureEnabled) {
                depthTex.loadData(depthRGBMap.getPixels(), depth_width, depth_height, GL_RGB);
            }
            if(rawIRTexturesEnabled) {
            	rawIRITex.loadData(rawIRIMap8.getPixels(), depth_width, depth_height, GL_LUMINANCE);
            	rawIRQTex.loadData(rawIRQMap8.getPixels(), depth_width, depth_height, GL_LUMINANCE);
            }
            frameNewDepth = true;
        } else {
            frameNewDepth = false;
        }

        if(videoStreamPx.updated) {
            {
                ofMutex::ScopedLock lock(mutex);
                videoStreamPx.swapFront();
            }

            if(videoTextureEnabled) {
                videoTex.loadData(videoStreamPx.front.getPixels(), video_width, video_height, GL_RGB);
            }
            frameNewVideo = true;
        } else {
            frameNewVideo = false;
        }
    }

    void clear() {
        ofMutex::ScopedLock lock(mutex);

        if(cam != NULL) {
            LOGE("clear() called while camera is still active");
            return;
        }

        setEnablePhaseMap(false);
        setEnableConfidenceMap(false);
        setEnableUVMap(false);
        setEnableDistanceMap(false);
        setEnableVideoMap(false);

        setEnableDepthTexture(false);
        setEnableVideoTexture(false);

        setEnableDepthStream(false);
        setEnableVideoStream(false);
    }

public:
    string deviceSerial;

    static void listDevices() {
        vector<UVCDevice> devices = UVCDevice::getDeviceList(ctx);
        int count = 0;
        for(vector<UVCDevice>::iterator it = devices.begin(); it != devices.end(); it++) {
            uvc_device_descriptor_t *desc;
            if(uvc_get_device_descriptor(it->dev, &desc) != UVC_SUCCESS)
                continue;

            LOGD("Device vid=%04x pid=%04x serial=%s manufacturer=%s product=%s\n",
                desc->idVendor, desc->idProduct, desc->serialNumber ?: "",
                desc->manufacturer ?: "", desc->product ?: "");

            if(desc->idVendor == CREATIVE_VID && desc->idProduct == GESTURECAM_PID) {
                count++;
            }

            uvc_free_device_descriptor(desc);
        }
        if(count == 0) {
            LOGD("No GestureCam devices found.");
        }
    }

    static int numDevices() {
        vector<UVCDevice> devices = UVCDevice::getDeviceList(ctx);
        int count = 0;
        for(vector<UVCDevice>::iterator it = devices.begin(); it != devices.end(); it++) {
            uvc_device_descriptor_t *desc;
            if(uvc_get_device_descriptor(it->dev, &desc) != UVC_SUCCESS)
                continue;

            if(desc->idVendor == CREATIVE_VID && desc->idProduct == GESTURECAM_PID) {
                count++;
            }

            uvc_free_device_descriptor(desc);
        }
        return count;
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
    if(id == -1) {
        return impl->open_first();
    } else {
        return impl->open_nth(id);
    }
}


void ofxGestureCam::enablePhaseMap() {
    impl->setEnableDepthStream(true);
    impl->setEnablePhaseMap(true);
}

void ofxGestureCam::disablePhaseMap() {
    impl->setEnablePhaseMap(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableConfidenceMap() {
    impl->setEnableDepthStream(true);
    impl->setEnableConfidenceMap(true);
}

void ofxGestureCam::disableConfidenceMap() {
    impl->setEnableConfidenceMap(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableUVMap() {
    impl->setEnableDepthStream(true);
    impl->setEnableUVMap(true);
}

void ofxGestureCam::disableUVMap() {
    impl->setEnableUVMap(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableDistanceMap() {
    impl->setEnableDepthStream(true);
    impl->setEnableDistanceMap(true);
}

void ofxGestureCam::disableDistanceMap() {
    impl->setEnableDistanceMap(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableRawIRMaps() {
	impl->setEnableRawIRMaps(true);
    impl->setEnableDepthStream(true);
}

void ofxGestureCam::disableRawIRMaps() {
    impl->setEnableRawIRMaps(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableVideoMap() {
    impl->setEnableVideoStream(true);
    impl->setEnableVideoMap(true);
}

void ofxGestureCam::disableVideoMap() {
    impl->setEnableVideoMap(false);
    if(!impl->isVideoStreamNeeded())
        impl->setEnableVideoStream(false);
}


void ofxGestureCam::enableDepthTexture() {
    impl->setEnableDepthStream(true);
    impl->setEnableDepthTexture(true);
}

void ofxGestureCam::disableDepthTexture() {
    impl->setEnableDepthTexture(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableRawIRTextures() {
	impl->setEnableRawIRTextures(true);
    impl->setEnableDepthStream(true);
}

void ofxGestureCam::disableRawIRTextures() {
    impl->setEnableRawIRTextures(false);
    if(!impl->isDepthStreamNeeded())
        impl->setEnableDepthStream(false);
}


void ofxGestureCam::enableVideoTexture() {
    impl->setEnableVideoStream(true);
    impl->setEnableVideoTexture(true);
}

void ofxGestureCam::disableVideoTexture() {
    impl->setEnableVideoTexture(false);
    if(!impl->isVideoStreamNeeded())
        impl->setEnableVideoStream(false);
}


bool ofxGestureCam::isFrameNewVideo() {
    return impl->isFrameNewVideo();
}

bool ofxGestureCam::isFrameNewDepth() {
    return impl->isFrameNewDepth();
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

void ofxGestureCam::drawRawIRI(float x, float y, float w, float h) {
	impl->drawRawIRI(x, y, w, h);
}

void ofxGestureCam::drawRawIRQ(float x, float y, float w, float h) {
	impl->drawRawIRQ(x, y, w, h);
}

short* ofxGestureCam::getPhasePixels() {
    return reinterpret_cast<short *>(impl->phaseMap.getPixels());
}

unsigned short* ofxGestureCam::getConfidencePixels() {
    return impl->confidenceMap.getPixels();
}

ofVec2f* ofxGestureCam::getUVCoords() {
    return reinterpret_cast<ofVec2f *>(impl->UVMap.getPixels());
}

unsigned short* ofxGestureCam::getDistancePixels() {
    return impl->distanceMap.getPixels();
}

short* ofxGestureCam::getRawIRIPixels() {
	return reinterpret_cast<short *>(impl->rawIRIMap.getPixels());
}

short* ofxGestureCam::getRawIRQPixels() {
	return reinterpret_cast<short *>(impl->rawIRQMap.getPixels());
}

ofTexture& ofxGestureCam::getVideoTextureRef() {
    return impl->videoTex;
}

ofTexture& ofxGestureCam::getDepthTextureRef() {
    return impl->depthTex;
}

ofTexture& ofxGestureCam::getRawIRITextureRef() {
    return impl->rawIRITex;
}

ofTexture& ofxGestureCam::getRawIRQTextureRef() {
    return impl->rawIRQTex;
}

string ofxGestureCam::getSerial() const {
    return impl->deviceSerial;
}

void ofxGestureCam::listDevices() {
    ofxGestureCamImpl::listDevices();
}

int ofxGestureCam::numDevices() {
    return ofxGestureCamImpl::numDevices();
}
