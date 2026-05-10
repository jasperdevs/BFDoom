#ifndef BRAINDOOM_ELVM_UNISTD_H
#define BRAINDOOM_ELVM_UNISTD_H

int close(int fd);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int open(const char *path, int flags, ...);

#endif
