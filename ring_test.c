#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd;
    char buffer[17];
    ssize_t bytes;
    int total = 0;

    fd = open("/dev/usb_demo", O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Ring buffer wraparound test started\n");

    for (int i = 0; i < 30; i++) {

        bytes = read(fd, buffer, 16);

        if (bytes < 0) {
            perror("read");
            break;
        }

        buffer[bytes] = '\0';
        total += bytes;

        printf("Read #%02d: %2zd bytes | Total: %d | %s\n",
               i + 1, bytes, total, buffer);

        usleep(200000);
    }

    printf("\nTotal bytes read: %d\n", total);

    close(fd);
    return 0;
}
