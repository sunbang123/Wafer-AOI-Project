#include "onnxruntime_c_api.h"

#include <Windows.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    constexpr int ModelImageSize = 64;
    const OrtApi* g_ort = nullptr;
    HMODULE g_ortModule = nullptr;
    OrtEnv* g_env = nullptr;
    OrtSession* g_session = nullptr;
    OrtMemoryInfo* g_memoryInfo = nullptr;
    std::string g_inputName;
    std::string g_outputName;
    std::mutex g_modelMutex;

    using OrtGetApiBaseProc = const OrtApiBase*(ORT_API_CALL*)();

    void ReleaseStatus(OrtStatus* status)
    {
        if (status != nullptr && g_ort != nullptr) {
            g_ort->ReleaseStatus(status);
        }
    }

    bool CheckStatus(OrtStatus* status)
    {
        if (status == nullptr) {
            return true;
        }

        ReleaseStatus(status);
        return false;
    }

    std::wstring Utf8ToWide(const char* text)
    {
        if (text == nullptr || text[0] == '\0') {
            return std::wstring();
        }

        const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (length <= 0) {
            return std::wstring();
        }

        std::wstring wide(length - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, &wide[0], length);
        return wide;
    }

    std::wstring GetDirectoryName(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    }

    std::wstring GetDefaultOnnxRuntimePath()
    {
        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        const std::wstring dllDirectory = GetDirectoryName(modulePath);
        const std::wstring projectRoot = GetDirectoryName(GetDirectoryName(dllDirectory));

        const std::vector<std::wstring> candidates = {
            dllDirectory + L"\\onnxruntime.dll",
            projectRoot + L"\\WaferUI\\bin\\Debug\\net8.0-windows\\runtimes\\win-x64\\native\\onnxruntime.dll",
            projectRoot + L"\\WaferUI\\bin\\Release\\net8.0-windows\\runtimes\\win-x64\\native\\onnxruntime.dll"
        };

        for (const std::wstring& candidate : candidates) {
            const DWORD attributes = GetFileAttributesW(candidate.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                return candidate;
            }
        }

        return L"onnxruntime.dll";
    }

    bool EnsureOrtApiLoaded()
    {
        if (g_ort != nullptr) {
            return true;
        }

        const std::wstring runtimePath = GetDefaultOnnxRuntimePath();
        g_ortModule = LoadLibraryW(runtimePath.c_str());
        if (g_ortModule == nullptr) {
            return false;
        }

        auto getApiBase = reinterpret_cast<OrtGetApiBaseProc>(GetProcAddress(g_ortModule, "OrtGetApiBase"));
        if (getApiBase == nullptr) {
            return false;
        }

        const OrtApiBase* apiBase = getApiBase();
        g_ort = apiBase->GetApi(ORT_API_VERSION);
        return g_ort != nullptr;
    }

    void ReleaseSessionState()
    {
        if (g_session != nullptr) {
            g_ort->ReleaseSession(g_session);
            g_session = nullptr;
        }

        g_inputName.clear();
        g_outputName.clear();
    }

    void ReleaseAllState()
    {
        ReleaseSessionState();

        if (g_memoryInfo != nullptr) {
            g_ort->ReleaseMemoryInfo(g_memoryInfo);
            g_memoryInfo = nullptr;
        }

        if (g_env != nullptr) {
            g_ort->ReleaseEnv(g_env);
            g_env = nullptr;
        }

        if (g_ortModule != nullptr) {
            FreeLibrary(g_ortModule);
            g_ortModule = nullptr;
            g_ort = nullptr;
        }
    }

    bool EnsureOrtEnvironment()
    {
        if (!EnsureOrtApiLoaded()) {
            return false;
        }

        if (g_env == nullptr && !CheckStatus(g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "CoreVision", &g_env))) {
            return false;
        }

        if (g_memoryInfo == nullptr &&
            !CheckStatus(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_memoryInfo))) {
            return false;
        }

        return true;
    }

    bool ReadSessionNames(OrtSession* session)
    {
        OrtAllocator* allocator = nullptr;
        if (!CheckStatus(g_ort->GetAllocatorWithDefaultOptions(&allocator))) {
            return false;
        }

        char* inputName = nullptr;
        char* outputName = nullptr;
        if (!CheckStatus(g_ort->SessionGetInputName(session, 0, allocator, &inputName))) {
            return false;
        }

        if (!CheckStatus(g_ort->SessionGetOutputName(session, 0, allocator, &outputName))) {
            g_ort->AllocatorFree(allocator, inputName);
            return false;
        }

        g_inputName = inputName;
        g_outputName = outputName;
        g_ort->AllocatorFree(allocator, inputName);
        g_ort->AllocatorFree(allocator, outputName);
        return true;
    }

    bool LoadModel(const char* modelPath)
    {
        if (modelPath == nullptr || modelPath[0] == '\0') {
            return false;
        }

        if (!EnsureOrtEnvironment()) {
            return false;
        }

        ReleaseSessionState();

        OrtSessionOptions* options = nullptr;
        if (!CheckStatus(g_ort->CreateSessionOptions(&options))) {
            return false;
        }

        CheckStatus(g_ort->SetIntraOpNumThreads(options, 1));
        CheckStatus(g_ort->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_BASIC));

        const std::wstring wideModelPath = Utf8ToWide(modelPath);
        OrtSession* session = nullptr;
        const bool created = CheckStatus(g_ort->CreateSession(g_env, wideModelPath.c_str(), options, &session));
        g_ort->ReleaseSessionOptions(options);

        if (!created || session == nullptr) {
            return false;
        }

        if (!ReadSessionNames(session)) {
            g_ort->ReleaseSession(session);
            return false;
        }

        g_session = session;
        return true;
    }

    float ColorDistanceSquared(float red, float green, float blue, float targetRed, float targetGreen, float targetBlue)
    {
        const float dr = red - targetRed;
        const float dg = green - targetGreen;
        const float db = blue - targetBlue;
        return (dr * dr) + (dg * dg) + (db * db);
    }

    float MapViridisWaferColorToModelValue(const cv::Vec3b& bgr)
    {
        struct ColorStop {
            float red;
            float green;
            float blue;
            float value;
        };

        static const ColorStop colors[] = {
            { 68.0f, 1.0f, 84.0f, 0.0f },
            { 32.0f, 144.0f, 140.0f, 0.5f },
            { 253.0f, 231.0f, 36.0f, 1.0f }
        };

        const float red = static_cast<float>(bgr[2]);
        const float green = static_cast<float>(bgr[1]);
        const float blue = static_cast<float>(bgr[0]);

        const ColorStop* best = &colors[0];
        float bestDistance = ColorDistanceSquared(red, green, blue, best->red, best->green, best->blue);

        for (const ColorStop& color : colors) {
            const float distance = ColorDistanceSquared(red, green, blue, color.red, color.green, color.blue);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &color;
            }
        }

        return best->value;
    }

    bool CreateInputTensorData(const char* imagePath, std::vector<float>& inputData)
    {
        cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
        if (image.empty()) {
            return false;
        }

        cv::Mat resized;
        cv::resize(image, resized, cv::Size(ModelImageSize, ModelImageSize));

        inputData.assign(ModelImageSize * ModelImageSize, 0.0f);
        for (int y = 0; y < ModelImageSize; ++y) {
            for (int x = 0; x < ModelImageSize; ++x) {
                inputData[(y * ModelImageSize) + x] = MapViridisWaferColorToModelValue(resized.at<cv::Vec3b>(y, x));
            }
        }

        return true;
    }

    float SoftmaxConfidence(const float* scores, size_t count, int bestIndex)
    {
        if (scores == nullptr || count == 0 || bestIndex < 0 || static_cast<size_t>(bestIndex) >= count) {
            return 0.0f;
        }

        const float maxValue = *std::max_element(scores, scores + count);
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += std::exp(static_cast<double>(scores[i] - maxValue));
        }

        return static_cast<float>(std::exp(static_cast<double>(scores[bestIndex] - maxValue)) / sum);
    }

    int PredictInternal(const char* imagePath, float* confidenceOut)
    {
        if (confidenceOut != nullptr) {
            *confidenceOut = 0.0f;
        }

        if (imagePath == nullptr || imagePath[0] == '\0') {
            return -1;
        }

        if (g_session == nullptr) {
            return -2;
        }

        std::vector<float> inputData;
        if (!CreateInputTensorData(imagePath, inputData)) {
            return -3;
        }

        const int64_t inputShape[] = { 1, 1, ModelImageSize, ModelImageSize };
        OrtValue* inputTensor = nullptr;
        if (!CheckStatus(g_ort->CreateTensorWithDataAsOrtValue(
            g_memoryInfo,
            inputData.data(),
            inputData.size() * sizeof(float),
            inputShape,
            4,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &inputTensor))) {
            return -4;
        }

        const char* inputNames[] = { g_inputName.c_str() };
        const char* outputNames[] = { g_outputName.c_str() };
        const OrtValue* inputValues[] = { inputTensor };
        OrtValue* outputTensor = nullptr;
        const bool ran = CheckStatus(g_ort->Run(
            g_session,
            nullptr,
            inputNames,
            inputValues,
            1,
            outputNames,
            1,
            &outputTensor));

        g_ort->ReleaseValue(inputTensor);

        if (!ran || outputTensor == nullptr) {
            return -4;
        }

        float* scores = nullptr;
        if (!CheckStatus(g_ort->GetTensorMutableData(outputTensor, reinterpret_cast<void**>(&scores)))) {
            g_ort->ReleaseValue(outputTensor);
            return -4;
        }

        OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
        size_t outputCount = 0;
        if (CheckStatus(g_ort->GetTensorTypeAndShape(outputTensor, &shapeInfo))) {
            CheckStatus(g_ort->GetTensorShapeElementCount(shapeInfo, &outputCount));
            g_ort->ReleaseTensorTypeAndShapeInfo(shapeInfo);
        }

        if (scores == nullptr || outputCount == 0) {
            g_ort->ReleaseValue(outputTensor);
            return -4;
        }

        const float* best = std::max_element(scores, scores + outputCount);
        const int bestIndex = static_cast<int>(best - scores);
        if (confidenceOut != nullptr) {
            *confidenceOut = SoftmaxConfidence(scores, outputCount, bestIndex);
        }

        g_ort->ReleaseValue(outputTensor);
        return bestIndex;
    }
}

extern "C" __declspec(dllexport) bool LoadWaferModel()
{
    std::lock_guard<std::mutex> lock(g_modelMutex);
    return LoadModel("wafer_defect_model.onnx");
}

extern "C" __declspec(dllexport) bool LoadWaferModelFromPath(const char* modelPath)
{
    std::lock_guard<std::mutex> lock(g_modelMutex);
    return LoadModel(modelPath);
}

extern "C" __declspec(dllexport) int PredictWaferDefect(const char* imagePath)
{
    std::lock_guard<std::mutex> lock(g_modelMutex);
    return PredictInternal(imagePath, nullptr);
}

extern "C" __declspec(dllexport) int PredictWaferDefectWithConfidence(const char* imagePath, float* confidenceOut)
{
    std::lock_guard<std::mutex> lock(g_modelMutex);
    return PredictInternal(imagePath, confidenceOut);
}

extern "C" __declspec(dllexport) void ReleaseWaferModel()
{
    std::lock_guard<std::mutex> lock(g_modelMutex);
    ReleaseAllState();
}
