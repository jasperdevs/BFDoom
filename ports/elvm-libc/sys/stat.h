#ifndef BRAINDOOM_ELVM_SYS_STAT_H
#define BRAINDOOM_ELVM_SYS_STAT_H

struct stat {
  int st_size;
};

int stat(const char *path, struct stat *buf);

#endif
