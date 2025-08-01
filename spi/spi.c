#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEV "/dev/spidev0.0"

int main() {
    int fd = open(SPI_DEV, O_RDWR);
    if (fd < 0) {
        perror("SPI打开失败");
        return 1;
    }

    // 配置SPI参数
    uint8_t mode = SPI_MODE_0;  // CPOL=0, CPHA=0
    uint8_t bits = 8;           // 8位数据
    uint32_t speed = 500000;    // 500kHz
    uint8_t tx_buf[3] = {0x01, 0x80, 0x00};  // 发送数据
    uint8_t rx_buf[3] = {0};                 // 接收缓冲区
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    // 创建传输结构体
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = 3,               // 传输3字节
        .delay_usecs = 10,      // 延迟
        .speed_hz = speed,
        .bits_per_word = bits,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI传输失败");
    }

    printf("收到: %02X %02X %02X\n", rx_buf[0], rx_buf[1], rx_buf[2]);

    close(fd);
    return 0;
}
