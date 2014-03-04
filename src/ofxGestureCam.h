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
#pragma once

#include "ofMain.h"

class ofxGestureCamImpl;

/// \class ofxGestureCam
///
/// Wrapper for a Creative GestureCam device
///
class ofxGestureCam {

public:

	ofxGestureCam();
	virtual ~ofxGestureCam();

/// \section Main

	/// Clear resources; do not call this while ofxGestureCam is running!
	void clear();

	/// Open the connection and start grabbing images.
	///
	/// Set the ID to choose a camera; see numAvailableDevices().
	/// If you don't set the ID, the first available device will be used
	bool open(int id=-1);

    /// \subsection Feature Control
    /// Phase map (linearly correlated with depth).
    /// Enabling this will enable the depth stream.
    void setEnablePhaseMap(bool use=true);

    /// Confidence map (confidence in the depth estimation at each pixel).
    /// Enabling this will enable the depth stream.
    void setEnableConfidenceMap(bool use=true);

    /// UV map (colour image coordinates for each depth pixel).
    /// Enabling this will enable the depth stream.
    void setEnableUVMap(bool use=true);
    
    /// Depth map (millimetre distance from the camera at each pixel).
    /// Enabling this will enable the depth stream.
    void setEnableDepthMap(bool use=true);
    
    /// Color map (RGB color data from the colour camera).
    /// Enabling this will enable the color stream.
    void setEnableColorMap(bool use=true);
    
    /// Depth texture (drawable texture containing millimetre data mapped into RGB colours).
    /// Enabling this will enable the depth stream.
    void setEnableDepthTexture(bool use=true);
    
    /// Color texture (drawable texture containing colour RGB data).
    /// Enabling this will enable the depth stream.
    void setEnableColorTexture(bool use=true);

	/// Close the connection and stop grabbing images
	void close();

	/// Is the connection currently open?
	bool isConnected();

	/// Is the current frame new?
	bool isFrameNew();
	bool isFrameNewVideo();
	bool isFrameNewDepth();

	/// Updates all enabled images and textures.
	void update();

/// \section Pixel Data

	/// get the pixels of the most recent depth frame
	unsigned short* getPhasePixels();   ///< raw 16 bit values

	/// get the distance in millimeters to a given point as a float array
	float* getDistancePixels();

	/// get the video pixels reference
	ofPixels & getRGBPixelsRef();

	/// get the pixels of the most recent depth frame
	ofShortPixels & getPhasePixelsRef();	///< raw 11 bit values

	/// get the distance in millimeters to a given point as a float array
	ofFloatPixels & getDistancePixelsRef();

/// \section Draw

	/// draw the video texture
	void drawColor(float x, float y, float w, float h);
	void drawColor(float x, float y);
	void drawColor(const ofPoint& point);
	void drawColor(const ofRectangle& rect);

	/// draw the colorized depth texture
	void drawDepth(float x, float y, float w, float h);
	void drawDepth(float x, float y);
	void drawDepth(const ofPoint& point);
	void drawDepth(const ofRectangle& rect);

/// \section Util

	/// get the device id
	/// returns -1 if not connected
	int getDeviceId() const;

	/// get the unique device serial number
	/// returns an empty string "" if not connected
	string getSerial() const;

	/// static depth image size
	const static int width = 320;
	const static int height = 240;
	float getHeight() const { return height; }
	float getWidth() const { return width; }

/// \section Static global device functions

	/// print the device list
	static void listDevices();

	/// get the total number of devices
	static int numTotalDevices();

	/// get the number of available devices (not connected)
	static int numAvailableDevices();

	/// get the number of currently connected devices
	static int numConnectedDevices();

	/// is a device already connected?
	static bool isDeviceConnected(int id);
	static bool isDeviceConnected(string serial);

	/// get the id of the next available device,
	/// returns -1 if nothing found
	static int nextAvailableId();

	/// get the serial number of the next available device,
	/// returns an empty string "" if nothing found
	static string nextAvailableSerial();

private:
    friend class ofxGestureCamImpl;
    ofxGestureCamImpl *impl;

private:
    /* Forbid copying */
    /* Copy constructor */
    ofxGestureCam(const ofxGestureCam &that);
    /* Copy assignment */
    ofxGestureCam& operator=(ofxGestureCam that);
};
