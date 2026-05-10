#ifndef BRAINDOOM_ELVM_STDIO_H
#define BRAINDOOM_ELVM_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF -1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef char FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int getchar(void);
int putchar(int c);
int puts(const char *p);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE *fp, const char *fmt, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int fileno(FILE *fp);
FILE *fopen(const char *filename, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fflush(FILE *fp);
int fputs(const char *s, FILE *fp);
int fgetc(FILE *fp);
int getc(FILE *fp);
int ungetc(int c, FILE *fp);
int fputc(int c, FILE *fp);
int putc(int c, FILE *fp);
char *fgets(char *s, int size, FILE *fp);
int ftell(FILE *fp);
int fseek(FILE *fp, int offset, int whence);

#endif
