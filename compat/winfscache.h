#ifndef WINFSCACHE_H
#define WINFSCACHE_H

#include <windows.h>
#include <sys/stat.h>

typedef struct fscache_dirent {
	unsigned char d_type;
	const char *d_name;
	void *pfsentry;
} fscache_DIR;

extern fscache_DIR *fscache_opendir(const char *dir);
extern struct fscache_dirent *fscache_readdir(fscache_DIR *dd);
extern void fscache_closedir(fscache_DIR *dd);

extern int fscache_lstat(const char *file_name, struct stat *buf);
extern int fscache_enable(int enable);

#ifdef USE_WINFSCACHE

#undef DIR
#define DIR fscache_DIR
#undef dirent
#define dirent fscache_dirent

#define NO_D_INO_IN_DIRENT 1
#define _DIRENT_HAVE_D_TYPE 1

#define DT_UNKNOWN 0
#define DT_DIR 1
#define DT_REG 2
#define DT_LNK 3

#undef opendir
#define opendir fscache_opendir
#undef readdir
#define readdir fscache_readdir
#undef closedir
#define closedir fscache_closedir

#undef rewinddir
#define rewinddir fscache_rewinddir_not_implemented
#undef readdir_r
#define readdir_r fscache_readdir_r_not_implemented
#undef seekdir
#define seekdir fscache_seekdir_not_implemented
#undef telldir
#define telldir fscache_telldir_not_implemented

#undef _stati64
#define _stati64(a,b) fscache_lstat(a,b)
#undef lstat
#define lstat(a,b) fscache_lstat(a,b)

#endif

#endif
