#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>
#include <mutex>

using namespace cv;
using namespace cv::dnn;

namespace {
    Net g_waferNet;
    std::mutex g_modelMutex;

    bool EnsureModelLoaded()
    {
        if (!g_waferNet.empty()) {
            return true;
        }

        String modelPath = "wafer_defect_model.onnx";
        g_waferNet = readNetFromONNX(modelPath);
        g_waferNet.setPreferableBackend(DNN_BACKEND_OPENCV);
        g_waferNet.setPreferableTarget(DNN_TARGET_CPU);

        return !g_waferNet.empty();
    }
}

// Exported for C# P/Invoke.
extern "C" __declspec(dllexport) bool LoadWaferModel() {
    try {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        return EnsureModelLoaded();
    }
    catch (const cv::Exception&) {
        return false;
    }
}

// Returns the predicted defect class index for the image file path.
extern "C" __declspec(dllexport) int PredictWaferDefect(const char* imagePath) {
    if (imagePath == nullptr || imagePath[0] == '\0') {
        return -1; // Empty image path.
    }

    try {
        std::lock_guard<std::mutex> lock(g_modelMutex);

        if (!EnsureModelLoaded()) {
            return -2; // Model load failed.
        }

        Mat image = imread(imagePath, IMREAD_COLOR);
        if (image.empty()) {
            return -3; // Image read failed.
        }

        Mat resized;
        resize(image, resized, Size(64, 64));

        Mat blob = blobFromImage(resized, 1.0 / 255.0, Size(64, 64), Scalar(), true, false);

        g_waferNet.setInput(blob);
        Mat output = g_waferNet.forward();

        Mat scores = output.reshape(1, 1);
        Point classIdPoint;
        double confidence;
        minMaxLoc(scores, nullptr, &confidence, nullptr, &classIdPoint);

        return classIdPoint.x;
    }
    catch (const cv::Exception&) {
        return -4; // OpenCV error.
    }
    catch (...) {
        return -5; // Unknown error.
    }
}
