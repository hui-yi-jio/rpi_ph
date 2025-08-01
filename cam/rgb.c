#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define DEVICE_NAME "/dev/video0"
#define WIDTH 1990
#define HEIGHT 1080
#define PIXEL_FORMAT V4L2_PIX_FMT_RGB24  // RGB24格式
#define BUFFER_COUNT 4
#define MAX_FRAMES 3  // 最大保存帧数（高分辨率内存消耗大）

// 计算RGB24图像大小
#define IMAGE_SIZE (WIDTH * HEIGHT * 3)

struct buffer {
    void *start;
    size_t length;
};

struct FrameData {
    uint8_t *data;
    size_t size;
    struct timespec timestamp;
};

static int fd = -1;
static struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;
static struct FrameData frames[MAX_FRAMES];
static int frame_count = 0;

void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

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
        fprintf(stderr, "Warning: Driver using format %c%c%c%c instead of RGB24\n",
                fmt.fmt.pix.pixelformat & 0xFF,
                (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
                (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
                (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    }
}

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

void start_capturing() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1)
        errno_exit("VIDIOC_STREAMON");
}

int read_frame(uint8_t **frame_data, size_t *frame_size) {
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
    
    *frame_data = (uint8_t*)buffers[buf.index].start;
    *frame_size = buf.bytesused;
    
    if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
        errno_exit("VIDIOC_QBUF");
    
    return buf.index;
}

void stop_capturing() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
        errno_exit("VIDIOC_STREAMOFF");
}

void uninit_device() {
    for (unsigned int i = 0; i < n_buffers; ++i) {
        if (munmap(buffers[i].start, buffers[i].length) == -1)
            errno_exit("munmap");
    }
    free(buffers);
}

void close_device() {
    if (close(fd) == -1)
        errno_exit("close");
    fd = -1;
}

void save_rgb_frame(const char *filename, const uint8_t *data, int width, int height) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Cannot open file");
        return;
    }
    
    // 写入PPM格式 (P6表示二进制RGB)
    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    fwrite(data, width * height * 3, 1, fp);
    fclose(fp);
    printf("Saved RGB frame to %s (%dx%d)\n", filename, width, height);
}

void analyze_rgb_data(const uint8_t *data, size_t size) {
    printf("\nRGB Data Analysis:\n");
    printf("  Expected size: %d bytes\n", IMAGE_SIZE);
    printf("  Actual size:   %zu bytes\n", size);
    
    if (size < IMAGE_SIZE) {
        printf("Warning: Frame is incomplete!\n");
    }
    
    // 分析图像中心区域
    const int sample_size = 100;
    const int start_x = WIDTH/2 - sample_size/2;
    const int start_y = HEIGHT/2 - sample_size/2;
    
    int r_sum = 0, g_sum = 0, b_sum = 0;
    int r_min = 255, g_min = 255, b_min = 255;
    int r_max = 0, g_max = 0, b_max = 0;
    
    for (int y = start_y; y < start_y + sample_size; y++) {
        for (int x = start_x; x < start_x + sample_size; x++) {
            int offset = (y * WIDTH + x) * 3;
            
            if (offset + 2 >= size) break;
            
            uint8_t r = data[offset];
            uint8_t g = data[offset + 1];
            uint8_t b = data[offset + 2];
            
            r_sum += r; g_sum += g; b_sum += b;
            
            if (r < r_min) r_min = r;
            if (r > r_max) r_max = r;
            if (g < g_min) g_min = g;
            if (g > g_max) g_max = g;
            if (b < b_min) b_min = b;
            if (b > b_max) b_max = b;
        }
    }
    
    int total_pixels = sample_size * sample_size;
    printf("Sample Area (center %dx%d):\n", sample_size, sample_size);
    printf("  R: avg=%.1f, min=%d, max=%d\n", (float)r_sum/total_pixels, r_min, r_max);
    printf("  G: avg=%.1f, min=%d, max=%d\n", (float)g_sum/total_pixels, g_min, g_max);
    printf("  B: avg=%.1f, min=%d, max=%d\n", (float)b_sum/total_pixels, b_min, b_max);
}

void capture_frames(int num_frames) {
    printf("Capturing %d RGB frames at %dx%d resolution...\n", num_frames, WIDTH, HEIGHT);
    printf("Estimated memory per frame: %.2f MB\n", (float)IMAGE_SIZE/(1024*1024));
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int frames_captured = 0;
    while (frames_captured < num_frames && frame_count < MAX_FRAMES) {
        uint8_t *frame_data;
        size_t frame_size;
        
        if (read_frame(&frame_data, &frame_size)) {
            // 分配内存存储帧
            frames[frame_count].data = malloc(frame_size);
            if (!frames[frame_count].data) {
                fprintf(stderr, "Memory allocation failed for frame %d\n", frame_count);
                break;
            }
            
            memcpy(frames[frame_count].data, frame_data, frame_size);
            frames[frame_count].size = frame_size;
            clock_gettime(CLOCK_MONOTONIC, &frames[frame_count].timestamp);
            
            printf("Frame %d captured: %zu bytes\n", frame_count, frame_size);
            
            if (frame_count == 0) {
                analyze_rgb_data(frames[frame_count].data, frame_size);
            }
            
            frame_count++;
            frames_captured++;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("\nCapture completed: %d frames in %.2f seconds (%.2f FPS)\n",
           frames_captured, elapsed, frames_captured / elapsed);
}

void save_all_frames() {
    for (int i = 0; i < frame_count; i++) {
        char filename[50];
        snprintf(filename, sizeof(filename), "frame_%dx%d_%d.ppm", WIDTH, HEIGHT, i);
        save_rgb_frame(filename, frames[i].data, WIDTH, HEIGHT);
    }
}

void free_frames() {
    for (int i = 0; i < frame_count; i++) {
        free(frames[i].data);
        frames[i].data = NULL;
        frames[i].size = 0;
    }
    frame_count = 0;
}

int main(int argc, char *argv[]) {
    // 打开设备
    fd = open(DEVICE_NAME, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        perror("Cannot open device");
        fprintf(stderr, "Please check:\n");
        fprintf(stderr, "1. Device exists: ls /dev/video*\n");
        fprintf(stderr, "2. Permissions: sudo usermod -a -G video $USER\n");
        fprintf(stderr, "3. Camera supports RGB24 format at %dx%d\n", WIDTH, HEIGHT);
        exit(EXIT_FAILURE);
    }
    
    // 设置格式
    set_format();
    
    // 初始化内存映射
    init_mmap();
    
    // 将缓冲区加入队列
    enqueue_buffers();
    
    // 开始捕获
    start_capturing();
    
    // 捕获帧
    int num_frames = 3;
    if (argc > 1) num_frames = atoi(argv[1]);
    if (num_frames <= 0 || num_frames > MAX_FRAMES) num_frames = 3;
    
    capture_frames(num_frames);
    
    // 保存帧
    save_all_frames();
    
    // 清理
    stop_capturing();
    uninit_device();
    close_device();
    free_frames();
    
    return 0;
}
