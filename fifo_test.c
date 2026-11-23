#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd;
    char buffer[128];
    ssize_t bytes;
    int read_count = 0;

    fd = open("/dev/usb_demo", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("FIFO test started\n");
    sleep(5);

    while (read_count < 5) {
        bytes = read(fd, buffer, sizeof(buffer) - 1);

        if (bytes < 0) {
            perror("read");
            break;
        }

        buffer[bytes] = '\0';

        printf("\nRead #%d: %zd bytes\n", ++read_count, bytes);
        printf("%s\n", buffer);

        sleep(1);
    }

    close(fd);
    return 0;
}
