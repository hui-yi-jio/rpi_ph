#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_DEV "/dev/i2c-13"
#define DEV_ADDR 0x40  // 设备地址（根据实际设备修改）

int main() {
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        perror("I2C打开失败");
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, DEV_ADDR) < 0) {
        perror("I2C地址设置失败");
        close(fd);
        return 1;
    }

    // 写入数据示例
    uint8_t buf[2] = {0x01, 0x42};  // 寄存器地址 + 数据
    if (write(fd, buf, 2) != 2) {
        perror("I2C写入失败");
    }

    // 读取数据示例
    uint8_t reg = 0x01;  // 要读取的寄存器
    if (write(fd, &reg, 1) != 1) {
        perror("I2C寄存器设置失败");
    }
    uint8_t val;
    if (read(fd, &val, 1) != 1) {
        perror("I2C读取失败");
    }
    printf("读取值: 0x%02X\n", val);

    close(fd);
    return 0;
}
