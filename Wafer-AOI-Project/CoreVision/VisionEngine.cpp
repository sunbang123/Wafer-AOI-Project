#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>

using namespace cv;
using namespace cv::dnn;

// C#에서 이 함수를 꺼내 쓸 수 있도록 꼬리표(__declspec)를 붙여줍니다.
extern "C" __declspec(dllexport) bool LoadWaferModel() {
    // 우리가 C# bin/Debug 폴더로 복사되게 만든 모델 파일 이름
    String modelPath = "wafer_defect_model.onnx";

    try {
        // AI 모델 읽기 시도
        Net net = readNetFromONNX(modelPath);

        // CPU 모드로 세팅
        net.setPreferableBackend(DNN_BACKEND_OPENCV);
        net.setPreferableTarget(DNN_TARGET_CPU);

        return true; // 성공하면 true 반환
    }
    catch (const cv::Exception& e) {
        // 실패하면 에러 메시지는 나중에 C# 콘솔 등에서 확인할 수 있습니다.
        return false; // 실패하면 false 반환
    }
}