#include <opencv2/opencv.hpp>
#include <fstream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

using namespace cv;
using namespace std;

// 直接捕获MJPEG帧的缓冲区
struct MJpegBuffer {
    vector<uchar> data;
    int frame_number;
    timeval capture_time;
};

// 全局变量
queue<MJpegBuffer> frame_queue;
mutex queue_mutex;
atomic<bool> done(false);
atomic<int> frames_saved(0);
atomic<int> frames_captured(0);

// 直接捕获MJPEG帧的线程
void capture_mjpeg_thread(int fd, double duration) {
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
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("请求缓冲区失败");
        return;
    }
    
    // 映射缓冲区
    vector<v4l2_buffer> buffers(req.count);
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
        
        buffers[i].m.userptr = reinterpret_cast<unsigned long>(ptr);
        
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
        MJpegBuffer mjpeg;
        uchar* src = reinterpret_cast<uchar*>(buf.m.userptr);
        mjpeg.data.assign(src, src + buf.bytesused);
        mjpeg.frame_number = frame_count;
        gettimeofday(&mjpeg.capture_time, NULL);
        
        // 添加到队列
        {
            lock_guard<mutex> lock(queue_mutex);
            frame_queue.push(move(mjpeg));
        }
        
        frame_count++;
        frames_captured++;
        
        // 重新入队缓冲区
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("重新入队缓冲区失败");
            break;
        }
    }
    
    // 停止流
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // 释放资源
    for (auto &buf : buffers) {
        munmap(reinterpret_cast<void*>(buf.m.userptr), buf.length);
    }
    
    done = true;
}

// 保存线程
void save_thread() {
    while (!done || !frame_queue.empty()) {
        MJpegBuffer mjpeg;
        bool has_frame = false;
        
        // 从队列获取帧
        {
            lock_guard<mutex> lock(queue_mutex);
            if (!frame_queue.empty()) {
                mjpeg = move(frame_queue.front());
                frame_queue.pop();
                has_frame = true;
            }
        }
        
        if (has_frame) {
            // 直接保存MJPEG数据
            char filename[100];
            sprintf(filename, "/dev/shm/captured_frames/frame_%04d.jpg", mjpeg.frame_number);
            
            ofstream file(filename, ios::binary);
            file.write(reinterpret_cast<const char*>(mjpeg.data.data()), mjpeg.data.size());
            
            frames_saved++;
        } else {
            // 队列为空时短暂休眠
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }
}

int main() {
    // 创建内存文件系统目录
    system("mkdir -p /dev/shm/captured_frames");
    
    // 打开摄像头设备
    const char* device = "/dev/video0";
    int fd = v4l2_open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("打开摄像头失败");
        return -1;
    }
    
    double duration = 60.0;  // 60秒
    
    printf("开始高分辨率捕获（3264x2448）...\n");
    
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    // 启动捕获线程
    thread cap_thread(capture_mjpeg_thread, fd, duration);
    
    // 启动保存线程
    thread save_thread1(save_thread);
    
    // 等待线程完成
    cap_thread.join();
    save_thread1.join();
    
    gettimeofday(&end_time, NULL);
    double total_time = (end_time.tv_sec - start_time.tv_sec) + 
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    // 关闭设备
    v4l2_close(fd);
    
    // 将文件从内存文件系统移动到存储
    system("mv /dev/shm/captured_frames captured_frames");
    
    // 输出结果
    printf("\n捕获完成！\n");
    printf("总时长: %.2f 秒\n", total_time);
    printf("捕获帧数: %d\n", frames_captured.load());
    printf("保存帧数: %d\n", frames_saved.load());
    printf("平均帧率: %.2f FPS\n", frames_captured.load() / total_time);
    printf("图片已保存至: captured_frames/\n");
    
    return 0;
}
