#include "../git-compat-util.h"
#include "../cache.h"
#include "../hashmap.h"

#include <stdlib.h>
#include <stdio.h>
#include <direct.h>

static inline double perf_ticks()
{
	return 0;
}

static double t_create_list = 0;
static double t_process_changes = 0;

static void print_performance()
{
	fprintf(stderr, "process changes: %f ms\n", t_process_changes);
	fprintf(stderr, "create list: %f ms\n", t_create_list);
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	ULARGE_INTEGER ul;
	ul.LowPart = ft->dwLowDateTime;
	ul.HighPart = ft->dwHighDateTime;
	ul.QuadPart /= 10000000;
	ul.QuadPart -= 11644473600LL;
	return (time_t) ul.QuadPart;
}

static inline int file_attr_to_st_mode(DWORD attr)
{
	int fMode = S_IREAD;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		fMode |= S_IFDIR;
	else
		fMode |= S_IFREG;
	if (!(attr & FILE_ATTRIBUTE_READONLY))
		fMode |= S_IWRITE;
	return fMode;
}

static int normalize(char *dst, const char *src, int forDir)
{
	int len;
	if (normalize_path_copy(dst, src)) {
		errno = EINVAL;
		return -1;
	}

	len = strlen(dst);
	if (forDir) {
		// for directory: append '/' if missing (except if empty)
		if (len && dst[len - 1] != '/')
			dst[len++] = '/';
	} else {
		// for regular file: strip '/' if present
		if (!len) {
			errno = ENOENT;
			return -1;
		}
		if (dst[len - 1] == '/')
			dst[--len] = 0;
	}
	return len;
}

/* TODO use normal buffers */
#define MAX_PATH_U ((4 * MAX_PATH + 1) * sizeof(char))
#define MAX_PATH_W ((2 * MAX_PATH + 1) * sizeof(wchar_t))

typedef struct pathbuf {
	union {
		char c[MAX_PATH_U];
		wchar_t w[MAX_PATH_W];
	};
} pathbuf;

typedef struct fsentry fsentry;
typedef struct fscache fscache;

struct fsentry {
	hashmap_entry hash;
	fsentry *next;
	fsentry *list;
	const char *path;
	const char *name;
	unsigned short len;
	unsigned char modcnt;
	unsigned char refcnt;
	mode_t st_mode;
	off64_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
};

struct fscache {
	hashmap map;
	const wchar_t root[MAX_PATH_W];
	HANDLE hdir;
	OVERLAPPED overlapped;
	unsigned int bufsize;
	DWORD buffer[1];
};

static int fsentry_cmp(const fsentry *fse1, const fsentry *fse2)
{
	return strcasecmp(fse1->path, fse2->path);
}

static void fsentry_init(fsentry *fse, const char *path)
{
	fse->path = path;
	hashmap_entry_init(&fse->hash, strihash(fse->path));
}

static fsentry *fsentry_alloc(const char *path, size_t len)
{
	fsentry *fse = (fsentry*) xmalloc(sizeof(fsentry) + len + 1);
	fse->name = fse->path = ((char*) fse) + sizeof(fsentry);
	fse->len = len;
	memcpy((void*) fse->path, path, len + 1);
	hashmap_entry_init(&fse->hash, strihash(fse->path));
	fse->list = fse;
	fse->next = NULL;
	fse->refcnt = fse->modcnt = 0;
	return fse;
}

static fsentry *fsentry_create(fsentry *list, const WIN32_FIND_DATAW *fdata)
{
	pathbuf buf;
	int len;
	fsentry *fse;
	memcpy(buf.c, list->path, list->len);
	len = xwcstoutf(buf.c + list->len, fdata->cFileName, MAX_PATH - list->len);

	fse = fsentry_alloc(buf.c, list->len + len);

	fse->list = list;
	fse->name = fse->path + list->len;
	fse->len = len;

	fse->st_mode = file_attr_to_st_mode(fdata->dwFileAttributes);
	fse->st_size = fdata->nFileSizeLow | (((off64_t) (fdata->nFileSizeHigh))
			<< 32);
	if (fse->st_size < 0 || fse->st_size >= 0x100000000LL) {
		printf("st_size > 4G: %I64u\n", fse->st_size);
		fse->st_size = 0;
	}
	fse->st_atime = filetime_to_time_t(&(fdata->ftLastAccessTime));
	fse->st_mtime = filetime_to_time_t(&(fdata->ftLastWriteTime));
	fse->st_ctime = filetime_to_time_t(&(fdata->ftCreationTime));

	return fse;
}

