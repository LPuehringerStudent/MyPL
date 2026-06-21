#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "os.h"

int os_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

char* os_read_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        close(fd);
        return NULL;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return NULL;
    }

    char* buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        close(fd);
        return NULL;
    }

    ssize_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, buffer + total, (size_t)(size - total));
        if (n <= 0) {
            free(buffer);
            close(fd);
            return NULL;
        }
        total += n;
    }
    buffer[size] = '\0';
    close(fd);
    return buffer;
}

int os_open(const char* path) {
    return open(path, O_RDWR | O_CREAT, 0644);
}

int os_close(int fd) {
    return close(fd);
}

int os_read(int fd, void* buf, size_t count, off_t offset) {
    return (int)pread(fd, buf, count, offset);
}

int os_write(int fd, const void* buf, size_t count, off_t offset) {
    return (int)pwrite(fd, buf, count, offset);
}

int os_ftruncate(int fd, off_t length) {
    return ftruncate(fd, length);
}
