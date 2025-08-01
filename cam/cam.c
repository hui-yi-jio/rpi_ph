#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define DEVICE_NAME "/dev/video0"
#define WIDTH 3264
#define HEIGHT 2448
#define PIXEL_FORMAT V4L2_PIX_FMT_YUYV  // YUV422 格式
#define BUFFER_COUNT 4
#define MAX_FRAMES 5  // 最大保存帧数（防止内存不足）

// 计算 YUV422 图像大小
#define IMAGE_SIZE (WIDTH * HEIGHT * 2)

// 用于存储完整帧数据的结构
struct FrameData {
    unsigned char *data;
    size_t size;
    struct timespec timestamp;
};

// 全局变量
static int fd = -1;
struct buffer {
    void *start;
    size_t length;
};
static struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;
static struct FrameData frames[MAX_FRAMES];
static int frame_count = 0;

// 错误处理
void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

// IOCTL 包装函数
int xioctl(int fh, int request, void *arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && errno == EINTR);
    
    if (r == -1) {
        fprintf(stderr, "ioctl %d failed: %d (%s)\n", request, errno, strerror(errno));
        return -1;
    }
    return r;
}

// 设置摄像头格式
void set_format() {
    struct v4l2_format fmt;
    CLEAR(fmt);
    
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = PIXEL_FORMAT;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
        errno_exit("VIDIOC_S_FMT");
    
    // 验证设置
    if (fmt.fmt.pix.width != WIDTH || fmt.fmt.pix.height != HEIGHT) {
        fprintf(stderr, "Warning: Driver adjusted resolution to %dx%d\n", 
                fmt.fmt.pix.width, fmt.fmt.pix.height);
    }
    
    if (fmt.fmt.pix.pixelformat != PIXEL_FORMAT) {
        fprintf(stderr, "Warning: Driver using format %c%c%c%c instead of YUYV\n",
                fmt.fmt.pix.pixelformat & 0xFF,
                (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
                (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
                (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    }
}

// 初始化内存映射
void init_mmap() {
    struct v4l2_requestbuffers req;
    CLEAR(req);
    
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        if (errno == EINVAL)
            fprintf(stderr, "Memory mapping not supported\n");
        else
            errno_exit("VIDIOC_REQBUFS");
    }
    
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory\n");
        exit(EXIT_FAILURE);
    }
    
    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }
    
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;
        
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
            errno_exit("VIDIOC_QUERYBUF");
        
        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        fd, buf.m.offset);
        
        if (buffers[n_buffers].start == MAP_FAILED)
            errno_exit("mmap");
    }
}

// 将缓冲区加入队列
void enqueue_buffers() {
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
            errno_exit("VIDIOC_QBUF");
    }
}

// 开始视频流
void start_capturing() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1)
        errno_exit("VIDIOC_STREAMON");
}

// 读取一帧
int read_frame(void **frame_data, size_t *frame_size) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            return 0; // 没有可用帧
        }
        errno_exit("VIDIOC_DQBUF");
    }
    
    *frame_data = buffers[buf.index].start;
    *frame_size = buf.bytesused;
    
    // 重新加入队列
    if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
        errno_exit("VIDIOC_QBUF");
    
    return buf.index;
}

// 停止视频流
void stop_capturing() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
        errno_exit("VIDIOC_STREAMOFF");
}

// 释放资源
void uninit_device() {
    for (unsigned int i = 0; i < n_buffers; ++i) {
        if (munmap(buffers[i].start, buffers[i].length) == -1)
            errno_exit("munmap");
    }
    free(buffers);
}

// 关闭设备
void close_device() {
    if (close(fd) == -1)
        errno_exit("close");
    fd = -1;
}

// 保存帧数据到文件（用于调试）
void save_frame_to_file(const char *filename, const void *data, size_t size) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Cannot open file");
        return;
    }
    
    fwrite(data, size, 1, fp);
    fclose(fp);
    printf("Saved frame to %s (%zu bytes)\n", filename, size);
}

