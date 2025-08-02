#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui_c.h>

using namespace cv;

int main() {
    // 创建存储目录
    system("mkdir -p captured_frames");
    
    // 打开摄像头
    VideoCapture cap(0);
    if (!cap.isOpened()) {
        printf("错误：无法打开摄像头\n");
        return -1;
    }
    
    // 设置分辨率
    cap.set(CAP_PROP_FRAME_WIDTH, 3264);
    cap.set(CAP_PROP_FRAME_HEIGHT, 2448);
    
    int frame_count = 0;
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    double duration = 60.0;  // 60秒
    
    printf("开始捕获帧（持续60秒）...\n");
    
    Mat frame;
    while (true) {
        // 检查是否超时
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                         (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
        
        if (elapsed >= duration) break;
        
        // 捕获帧
        cap >> frame;
        if (frame.empty()) {
            printf("错误：读取帧失败\n");
            break;
        }
        
        // 保存帧
        char filename[100];
        sprintf(filename, "captured_frames/frame_%04d.jpg", frame_count);
        imwrite(filename, frame);
        
        frame_count++;
    }
    
    gettimeofday(&end_time, NULL);
    double total_time = (end_time.tv_sec - start_time.tv_sec) + 
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    // 输出结果
    printf("\n捕获完成！\n");
    printf("总时长: %.2f 秒\n", total_time);
    printf("总帧数: %d\n", frame_count);
    printf("平均帧率: %.2f FPS\n", frame_count / total_time);
    printf("图片已保存至: captured_frames/\n");
    
    return 0;
}
