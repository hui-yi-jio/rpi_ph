#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 打开摄像头设备 (根据实际情况调整设备号)
    cv::VideoCapture cap("/dev/video0", cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "无法打开摄像头" << std::endl;
        return -1;
    }

    // 设置MJPG格式和分辨率
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

     // 捕获一帧
    cv::Mat frame;
    if (!cap.read(frame)) {
        fprintf(stderr, "捕获帧失败\n");
        return -1;
    }

    // 检查是否为MJPG格式（通常为BGR或YUV）
    if (frame.type() != CV_8UC3) {
        fprintf(stderr, "未获得BGR格式帧，请检查摄像头设置\n");
        return -1;
    }

    // 转换BGR到RGB
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);

    // 将RGB数据存储到连续数组
    std::vector<uint8_t> rgb_array(rgb_frame.total() * 3);
    memcpy(rgb_array.data(), rgb_frame.data, rgb_frame.total() * 3);

    printf("OpenCV捕获成功！RGB数组大小: %zu字节\n", rgb_array.size());
    cap.release();
    return 0;
}
// clang++ cam.cpp -o opencv_capture \
                                     -I/usr/include/opencv4 \
                                     -lopencv_core \
                                     -lopencv_highgui \
                                     -lopencv_videoio \
                                     -lopencv_imgcodecs \
                                     -lopencv_imgproc \
                                     -lopencv_video