/**
 * dir is expected to be empty or /-terminated
 */
static fsentry *fsentry_createlist(const char *dir)
{
	double start = perf_ticks();
	int len;
	WIN32_FIND_DATAW fdata;
	HANDLE handle;
	fsentry *list, **phead;
	pathbuf buf;
	/* open find file handle */
	len = xutftowcs(buf.w, dir, MAX_PATH);
	wcscpy(buf.w + len, L"*");

	handle = FindFirstFileW(buf.w, &fdata);
	if (handle == INVALID_HANDLE_VALUE) {
		//printf("Error in FindFirstFile for %s: %lu\n", dir, GetLastError());
		errno = err_win_to_posix(GetLastError());
		return NULL;
	}

	/* allocate object to hold directory listing */
	list = fsentry_alloc(dir, strlen(dir));
	list->refcnt = 1;

	/* walk directory -> build linked list */
	phead = &list->next;
	do {
		*phead = fsentry_create(list, &fdata);
		phead = &(*phead)->next;
	} while (FindNextFileW(handle, &fdata));
	FindClose(handle);

	t_create_list += perf_ticks() - start;
	return list;
}

static void fsentry_freelist(fsentry *e)
{
	e = e->list;
	if (--(e->refcnt) > 0)
		return;

	while (e) {
		fsentry *next = e->next;
		free(e);
		e = next;
	}
}

static void fscache_queue_changes(fscache *fsc);

static void fscache_add(fscache *fsc, fsentry *fse)
{
	fse = fse->list;
	while (fse) {
		hashmap_put(&fsc->map, &fse->hash);
		fse = fse->next;
	}
}

static fsentry *fscache_remove(fscache *fsc, fsentry *fse)
{
	fsentry *e;
	fse = (fsentry*) hashmap_remove(&fsc->map, &fse->hash);
	if (!fse)
		return NULL;

	e = fse->list;
	while (e) {
		hashmap_remove(&fsc->map, &e->hash);
		e = e->next;
	}
	return fse->list;
}

static void fscache_clear(fscache *fsc)
{
	hashmap_iter iter;
	fsentry *e = (fsentry*) hashmap_iter_first(&fsc->map, &iter);
	while (e) {
		fscache_remove(fsc, e);
		e = (fsentry*) hashmap_iter_first(&fsc->map, &iter);
	}

	if (fsc->map.size > 0)
		printf("still some objects in map!");
}

#define READ_DIR_CHG_BUFFER 16384

