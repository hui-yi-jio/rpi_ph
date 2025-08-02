#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cctype>
// 轮廓排序比较函数（从左到右）
bool sortContours(const std::vector<cv::Point>& c1, const std::vector<cv::Point>& c2) {
    cv::Rect rect1 = cv::boundingRect(c1);
    cv::Rect rect2 = cv::boundingRect(c2);
    return rect1.x < rect2.x;
}

// 预处理图像以增强数字区域
cv::Mat preprocessImage(const cv::Mat& input) {
    cv::Mat gray, blurred, clahe_out, edged;
    
    // 转换为灰度图
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    
    // 应用自适应直方图均衡化
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    clahe->apply(gray, clahe_out);
    
    // 高斯模糊减少噪声
    cv::GaussianBlur(clahe_out, blurred, cv::Size(5, 5), 0);
    
    // 边缘检测
    cv::Canny(blurred, edged, 30, 150);
    
    // 形态学操作（闭运算连接边缘）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edged, edged, cv::MORPH_CLOSE, kernel);
    
    return edged;
}

// 识别数字并返回结果
std::vector<std::pair<std::string, cv::Point>> recognizeDigits(
    cv::Mat& frame, cv::Mat& processed, tesseract::TessBaseAPI& ocr) {
    
    std::vector<std::pair<std::string, cv::Point>> results;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    
    // 查找轮廓
    cv::findContours(processed.clone(), contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // 过滤轮廓
    std::vector<std::vector<cv::Point>> digitContours;
    for (const auto& contour : contours) {
        cv::Rect rect = cv::boundingRect(contour);
        double area = cv::contourArea(contour);
        double aspectRatio = static_cast<double>(rect.width) / rect.height;
        
        // 根据面积和宽高比过滤可能的数字区域
        if (area > 300 && area < 10000 && aspectRatio > 0.2 && aspectRatio < 1.2) {
            digitContours.push_back(contour);
        }
    }
    
    // 按从左到右排序轮廓
    std::sort(digitContours.begin(), digitContours.end(), sortContours);
    
    // 处理每个可能的数字区域
    for (const auto& contour : digitContours) {
        cv::Rect rect = cv::boundingRect(contour);
        
        // 扩展区域以确保包含整个数字
        int padding = 8;
        rect.x = std::max(0, rect.x - padding);
        rect.y = std::max(0, rect.y - padding);
        rect.width = std::min(frame.cols - rect.x, rect.width + 2 * padding);
        rect.height = std::min(frame.rows - rect.y, rect.height + 2 * padding);
        
        // 提取ROI
        cv::Mat roi = frame(rect);
        cv::Mat grayRoi;
        cv::cvtColor(roi, grayRoi, cv::COLOR_BGR2GRAY);
        
        // 二值化
        cv::Mat binRoi;
        cv::threshold(grayRoi, binRoi, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
        
        // 调整大小以提高OCR识别率
        cv::Mat resizedRoi;
        cv::resize(binRoi, resizedRoi, cv::Size(100, 100), 0, 0, cv::INTER_AREA);
        
        // 使用Tesseract进行OCR识别
        ocr.SetImage(resizedRoi.data, resizedRoi.cols, resizedRoi.rows, 1, resizedRoi.step);
        char* text = ocr.GetUTF8Text();
        std::string digitText(text);
        
        // 清理文本
        digitText.erase(std::remove(digitText.begin(), digitText.end(), '\n'), digitText.end());
        digitText.erase(std::remove(digitText.begin(), digitText.end(), ' '), digitText.end());
        
        // 如果识别结果为单个数字，则保存结果
        if (!digitText.empty() && std::isdigit(static_cast<unsigned char>(digitText[0]))) {
            cv::Point center(rect.x + rect.width / 2, rect.y + rect.height / 2);
            results.push_back({digitText.substr(0, 1), center});
            
            // 在原始图像上绘制结果
            cv::rectangle(frame, rect, cv::Scalar(0, 255, 0), 2);
            cv::circle(frame, center, 5, cv::Scalar(0, 0, 255), -1);
            
            std::string label = digitText + " @(" + std::to_string(center.x) + "," + std::to_string(center.y) + ")";
            cv::putText(frame, label, cv::Point(rect.x, rect.y - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        }
        
        delete[] text;
    }
    
    return results;
}

// 屏幕坐标映射函数
cv::Point mapToScreen(const cv::Point& cameraPoint, const cv::Size& cameraRes, const cv::Size& screenRes) {
    int screenX = (cameraPoint.x * screenRes.width) / cameraRes.width;
    int screenY = (cameraPoint.y * screenRes.height) / cameraRes.height;
    return cv::Point(screenX, screenY);
}

int main() {
    // 初始化摄像头
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "无法打开摄像头！" << std::endl;
        return -1;
    }
    
    // 设置摄像头分辨率
    int camWidth = 1280;
    int camHeight = 720;
    cap.set(cv::CAP_PROP_FRAME_WIDTH, camWidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, camHeight);
    
    // 初始化Tesseract OCR
    tesseract::TessBaseAPI ocr;
    if (ocr.Init(nullptr, "eng", tesseract::OEM_LSTM_ONLY)) {
        std::cerr << "无法初始化Tesseract OCR！" << std::endl;
        return -1;
    }
    
    // 配置OCR参数
    ocr.SetPageSegMode(tesseract::PSM_SINGLE_CHAR); // 单字符模式
    ocr.SetVariable("tessedit_char_whitelist", "0123456789"); // 只识别数字
    
    // 设置屏幕分辨率（根据实际显示器修改）
    cv::Size screenRes(1920, 1080);
    
    // 创建窗口
    cv::namedWindow("Digit Recognition", cv::WINDOW_NORMAL);
    cv::resizeWindow("Digit Recognition", 800, 600);
    
    cv::Mat frame, processed;
    int frameCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        cap >> frame;
        if (frame.empty()) break;
        
        frameCount++;
        
        // 预处理图像
        processed = preprocessImage(frame);
        
        // 识别数字
        auto digits = recognizeDigits(frame, processed, ocr);
        
        // 显示检测到的数字及其位置
        for (const auto& digit : digits) {
            cv::Point screenPos = mapToScreen(digit.second, cv::Size(camWidth, camHeight), screenRes);
            std::cout << "检测到数字: " << digit.first 
                      << " | 摄像头位置: (" << digit.second.x << ", " << digit.second.y << ")"
                      << " | 屏幕位置: (" << screenPos.x << ", " << screenPos.y << ")\n";
        }
        
        // 计算并显示FPS
        auto currentTime = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0;
        double fps = frameCount / elapsed;
        
        if (elapsed > 1.0) {
            frameCount = 0;
            startTime = currentTime;
        }
        
        std::ostringstream fpsText;
        fpsText << "FPS: " << std::fixed << std::setprecision(1) << fps;
        cv::putText(frame, fpsText.str(), cv::Point(10, 30), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
        
        // 显示结果
        cv::imshow("Digit Recognition", frame);
        
        // 按ESC退出
        if (cv::waitKey(1) == 27) break;
    }
    
    // 清理资源
    ocr.End();
    cap.release();
    cv::destroyAllWindows();
    
    return 0;
}
