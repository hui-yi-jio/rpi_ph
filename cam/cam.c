#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <jpeglib.h>

#define CAM_DEVICE "/dev/video0"
#define WIDTH 640
#define HEIGHT 480

// JPEG解码为RGB
void jpeg_to_rgb(unsigned char* jpeg_data, size_t size, uint8_t** rgb, int* width, int* height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_data, size);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    *width = cinfo.output_width;
    *height = cinfo.output_height;
    *rgb = malloc(*width * *height * 3); // RGB存储

    JSAMPROW row_pointer[1];
    while (cinfo.output_scanline < cinfo.output_height) {
        row_pointer[0] = *rgb + cinfo.output_scanline * *width * 3;
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
}

int main() {
    int fd = open(CAM_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("打开设备失败");
        return -1;
    }

    // 设置视频格式为MJPG
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置MJPG格式失败");
        close(fd);
        return -1;
    }

    // 申请缓冲区
    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("申请缓冲区失败");
        close(fd);
        return -1;
    }

    // 映射内存
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        perror("查询缓冲区失败");
        close(fd);
        return -1;
    }

    void* buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer == MAP_FAILED) {
        perror("内存映射失败");
        close(fd);
        return -1;
    }

    // 开始捕获
    if (ioctl(fd, VIDIOC_STREAMON, &buf.type) < 0) {
        perror("开启流失败");
        munmap(buffer, buf.length);
        close(fd);
        return -1;
    }

    // 获取一帧
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("入队缓冲区失败");
        goto cleanup;
    }

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("出队缓冲区失败");
        goto cleanup;
    }

    // 解码MJPG到RGB
    uint8_t* rgb_data = NULL;
    int rgb_width, rgb_height;
    jpeg_to_rgb((unsigned char*)buffer, buf.bytesused, &rgb_data, &rgb_width, &rgb_height);

    printf("V4L2捕获成功！RGB数组大小: %dx%d=%d字节\n", 
           rgb_width, rgb_height, rgb_width * rgb_height * 3);

    // 清理
    free(rgb_data);
cleanup:
    munmap(buffer, buf.length);
    close(fd);
    return 0;
}
// clang -o ./cam ./cam.c -ljpeg
