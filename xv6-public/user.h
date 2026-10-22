struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// Kernel logging
struct klog_entry {
  unsigned int seq;
  unsigned int timestamp_hi;
  unsigned int timestamp_lo;
  unsigned int cpu;
  unsigned int pid;
  unsigned int level;
  char msg[64];
};

#define KLOG_SUB_CORE 0x01
#define KLOG_SUB_PROC 0x02
#define KLOG_SUB_FS   0x04
#define KLOG_SUB_MEM  0x08
#define KLOG_SUB_ALL  0xFF

#define KLOG_CRASH_MAGIC 0x4B4C4F47
struct klog_crash_dump {
  unsigned int magic;
  char panic_msg[64];
  unsigned int timestamp_hi;
  unsigned int timestamp_lo;
  unsigned int cpu;
  unsigned int entry_count;
  struct klog_entry entries[4];
};

int getklog(struct klog_entry*, int);
int setklogmask(unsigned int);
int getklogmask(void);
int getcrashdump(struct klog_crash_dump*);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
