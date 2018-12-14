/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>
#include <time.h>
#include <stdio.h>
#include <memory>
#include <vector>

#include "GifTranscoder.h"

#define SQUARE(a) ((a)*(a))

// GIF does not support partial transparency, so our alpha channels are always 0x0 or 0xff.
static const ColorARGB TRANSPARENT = 0x0;

#define ALPHA(color) (((color) >> 24) & 0xff)
#define RED(color)   (((color) >> 16) & 0xff)
#define GREEN(color) (((color) >>  8) & 0xff)
#define BLUE(color)  (((color) >>  0) & 0xff)

#define MAKE_COLOR_ARGB(a, r, g, b) \
    ((a) << 24 | (r) << 16 | (g) << 8 | (b))

#define MAX_COLOR_DISTANCE (255 * 255 * 255)

#define TAG "GifTranscoder.cpp"
#define LOGD_ENABLED 0
#if LOGD_ENABLED
#define LOGD(...)((void)0)
#else
#define LOGD(...)((void)0)
#endif
#define LOGI(...)((void)0)
#define LOGW(...)((void)0)
#define LOGE(...)((void)0)

// This macro expects the assertion to pass, but logs a FATAL if not.
#define ASSERT(cond, ...) \
    ( (__builtin_expect((cond) == 0, 0)) \
    ? ((void)__android_log_assert(#cond, TAG, ## __VA_ARGS__)) \
    : (void) 0 )
#define ASSERT_ENABLED 1

namespace {

// Current time in milliseconds since Unix epoch.
double now(void) {
    struct timespec res;
    clock_gettime(CLOCK_REALTIME, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}

// Gets the pixel at position (x,y) from a buffer that uses row-major order to store an image with
// the specified width.
template <typename T>
T* getPixel(T* buffer, int width, int x, int y) {
    return buffer + (y * width + x);
}

} // namespace

int GifTranscoder::transcode(const char* pathIn, const char* pathOut) {
    int error;
    double t0;
    GifFileType* gifIn;
    GifFileType* gifOut;
    
    // Automatically closes the GIF files when this method returns
    GifFilesCloser closer;

// Index of the current image.
    int imageIndex = 0;

    // Transparent color of the current image.
    int transparentColor = NO_TRANSPARENT_COLOR;

    gifIn = DGifOpenFileName(pathIn, &error);
    if (gifIn) {
        closer.setGifIn(gifIn);
        LOGD("Opened input GIF: %s", pathIn);
    } else {
        LOGE("Could not open input GIF: %s, error = %d", pathIn, error);
        return GIF_ERROR;
    } 

    gifOut = EGifOpenFileName(pathOut, false, &error);
    if (gifOut) {
        closer.setGifOut(gifOut);
        LOGD("Opened output GIF: %s", pathOut);
    } else {
        LOGE("Could not open output GIF: %s, error = %d", pathOut, error);
        return GIF_ERROR;
    }

    // Buffer for reading raw images from the input GIF.
    std::vector<GifByteType> srcBuffer(gifIn->SWidth * gifIn->SHeight);
    srcBuffer.resize(gifIn->Image.Width * gifIn->Image.Height);

    // Buffer for rendering images from the input GIF.
    std::unique_ptr<ColorARGB[]> renderBuffer(new ColorARGB[gifIn->SWidth * gifIn->SHeight]);

    // Buffer for writing new images to output GIF (one row at a time).
    std::unique_ptr<GifByteType[]> dstRowBuffer(new GifByteType[gifOut->SWidth]);

    // Background color (applies to entire GIF).
    ColorARGB bgColor = TRANSPARENT;

    // Many GIFs use DISPOSE_DO_NOT to make images draw on top of previous images. They can also
    // use DISPOSE_BACKGROUND to clear the last image region before drawing the next one. We need
    // to keep track of the disposal mode as we go along to properly render the GIF.
    int disposalMode = DISPOSAL_UNSPECIFIED;
    int prevImageDisposalMode = DISPOSAL_UNSPECIFIED;
    GifImageDesc prevImageDimens;

    
    if (renderImage(gifIn, srcBuffer.data(), imageIndex, transparentColor, renderBuffer.get(), bgColor,
                    prevImageDimens, prevImageDisposalMode) == false) {
                    LOGE("Could not render %d", imageIndex);
                    return false;
    } else {
        LOGD("Rendered image (%d)", imageIndex);
    }

    return GIF_OK;
}



bool GifTranscoder::renderImage(GifFileType* gifIn,
                                GifByteType* rasterBits,
                                int imageIndex,
                                int transparentColorIndex,
                                ColorARGB* renderBuffer,
                                ColorARGB bgColor,
                                GifImageDesc prevImageDimens,
                                int prevImageDisposalMode) {


    ColorMapObject* colorMap = getColorMap(gifIn);
    if (colorMap == NULL) {
        LOGE("No GIF color map found");
        return false;
    }

    // Clear all or part of the background, before drawing the first image and maybe before drawing
    // subsequent images (depending on the DisposalMode).
    if (imageIndex == 0) {
        fillRect(renderBuffer, gifIn->SWidth, gifIn->SHeight,
                 0, 0, gifIn->SWidth, gifIn->SHeight, bgColor);
    } else if (prevImageDisposalMode == DISPOSE_BACKGROUND) {
        fillRect(renderBuffer, gifIn->SWidth, gifIn->SHeight,
                 prevImageDimens.Left, prevImageDimens.Top,
                 prevImageDimens.Width, prevImageDimens.Height, TRANSPARENT);
    }

    // Paint this image onto the canvas
    for (int y = 0; y < gifIn->Image.Height; y++) {
        for (int x = 0; x < gifIn->Image.Width; x++) {
            GifByteType colorIndex = *getPixel(rasterBits, gifIn->Image.Width, x, y);
            if (colorIndex >= colorMap->ColorCount) {
                LOGE("Color Index %d is out of bounds (count=%d)", colorIndex,
                    colorMap->ColorCount);
                return false;
            }

            // This image may be smaller than the GIF's "logical screen"
            int renderX = x + gifIn->Image.Left;
            int renderY = y + gifIn->Image.Top;

            // Skip drawing transparent pixels if this image renders on top of the last one
            if (imageIndex > 0 && prevImageDisposalMode == DISPOSE_DO_NOT &&
                colorIndex == transparentColorIndex) {
                continue;
            }

            ColorARGB* renderPixel = getPixel(renderBuffer, gifIn->SWidth, renderX, renderY);
            *renderPixel = getColorARGB(colorMap, transparentColorIndex, colorIndex);
        }
    }
    return true;
}

void GifTranscoder::fillRect(ColorARGB* renderBuffer,
                             int imageWidth,
                             int imageHeight,
                             int left,
                             int top,
                             int width,
                             int height,
                             ColorARGB color) {

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            ColorARGB* renderPixel = getPixel(renderBuffer, imageWidth, x + left, y + top);
            *renderPixel = color;
        }
    }
}

GifByteType GifTranscoder::computeNewColorIndex(GifFileType* gifIn,
                                                int transparentColorIndex,
                                                ColorARGB* renderBuffer,
                                                int x,
                                                int y) {
    ColorMapObject* colorMap = getColorMap(gifIn);

    // Compute the average color of 4 adjacent pixels from the input image.
    ColorARGB c1 = *getPixel(renderBuffer, gifIn->SWidth, x * 2, y * 2);
    ColorARGB c2 = *getPixel(renderBuffer, gifIn->SWidth, x * 2 + 1, y * 2);
    ColorARGB c3 = *getPixel(renderBuffer, gifIn->SWidth, x * 2, y * 2 + 1);
    ColorARGB c4 = *getPixel(renderBuffer, gifIn->SWidth, x * 2 + 1, y * 2 + 1);
    ColorARGB avgColor = computeAverage(c1, c2, c3, c4);

    // Search the color map for the best match.
    return findBestColor(colorMap, transparentColorIndex, avgColor);
}

ColorARGB GifTranscoder::computeAverage(ColorARGB c1, ColorARGB c2, ColorARGB c3, ColorARGB c4) {
    char avgAlpha = (char)(((int) ALPHA(c1) + (int) ALPHA(c2) +
                            (int) ALPHA(c3) + (int) ALPHA(c4)) / 4);
    char avgRed =   (char)(((int) RED(c1) + (int) RED(c2) +
                            (int) RED(c3) + (int) RED(c4)) / 4);
    char avgGreen = (char)(((int) GREEN(c1) + (int) GREEN(c2) +
                            (int) GREEN(c3) + (int) GREEN(c4)) / 4);
    char avgBlue =  (char)(((int) BLUE(c1) + (int) BLUE(c2) +
                            (int) BLUE(c3) + (int) BLUE(c4)) / 4);
    return MAKE_COLOR_ARGB(avgAlpha, avgRed, avgGreen, avgBlue);
}

GifByteType GifTranscoder::findBestColor(ColorMapObject* colorMap, int transparentColorIndex,
                                         ColorARGB targetColor) {
    // Return the transparent color if the average alpha is zero.
    char alpha = ALPHA(targetColor);
    if (alpha == 0 && transparentColorIndex != NO_TRANSPARENT_COLOR) {
        return transparentColorIndex;
    }

    GifByteType closestColorIndex = 0;
    int closestColorDistance = MAX_COLOR_DISTANCE;
    for (int i = 0; i < colorMap->ColorCount; i++) {
        // Skip the transparent color (we've already eliminated that option).
        if (i == transparentColorIndex) {
            continue;
        }
        ColorARGB indexedColor = gifColorToColorARGB(colorMap->Colors[i]);
        int distance = computeDistance(targetColor, indexedColor);
        if (distance < closestColorDistance) {
            closestColorIndex = i;
            closestColorDistance = distance;
        }
    }
    return closestColorIndex;
}

int GifTranscoder::computeDistance(ColorARGB c1, ColorARGB c2) {
    return SQUARE(RED(c1) - RED(c2)) +
           SQUARE(GREEN(c1) - GREEN(c2)) +
           SQUARE(BLUE(c1) - BLUE(c2));
}

ColorMapObject* GifTranscoder::getColorMap(GifFileType* gifIn) {
    if (gifIn->Image.ColorMap) {
        return gifIn->Image.ColorMap;
    }
    return gifIn->SColorMap;
}

ColorARGB GifTranscoder::getColorARGB(ColorMapObject* colorMap, int transparentColorIndex,
                                      GifByteType colorIndex) {
    if (colorIndex == transparentColorIndex) {
        return TRANSPARENT;
    }
    return gifColorToColorARGB(colorMap->Colors[colorIndex]);
}

ColorARGB GifTranscoder::gifColorToColorARGB(const GifColorType& color) {
    return MAKE_COLOR_ARGB(0xff, color.Red, color.Green, color.Blue);
}

GifFilesCloser::~GifFilesCloser() {
    if (mGifIn) {
        DGifCloseFile(mGifIn, NULL);
        mGifIn = NULL;
    }
    if (mGifOut) {
        EGifCloseFile(mGifOut, NULL);
        mGifOut = NULL;
    }
}

void GifFilesCloser::setGifIn(GifFileType* gifIn) {
    mGifIn = gifIn;
}

void GifFilesCloser::releaseGifIn() {
    mGifIn = NULL;
}

void GifFilesCloser::setGifOut(GifFileType* gifOut) {
    mGifOut = gifOut;
}

void GifFilesCloser::releaseGifOut() {
    mGifOut = NULL;
}

// JNI stuff

jboolean transcode(JNIEnv* env, jobject clazz, jstring filePath, jstring outFilePath) {
    const char* pathIn = env->GetStringUTFChars(filePath, JNI_FALSE);
    const char* pathOut = env->GetStringUTFChars(outFilePath, JNI_FALSE);

    GifTranscoder transcoder;
    int gifCode = transcoder.transcode(pathIn, pathOut);

    env->ReleaseStringUTFChars(filePath, pathIn);
    env->ReleaseStringUTFChars(outFilePath, pathOut);

    return (gifCode == GIF_OK);
}

const char *kClassPathName = "com/android/messaging/util/GifTranscoder";

JNINativeMethod kMethods[] = {
        { "transcodeInternal", "(Ljava/lang/String;Ljava/lang/String;)Z", (void*)transcode },
};

int registerNativeMethods(JNIEnv* env, const char* className,
                          JNINativeMethod* gMethods, int numMethods) {
    jclass clazz = env->FindClass(className);
    if (clazz == NULL) {
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    if (!registerNativeMethods(env, kClassPathName,
                               kMethods, sizeof(kMethods) / sizeof(kMethods[0]))) {
      return -1;
    }
    return JNI_VERSION_1_6;
}
