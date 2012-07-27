#include "../git-compat-util.h"
#include "../cache.h"
#include "../hashmap.h"

#include <stdlib.h>
#include <stdio.h>
#include <direct.h>

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

static int normalize(char *dst, const char *src)
{
	int len;
	if (normalize_path_copy(dst, src)) {
		errno = EINVAL;
		return -1;
	}

	len = strlen(dst);
	// strip trailing '/' if present
	if (!len) {
		errno = ENOENT;
		return -1;
	}
	if (dst[len - 1] == '/')
		dst[--len] = 0;
	return len;
}

typedef struct fsentry fsentry;

struct fsentry {
	hashmap_entry hash;
	fsentry *next;
	fsentry *list;
	const char *name;
	unsigned short len;
	unsigned short refcnt;
	mode_t st_mode;
	off64_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
};

static int fsentry_cmp(const fsentry *fse1, const fsentry *fse2)
{
	int res;
	if (fse1 == fse2)
		return 0;
	if (fse1->list != fse2->list && (res = fsentry_cmp(
			fse1->list ? fse1->list : fse1,
			fse2->list ? fse2->list	: fse2)))
		return res;
	if (fse1->len != fse2->len)
		return fse1->len - fse2->len;
	return strnicmp(fse1->name, fse2->name, fse1->len);
}

static unsigned int fsentry_hash(const fsentry *fse)
{
	unsigned int hash = fse->list ? fse->list->hash.hash : 0;
	return hash ^ memihash(fse->name, fse->len);
}

static void fsentry_init(fsentry *fse, fsentry *list, const char *name,
		size_t len)
{
	fse->list = list;
	fse->name = name;
	fse->len = len;
	hashmap_entry_init(&fse->hash, fsentry_hash(fse));
}

static fsentry *fsentry_alloc(fsentry *list, const char *name, size_t len)
{
	fsentry *fse = (fsentry*) xmalloc(sizeof(fsentry) + len + 1);
	fse->list = list;
	fse->name = ((char*) fse) + sizeof(fsentry);
	fse->len = len;
	memcpy((void*) fse->name, name, len);
	((char*)fse->name)[len] = 0;
	hashmap_entry_init(&fse->hash, fsentry_hash(fse));
	fse->next = NULL;
	fse->refcnt = 0;
	return fse;
}

