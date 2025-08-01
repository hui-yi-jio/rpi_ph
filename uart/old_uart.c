#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#define UART_DEV "/dev/ttyAMA0"  // 或 /dev/serial0

int main() {
    int fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("UART打开失败");
        return 1;
    }

    // 配置串口参数
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B115200);   // 波特率
    cfsetospeed(&options, B115200);

    options.c_cflag &= ~PARENB;     // 无校验
    options.c_cflag &= ~CSTOPB;     // 1位停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;         // 8位数据位
    options.c_cflag |= (CLOCAL | CREAD);

    tcsetattr(fd, TCSANOW, &options);

    // 发送数据
    char tx_buf[] = "Hello UART!";
    write(fd, tx_buf, sizeof(tx_buf));

    // 接收数据
    char rx_buf[32];
    int n = read(fd, rx_buf, sizeof(rx_buf));
    if (n > 0) {
        printf("收到: %.*s\n", n, rx_buf);
    }

    close(fd);
    return 0;
}