// 分析 YUV 数据
void analyze_yuv_data(const unsigned char *data, size_t size) {
    printf("\nYUV Data Analysis:\n");
    printf("  Expected size: %d bytes\n", IMAGE_SIZE);
    printf("  Actual size:   %zu bytes\n", size);
    
    if (size < IMAGE_SIZE) {
        printf("Warning: Frame is incomplete!\n");
    }
    
    // 计算统计信息
    int y_sum = 0, u_sum = 0, v_sum = 0;
    int y_min = 255, u_min = 255, v_min = 255;
    int y_max = 0, u_max = 0, v_max = 0;
    
    // 只分析图像中心 100x100 区域（避免全图分析耗时）
    const int sample_size = 100;
    const int start_x = WIDTH/2 - sample_size/2;
    const int start_y = HEIGHT/2 - sample_size/2;
    
    for (int y = start_y; y < start_y + sample_size; y++) {
        for (int x = start_x; x < start_x + sample_size; x++) {
            int offset = y * WIDTH * 2 + x * 2;
            
            // YUYV 布局: [Y0, U, Y1, V] 每4字节对应2像素
            if (offset + 3 >= size) break;
            
            uint8_t y0 = data[offset];
            uint8_t u = data[offset + 1];
            uint8_t y1 = data[offset + 2];
            uint8_t v = data[offset + 3];
            
            // 更新统计
            y_sum += y0 + y1;
            u_sum += u;
            v_sum += v;
            
            if (y0 < y_min) y_min = y0;
            if (y0 > y_max) y_max = y0;
            if (u < u_min) u_min = u;
            if (u > u_max) u_max = u;
            if (v < v_min) v_min = v;
            if (v > v_max) v_max = v;
        }
    }
    
    int total_pixels = 2 * sample_size * sample_size;
    printf("Sample Area (center %dx%d):\n", sample_size, sample_size);
    printf("  Y: avg=%.1f, min=%d, max=%d\n", (float)y_sum/total_pixels, y_min, y_max);
    printf("  U: avg=%.1f, min=%d, max=%d\n", (float)u_sum/(sample_size*sample_size), u_min, u_max);
    printf("  V: avg=%.1f, min=%d, max=%d\n", (float)v_sum/(sample_size*sample_size), v_min, v_max);
}

// 捕获并存储帧
void capture_and_store(int num_frames) {
    printf("Capturing %d frames at %dx%d resolution...\n", num_frames, WIDTH, HEIGHT);
    printf("Estimated memory per frame: %.2f MB\n", (float)IMAGE_SIZE/(1024*1024));
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    int frames_captured = 0;
    while (frames_captured < num_frames && frame_count < MAX_FRAMES) {
        void *frame_data;
        size_t frame_size;
        
        int buf_index = read_frame(&frame_data, &frame_size);
        if (frame_size > 0) {
            // 分配内存存储帧数据
            frames[frame_count].data = malloc(frame_size);
            if (!frames[frame_count].data) {
                fprintf(stderr, "Memory allocation failed for frame %d\n", frame_count);
                break;
            }
            
            // 复制数据
            memcpy(frames[frame_count].data, frame_data, frame_size);
            frames[frame_count].size = frame_size;
            clock_gettime(CLOCK_MONOTONIC, &frames[frame_count].timestamp);
            
            printf("Frame %d captured: %zu bytes\n", frame_count, frame_size);
            
            // 分析第一帧
            if (frame_count == 0) {
                analyze_yuv_data(frames[frame_count].data, frame_size);
            }
            
            frame_count++;
            frames_captured++;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_time = (end_time.tv_sec - start_time.tv_sec) +
                       (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    
    printf("\nCapture completed: %d frames in %.2f seconds (%.2f FPS)\n",
           frames_captured, total_time, frames_captured / total_time);
}

// 释放帧内存
void free_frames() {
    for (int i = 0; i < frame_count; i++) {
        free(frames[i].data);
        frames[i].data = NULL;
        frames[i].size = 0;
    }
    frame_count = 0;
}

// 保存所有帧到文件
void save_all_frames() {
    for (int i = 0; i < frame_count; i++) {
        char filename[50];
        snprintf(filename, sizeof(filename), "frame_%dx%d_%d.yuv", WIDTH, HEIGHT, i);
        save_frame_to_file(filename, frames[i].data, frames[i].size);
    }
}

int main(int argc, char *argv[]) {
    // 打开设备
    fd = open(DEVICE_NAME, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        perror("Cannot open device");
        fprintf(stderr, "Please check:\n");
        fprintf(stderr, "1. Device exists: ls /dev/video*\n");
        fprintf(stderr, "2. Permissions: sudo usermod -a -G video $USER\n");
        fprintf(stderr, "3. Camera is connected and supports %dx%d resolution\n", WIDTH, HEIGHT);
        exit(EXIT_FAILURE);
    }
    
    // 设置摄像头格式
    set_format();
    
    // 初始化内存映射
    init_mmap();
    
    // 将缓冲区加入队列
    enqueue_buffers();
    
    // 开始捕获
    start_capturing();
    
    // 捕获并存储帧
    int num_frames = 3; // 默认捕获3帧
    if (argc > 1) num_frames = atoi(argv[1]);
    if (num_frames <= 0 || num_frames > MAX_FRAMES) num_frames = 3;
    
    capture_and_store(num_frames);
    
    // 保存帧到文件
    save_all_frames();
    
    // 清理
    stop_capturing();
    uninit_device();
    close_device();
    
    // 释放帧内存
    free_frames();
    
    return 0;
}