static fscache *fscache_create(const wchar_t *root)
{
	fscache *fsc;
	//printf("create fscache %S\n", root);
	fsc = (fscache*) xmalloc(sizeof(fscache) + READ_DIR_CHG_BUFFER);

	hashmap_init(&fsc->map, (hashmap_cmp_fn) fsentry_cmp, 0);

	wcscpy((wchar_t*) fsc->root, root);
	fsc->bufsize = READ_DIR_CHG_BUFFER;
	fsc->overlapped.hEvent = (HANDLE) fsc;

	fsc->hdir = CreateFileW(root, FILE_LIST_DIRECTORY, FILE_SHARE_READ
			| FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	if (fsc->hdir == INVALID_HANDLE_VALUE)
		// TODO error handling
		printf("hdir == INVALID_HANDLE_VALUE\n");

	// initial call to ReadDirectoryChangesW to init OS buffer
	fscache_queue_changes(fsc);

	return fsc;
}

static void fscache_close(fscache *fsc)
{
	//printf("close fscache %S\n", fsc->root);
	HANDLE h = fsc->hdir;
	fsc->hdir = INVALID_HANDLE_VALUE;
	CloseHandle(h);
	SleepEx(0, TRUE);

	fscache_clear(fsc);
	hashmap_free(&fsc->map, NULL);

	free(fsc);
}

static VOID CALLBACK fscache_process_changes(DWORD dwErrorCode,
		DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	double start = perf_ticks();
	FILE_NOTIFY_INFORMATION *pfni;
	fscache *fsc;
	//puts("> fscache_process_changes");
	fsc = (fscache*) lpOverlapped->hEvent;
	if (dwErrorCode != NO_ERROR) {
		printf("Error in directory change handler: %lu\n", dwErrorCode);
		fscache_queue_changes(fsc);
		return;
	}

	pfni = (FILE_NOTIFY_INFORMATION*) fsc->buffer;
	if (dwNumberOfBytesTransfered) {
		for (;;) {
			pathbuf buf1, buf2;
			fsentry key, *e;
			int len = WideCharToMultiByte(CP_UTF8, 0, pfni->FileName,
					pfni->FileNameLength / sizeof(wchar_t), buf1.c,
					sizeof(buf1.c), NULL, NULL);
			buf1.c[len] = 0;
			len = normalize(buf2.c, buf1.c, 0);

			//printf("FileNotification %lu for %s -> remove %s\n", pfni->Action,
			//buf1.c, buf2.c);

			fsentry_init(&key, buf2.c);
			e = (fsentry*) hashmap_get(&fsc->map, &key.hash);
			if (!e) {
				while (len && buf2.c[len - 1] != '/')
					len--;
				buf2.c[len] = 0;
				fsentry_init(&key, buf2.c);
				e = (fsentry*) hashmap_get(&fsc->map, &key.hash);
			}

			if (e) {
				e = e->list;
				e->modcnt = 1;
			}

			if (!pfni->NextEntryOffset)
				break;
			pfni = (FILE_NOTIFY_INFORMATION*) (((DWORD) pfni)
					+ pfni->NextEntryOffset);
		}
	}
	fscache_queue_changes(fsc);
	//puts("< fscache_process_changes");
	t_process_changes += perf_ticks() - start;
}

static void fscache_queue_changes(fscache *fsc)
{
	if (fsc->hdir == INVALID_HANDLE_VALUE)
		return;
	if (!ReadDirectoryChangesW(fsc->hdir, fsc->buffer, fsc->bufsize, TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
					| FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE
					| FILE_NOTIFY_CHANGE_LAST_WRITE
					| FILE_NOTIFY_CHANGE_CREATION, NULL, &fsc->overlapped,
			fscache_process_changes))
		printf("ReadDirectoryChangesW failed: %lu\n", GetLastError());
}

void test_fscache(const char *dir)
{
	pathbuf buf;
	int i;
	xutftowcs(buf.w, dir, MAX_PATH);
	fscache_create(buf.w);

	for (i = 0; i < 30; i++) {
		SleepEx(1000, TRUE);
	}
}

static fscache *current_fscache = NULL;
static CRITICAL_SECTION mutex;
static int initialized = 0;

static void fscache_init()
{
	if (!initialized) {
		InitializeCriticalSection(&mutex);
		if (0)
			atexit(print_performance);
		initialized = 1;
	}
}

static fscache *fscache_get(const char *path)
{
	if (is_absolute_path(path))
		return NULL;

	fscache_init();
	EnterCriticalSection(&mutex);
	if (!current_fscache) {
		pathbuf buf;
		GetCurrentDirectoryW(MAX_PATH, buf.w);
		current_fscache = fscache_create(buf.w);
	}
	SleepEx(0, TRUE);
	LeaveCriticalSection(&mutex);

	return current_fscache;
}

extern int fscache_chdir(const char *path)
{
	//printf("chdir: %s\n", path);
	pathbuf buf;
	int result;
	fscache *oldcache;
	xutftowcs(buf.w, path, MAX_PATH);
	result = _wchdir(buf.w);

	fscache_init();
	EnterCriticalSection(&mutex);
	oldcache = current_fscache;
	current_fscache = NULL;
	if (oldcache)
		fscache_close(oldcache);
	LeaveCriticalSection(&mutex);
	return result;
}

static fsentry *fscache_getlist(fscache *fsc, const char *dir)
{
	fsentry key, *list;
	if (!fsc)
		return fsentry_createlist(dir);

	/* lookup in cache */
	fsentry_init(&key, dir);
	EnterCriticalSection(&mutex);
	list = (fsentry*) hashmap_get(&fsc->map, &key.hash);
	if (!list) {
		LeaveCriticalSection(&mutex);
		list = fsentry_createlist(dir);
		if (list == NULL)
			return NULL;

		EnterCriticalSection(&mutex);
		fscache_add(fsc, list);
	}

	list->refcnt++;
	LeaveCriticalSection(&mutex);
	return list;
}

#define MODCNT_THRESHOLD 8
int fscache_lstat(const char *filename, struct stat *st)
{
//printf("lstat %s\n", filename);
	fsentry key, *e, *list;
	fscache *fsc = fscache_get(filename);
	if (!fsc)
		return mingw_lstat(filename, st);

	fsentry_init(&key, filename);
	EnterCriticalSection(&mutex);
	e = (fsentry*) hashmap_get(&fsc->map, &key.hash);
	LeaveCriticalSection(&mutex);
	if (e && e != e->list) {
		if (e->list->modcnt) {
			e->list->modcnt++;
			if (e->list->modcnt > MODCNT_THRESHOLD) {
				EnterCriticalSection(&mutex);
				fscache_remove(fsc, e);
				fsentry_freelist(e);
				LeaveCriticalSection(&mutex);
				e = NULL;
			} else {
				return mingw_lstat(filename, st);
			}
		}
	}
	if (!e || e == e->list) {
		pathbuf buf;
		char ctmp;
		int len = normalize(buf.c, filename, 0);
		if (len < 0) {
			return mingw_lstat(filename, st);
		}

		// make sure parent directory is in cache
		while (len && buf.c[len - 1] != '/')
			len--;
		ctmp = buf.c[len];
		buf.c[len] = 0;
		list = fscache_getlist(fsc, buf.c);
		if (!list) {
			errno = ENOENT;
			return -1;
		}
		EnterCriticalSection(&mutex);
		fsentry_freelist(list);
		LeaveCriticalSection(&mutex);
		if (list->modcnt) {
			list->modcnt++;
			if (list->modcnt > MODCNT_THRESHOLD) {
				EnterCriticalSection(&mutex);
				fscache_remove(fsc, list);
				fsentry_freelist(list);
				LeaveCriticalSection(&mutex);
				list = fscache_getlist(fsc, buf.c);
				if (!list) {
					errno = ENOENT;
					return -1;
				}
				EnterCriticalSection(&mutex);
				fsentry_freelist(list);
				LeaveCriticalSection(&mutex);
			} else {
				return mingw_lstat(filename, st);
			}
		}

		buf.c[len] = ctmp;

		// parent directory is in cache, requery cache
		fsentry_init(&key, buf.c);
		EnterCriticalSection(&mutex);
		e = (fsentry*) hashmap_get(&fsc->map, &key.hash);
		LeaveCriticalSection(&mutex);
		if (!e) {
			// TODO case insensitive search
			errno = ENOENT;
			return -1;
		}
	}

	st->st_ino = 0;
	st->st_gid = 0;
	st->st_uid = 0;
	st->st_dev = 0;
	st->st_rdev = 0;
	st->st_nlink = 1;
	st->st_mode = e->st_mode;
	st->st_size = e->st_size;
	st->st_atime = e->st_atime;
	st->st_mtime = e->st_mtime;
	st->st_ctime = e->st_ctime;
	return 0;
}

struct fscache_dirent *fscache_opendir(const char *dirname)
{
	pathbuf buf;
	int len;
	fsentry *fsl;
	fscache_DIR *dir;
	strcpy(buf.c, dirname);
	len = strlen(buf.c);
	if (len == 1 && buf.c[0] == '.')
		buf.c[0] = 0;
	else if (len > 0 && buf.c[len - 1] != '/')
		strcpy(buf.c + len, "/");
//printf("opendir: %s\n", buf.c);
	// TODO don't use dirty cache entries
	EnterCriticalSection(&mutex);
	fsl = fscache_getlist(fscache_get(buf.c), buf.c);
	//fsl = fscache_getlist(NULL, buf.c);
	LeaveCriticalSection(&mutex);
	if (fsl == NULL) {
		// TODO set errno appropriately
		return NULL;
	}

	dir = (fscache_DIR*) xmalloc(sizeof(fscache_DIR));
	dir->d_type = 0;
	dir->pfsentry = fsl;
	return dir;
}

struct fscache_dirent *fscache_readdir(fscache_DIR *dir)
{
	fsentry *next = ((fsentry*) dir->pfsentry)->next;
	if (!next)
		return NULL;
	dir->pfsentry = next;
	dir->d_type = S_ISDIR(next->st_mode) ? DT_DIR : DT_REG;
	dir->d_name = next->name;
	return dir;
}

void fscache_closedir(fscache_DIR *dir)
{
	EnterCriticalSection(&mutex);
	fsentry_freelist((fsentry*) dir->pfsentry);
	LeaveCriticalSection(&mutex);
	free(dir);
}

