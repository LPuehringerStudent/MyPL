#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "os.h"

int os_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int os_write_file(const char* path, const char* contents, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, contents + total, len - total);
        if (n < 0) {
            close(fd);
            return 0;
        }
        total += (size_t)n;
    }
    close(fd);
    return 1;
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

int os_is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

int os_mkdir(const char* path) {
    return mkdir(path, 0755) == 0;
}

int os_list_dir(const char* path, char*** out_names, int* out_count) {
    if (out_names == NULL || out_count == NULL) return 0;
    *out_names = NULL;
    *out_count = 0;
    DIR* dir = opendir(path);
    if (dir == NULL) return 0;

    int capacity = 8;
    int count = 0;
    char** names = malloc(sizeof(char*) * (size_t)capacity);
    if (names == NULL) {
        closedir(dir);
        return 0;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (count >= capacity) {
            capacity *= 2;
            char** new_names = realloc(names, sizeof(char*) * (size_t)capacity);
            if (new_names == NULL) {
                for (int i = 0; i < count; i++) free(names[i]);
                free(names);
                closedir(dir);
                return 0;
            }
            names = new_names;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) {
            for (int i = 0; i < count; i++) free(names[i]);
            free(names);
            closedir(dir);
            return 0;
        }
        count++;
    }
    closedir(dir);
    *out_names = names;
    *out_count = count;
    return 1;
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
