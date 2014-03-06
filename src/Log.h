#pragma once

#define LOGD(fmt, ...) ofLog(OF_LOG_NOTICE, "ofxGestureCam: " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) ofLog(OF_LOG_ERROR, "ofxGestureCam: " fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) ofLog(OF_LOG_FATAL_ERROR, "ofxGestureCam: " fmt, ##__VA_ARGS__)
