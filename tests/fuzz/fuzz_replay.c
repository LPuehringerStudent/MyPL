/* Runs a fuzz target over files and directories without libFuzzer, so the
 * seed corpus and saved crash inputs are checked by any C compiler as part
 * of `make test`. Usage: fuzz_replay_<target> PATH...
 * Each PATH is a file or a directory whose regular files are all replayed. */
#include "fuzz_common.h"

#include <dirent.h>
#include <sys/stat.h>

static int replay_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "fuzz_replay: cannot open %s\n", path);
        return 0;
    }
    uint8_t* data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (size + n > capacity) {
            capacity = (size + n) * 2;
            uint8_t* grown = realloc(data, capacity);
            if (grown == NULL) {
                free(data);
                fclose(f);
                fprintf(stderr, "fuzz_replay: out of memory reading %s\n", path);
                return 0;
            }
            data = grown;
        }
        memcpy(data + size, chunk, n);
        size += n;
    }
    fclose(f);
    LLVMFuzzerTestOneInput(data != NULL ? data : (const uint8_t*)"", size);
    free(data);
    return 1;
}

int main(int argc, char** argv) {
    int replayed = 0;
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            fprintf(stderr, "fuzz_replay: no such path %s\n", argv[i]);
            failed = 1;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            if (replay_file(argv[i])) replayed++; else failed = 1;
            continue;
        }
        DIR* dir = opendir(argv[i]);
        if (dir == NULL) {
            fprintf(stderr, "fuzz_replay: cannot open directory %s\n", argv[i]);
            failed = 1;
            continue;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s", argv[i], entry->d_name);
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if (replay_file(path)) replayed++; else failed = 1;
        }
        closedir(dir);
    }
    printf("%s: replayed %d input(s)\n", argc > 0 ? argv[0] : "fuzz_replay", replayed);
    return failed;
}
