#ifndef OS_H
#define OS_H

#include <stddef.h>
#include <sys/types.h>

char* os_read_file(const char* path);
int os_file_exists(const char* path);
int os_write_file(const char* path, const char* contents, size_t len);

int os_open(const char* path);
int os_close(int fd);
int os_read(int fd, void* buf, size_t count, off_t offset);
int os_write(int fd, const void* buf, size_t count, off_t offset);
int os_ftruncate(int fd, off_t length);

#endif
