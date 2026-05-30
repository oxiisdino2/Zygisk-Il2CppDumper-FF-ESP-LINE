/*
 * Android memory reader helper
 * Usage: readmem <pid> <address_hex> <size>
 * Reads process memory and outputs raw bytes to stdout
 * Compiled with: arm-linux-androideabi-gcc -static -o readmem readmem.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <pid> <address_hex> <size>\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    uintptr_t addr = strtoull(argv[2], NULL, 16);
    size_t size = atol(argv[3]);

    if (size == 0 || size > 4096) {
        fprintf(stderr, "Invalid size (max 4096)\n");
        return 1;
    }

    // Method 1: Use process_vm_readv (requires kernel 3.2+)
    struct iovec local_iov, remote_iov;
    char buf[4096];

    local_iov.iov_base = buf;
    local_iov.iov_len = size;
    remote_iov.iov_base = (void*)addr;
    remote_iov.iov_len = size;

    ssize_t nread = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);

    if (nread > 0) {
        // Write to stdout
        fwrite(buf, 1, nread, stdout);
        fflush(stdout);
        return 0;
    }

    // Method 2: Fallback to /proc/pid/mem
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s\n", path);
        return 1;
    }

    if (lseek(fd, addr, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "lseek failed\n");
        close(fd);
        return 1;
    }

    nread = read(fd, buf, size);
    close(fd);

    if (nread > 0) {
        fwrite(buf, 1, nread, stdout);
        fflush(stdout);
        return 0;
    }

    fprintf(stderr, "Read failed\n");
    return 1;
}
