#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <errno.h>

#define UART_DEV "/dev/ttyAMA0"

int main() {
    int fd = open(UART_DEV, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("UART打开失败");
        return 1;
    }
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag &= ~PARENB;     // 无校验
    options.c_cflag &= ~CSTOPB;     // 1位停止位
    options.c_cflag &= ~CSIZE;      // 清除数据位设置
    options.c_cflag |= CS8;         // 8位数据位
    
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("UART配置失败");
        close(fd);
        return 1;
    }
    
    // 清空输入输出缓冲区
    tcflush(fd, TCIOFLUSH);
    
    // 发送数据
    const char *tx_msg = "Hello UART!\n";
    int tx_len = strlen(tx_msg);
    int bytes_sent = write(fd, tx_msg, tx_len);
    if (bytes_sent < 0) {
        perror("UART发送失败");
    } else {
        printf("成功发送 %d 字节: %s", bytes_sent, tx_msg);
    }
    char rx_buf[256];
    while (1) {
        int bytes_read = read(fd, rx_buf, sizeof(rx_buf) - 1);
        if (bytes_read > 0) {
            rx_buf[bytes_read] = '\0'; // 添加字符串结束符
            printf("接收到 %d 字节: %s\n", bytes_read, rx_buf);
        }
    }
    
    close(fd);
    return 0;
}
