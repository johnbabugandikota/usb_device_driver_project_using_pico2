#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

int main(void)
{
    int fd;
    struct pollfd pfd;
    char buffer[256];
    int ret;
    ssize_t bytes;

    fd = open("/dev/usb_demo", O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;

    printf("Waiting for USB data using poll()...\n");

    while (1) {
        ret = poll(&pfd, 1, 3000);

        if (ret < 0) {
            perror("poll");
            break;
        }

        if (ret == 0) {
            printf("Timeout: no data in 3 seconds\n");
            continue;
        }

        if (pfd.revents & POLLIN) {
            bytes = read(fd, buffer, sizeof(buffer) - 1);

            if (bytes < 0) {
                perror("read");
                break;
            }

            buffer[bytes] = '\0';
            printf("Data ready (%zd bytes): %s\n", bytes, buffer);
        }

        if (pfd.revents & POLLHUP) {
            printf("Device disconnected\n");
            break;
        }
    }

    close(fd);
    return 0;
}
