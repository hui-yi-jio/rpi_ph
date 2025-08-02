#include <opencv2/opencv.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>

using namespace cv;
using namespace std;

// 全局变量
atomic<bool> done(false);
atomic<int> frames_displayed(0);
atomic<int> frames_captured(0);
Mat current_frame;
mutex frame_mutex;

// 直接捕获MJPEG帧的线程
void capture_thread(int fd, double duration) {
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    
    // 设置视频格式为MJPEG
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 3264;
    fmt.fmt.pix.height = 2448;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置MJPEG格式失败");
        return;
    }
    
    // 分配缓冲区
    struct v4l2_requestbuffers req = {};
    req.count = 4;  // 使用4个缓冲区
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("请求缓冲区失败");
        return;
    }
    
    // 映射缓冲区
    vector<v4l2_buffer> buffers(req.count);
    vector<void*> buffer_pointers(req.count);
    
    for (size_t i = 0; i < req.count; ++i) {
        v4l2_buffer &buf = buffers[i];
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("查询缓冲区失败");
            return;
        }
        
        void *ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (ptr == MAP_FAILED) {
            perror("映射缓冲区失败");
            return;
        }
        
        buffer_pointers[i] = ptr;
        
        // 入队缓冲区
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("入队缓冲区失败");
            return;
        }
    }
    
    // 开始捕获
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("开始流失败");
        return;
    }
    
    int frame_count = 0;
    while (true) {
        // 检查超时
        timeval current_time;
        gettimeofday(&current_time, NULL);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                        (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
        if (elapsed >= duration) break;
        
        // 出队缓冲区
        v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("出队缓冲区失败");
            continue;
        }
        
        // 获取帧数据
        uchar* src = static_cast<uchar*>(buffer_pointers[buf.index]);
        vector<uchar> jpeg_data(src, src + buf.bytesused);
        
        // 使用硬件加速解码（如果可用）
        Mat frame;
        try {
            frame = imdecode(jpeg_data, IMREAD_COLOR );
        } catch (...) {
            cerr << "解码帧失败" << endl;
        }
        
        if (!frame.empty()) {
            // 更新当前帧（带锁保护）
            {
                lock_guard<mutex> lock(frame_mutex);
                current_frame = frame.clone();  // 克隆确保显示线程安全
            }
            frames_captured++;
        }
        
        // 重新入队缓冲区
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("重新入队缓冲区失败");
            break;
        }
        
        frame_count++;
    }
    
    // 停止流
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // 释放资源
    for (size_t i = 0; i < buffer_pointers.size(); i++) {
        munmap(buffer_pointers[i], buffers[i].length);
    }
    
    done = true;
}

// 显示线程函数
void display_thread() {
    // 创建显示窗口
    namedWindow("HighRes Preview", WINDOW_NORMAL);
    resizeWindow("HighRes Preview", 3264, 2448);  // 以较低的分辨率显示
    
    double fps = 0.0;
    auto last_fps_time = chrono::steady_clock::now();
    int fps_frame_count = 0;
    
    while (!done) {
        Mat display_frame;
        {
            lock_guard<mutex> lock(frame_mutex);
            if (!current_frame.empty()) {
                display_frame = current_frame;
            }
        }
        
        if (!display_frame.empty()) {
            // 在帧上显示FPS
            putText(display_frame, "FPS: " + to_string(fps), Point(20, 40), 
                    FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
            
            // 显示帧
            imshow("HighRes Preview", display_frame);
            frames_displayed++;
            
            // 计算FPS
            fps_frame_count++;
            auto now = chrono::steady_clock::now();
            chrono::duration<double> elapsed = now - last_fps_time;
            if (elapsed.count() >= 1.0) {
                fps = fps_frame_count / elapsed.count();
                fps_frame_count = 0;
                last_fps_time = now;
            }
        }
        
        // 处理键盘事件（按ESC退出）
        if (waitKey(1) == 27) {
            done = true;
            break;
        }
        
        // 避免过度占用CPU
//        this_thread::sleep_for(chrono::milliseconds(1));
    }
    
    destroyAllWindows();
}

int main() {
    // 打开摄像头设备
    const char* device = "/dev/video0";
    int fd = v4l2_open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("打开摄像头失败");
        return -1;
    }
    
    double duration = 60.0;  // 60秒
    
    printf("开始高分辨率捕获与显示（3264x2448）...\n");
    printf("按ESC键可提前退出\n");
    
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    // 启动捕获线程
    thread cap_thread(capture_thread, fd, duration);
    
    // 启动显示线程
    thread display_thread_obj(display_thread);
    
    // 等待线程完成
    cap_thread.join();
    display_thread_obj.join();
    
    gettimeofday(&end_time, NULL);
    double total_time = (end_time.tv_sec - start_time.tv_sec) + 
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    // 关闭设备
    v4l2_close(fd);
    
    // 输出结果
    printf("\n捕获完成！\n");
    printf("总时长: %.2f 秒\n", total_time);
    printf("捕获帧数: %d\n", frames_captured.load());
    printf("显示帧数: %d\n", frames_displayed.load());
    printf("平均捕获帧率: %.2f FPS\n", frames_captured.load() / total_time);
    printf("平均显示帧率: %.2f FPS\n", frames_displayed.load() / total_time);
    
    return 0;
}