static fsentry *fsentry_create(fsentry *list, const WIN32_FIND_DATAW *fdata)
{
	char buf[MAX_PATH * 3];
	int len;
	fsentry *fse;
	len = xwcstoutf(buf, fdata->cFileName, ARRAY_SIZE(buf));

	fse = fsentry_alloc(list, buf, len);

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
 * dir is expected to be empty or not /-terminated
 */
static fsentry *fsentry_createlist(const char *dir, size_t len)
{
	int wlen;
	WIN32_FIND_DATAW fdata;
	HANDLE handle;
	fsentry *list, **phead;
	wchar_t wbuf[MAX_PATH * 2];
	/* open find file handle */
	wlen = xutftowcsn(wbuf, dir, ARRAY_SIZE(wbuf), len);
	if (wlen)
		wbuf[wlen++] = L'/';
	wbuf[wlen++] = L'*';
	wbuf[wlen] = 0;

	handle = FindFirstFileW(wbuf, &fdata);
	if (handle == INVALID_HANDLE_VALUE) {
		//printf("Error in FindFirstFile for %s: %lu\n", dir, GetLastError());
		errno = err_win_to_posix(GetLastError());
		return NULL;
	}

	/* allocate object to hold directory listing */
	list = fsentry_alloc(NULL, dir, len);
	list->refcnt = 1;

	/* walk directory -> build linked list */
	phead = &list->next;
	do {
		*phead = fsentry_create(list, &fdata);
		phead = &(*phead)->next;
	} while (FindNextFileW(handle, &fdata));
	FindClose(handle);

	return list;
}

static hashmap map;
static int initialized = 0;
static int enabled = 0;
static CRITICAL_SECTION mutex;

static inline void fscache_init()
{
	if (!initialized) {
		InitializeCriticalSection(&mutex);
		hashmap_init(&map, (hashmap_cmp_fn) fsentry_cmp, 0);
		initialized = 1;
	}
}

static void fsentry_freelist(fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	EnterCriticalSection(&mutex);
	if (--(fse->refcnt) > 0)
		fse = NULL;
	LeaveCriticalSection(&mutex);

	while (fse) {
		fsentry *next = fse->next;
		free(fse);
		fse = next;
	}
}

static void fscache_add(fsentry *fse)
{
	if (fse->list)
		fse = fse->list;
	while (fse) {
		hashmap_put(&map, &fse->hash);
		fse = fse->next;
	}
}

static fsentry *fscache_remove(fsentry *fse)
{
	fsentry *e;
	if (fse->list)
		fse = fse->list;
	fse = (fsentry*) hashmap_remove(&map, &fse->hash);
	if (!fse)
		return NULL;

	e = fse->next;
	while (e) {
		hashmap_remove(&map, &e->hash);
		e = e->next;
	}
	return fse;
}

static void fscache_clear()
{
	hashmap_iter iter;
	fsentry *fse = (fsentry*) hashmap_iter_first(&map, &iter);
	while (fse) {
		fscache_remove(fse);
		fsentry_freelist(fse);
		fse = (fsentry*) hashmap_iter_first(&map, &iter);
	}

	if (map.size > 0)
		printf("still some objects in map!");
}

static int fscache_enabled(const char *path)
{
	fscache_init();
	return enabled > 0 && !is_absolute_path(path);
}

extern int fscache_enable(int enable)
{
	fscache_init();
	EnterCriticalSection(&mutex);
	if (enable) {
		enabled++;
	} else if (enabled) {
		enabled--;
		if (!enabled)
			fscache_clear();
	}
	LeaveCriticalSection(&mutex);
	return enabled;
}

static fsentry *fscache_getlist(const char *dir, size_t len, int addref)
{
	fsentry key, *list;
	if (!fscache_enabled(dir))
		return fsentry_createlist(dir, len);

	/* lookup in cache */
	fsentry_init(&key, NULL, dir, len);
	EnterCriticalSection(&mutex);
	list = (fsentry*) hashmap_get(&map, &key.hash);
	if (!list) {
		LeaveCriticalSection(&mutex);
		list = fsentry_createlist(dir, len);
		if (!list)
			return NULL;

		EnterCriticalSection(&mutex);
		fscache_add(list);
	}

	if (addref)
		list->refcnt++;
	LeaveCriticalSection(&mutex);
	return list;
}

static fsentry *fscache_getentry(const char *filename, size_t len)
{
	int dirlen, base = len;
	fsentry key[2], *fse;
	while (base && filename[base - 1] != '/')
		base--;
	dirlen = base ? base - 1 : 0;
	fsentry_init(key, NULL, filename, dirlen);
	fsentry_init(key + 1, key, filename + base, len - base);
	EnterCriticalSection(&mutex);
	fse = (fsentry*) hashmap_get(&map, &key[1].hash);
	if (!fse) {
		fse = (fsentry*) hashmap_get(&map, &key[0].hash);
		LeaveCriticalSection(&mutex);
		if (fse || !fscache_getlist(filename, dirlen, 0))
			return NULL;
		EnterCriticalSection(&mutex);
		fse = (fsentry*) hashmap_get(&map, &key[1].hash);
	}
	LeaveCriticalSection(&mutex);
	return fse;
}

int fscache_lstat(const char *filename, struct stat *st)
{
	int len;
	fsentry *fse;
	//printf("lstat %s\n", filename);
	if (!fscache_enabled(filename))
		return mingw_lstat(filename, st);

	len = strlen(filename);
	if (len && filename[len - 1] == '/')
		len--;
	fse = fscache_getentry(filename, len);
	if (!fse) {
		char buf[MAX_PATH];
		len = normalize(buf, filename);
		if (len < 0)
			return mingw_lstat(filename, st);

		fse = fscache_getentry(buf, len);
		if (!fse) {
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
	st->st_mode = fse->st_mode;
	st->st_size = fse->st_size;
	st->st_atime = fse->st_atime;
	st->st_mtime = fse->st_mtime;
	st->st_ctime = fse->st_ctime;
	return 0;
}

struct fscache_dirent *fscache_opendir(const char *dirname)
{
	fsentry *list;
	fscache_DIR *dir;
	int len = strlen(dirname);
	//printf("opendir: %s\n", dirname);
	if ((len == 1 && dirname[0] == '.') ||
	    (len > 0 && dirname[len - 1] == '/'))
		len--;

	list = fscache_getlist(dirname, len, 1);
	if (!list)
		return NULL;

	dir = (fscache_DIR*) xmalloc(sizeof(fscache_DIR));
	dir->d_type = 0;
	dir->pfsentry = list;
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
	fsentry_freelist((fsentry*) dir->pfsentry);
	free(dir);
}
