#include <winsock2.h>
#include <ws2tcpip.h>

/*
 * things that are not available in header files
 */

typedef int pid_t;
typedef int uid_t;
typedef int socklen_t;
#define hstrerror strerror

#define S_IFLNK    0120000 /* Symbolic link */
#define S_ISLNK(x) (((x) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(x) 0

#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_ISUID 0
#define S_ISGID 0
#define S_ISVTX 0

#define WIFEXITED(x) 1
#define WIFSIGNALED(x) 0
#define WEXITSTATUS(x) ((x) & 0xff)
#define WTERMSIG(x) SIGTERM

#define EWOULDBLOCK EAGAIN
#define SHUT_WR SD_SEND

#define SIGHUP 1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGCHLD 17

#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 0x1

#define EAFNOSUPPORT WSAEAFNOSUPPORT
#define ECONNABORTED WSAECONNABORTED

struct passwd {
	char *pw_name;
	char *pw_gecos;
	char *pw_dir;
};

extern char *getpass(const char *prompt);

typedef void (__cdecl *sig_handler_t)(int);
struct sigaction {
	sig_handler_t sa_handler;
	unsigned sa_flags;
};
#define sigemptyset(x) (void)0
#define SA_RESTART 0

struct itimerval {
	struct timeval it_value, it_interval;
};
#define ITIMER_REAL 0

/*
 * sanitize preprocessor namespace polluted by Windows headers defining
 * macros which collide with git local versions
 */
#undef HELP_COMMAND /* from winuser.h */

/*
 * trivial stubs
 */

static inline int readlink(const char *path, char *buf, size_t bufsiz)
{ errno = ENOSYS; return -1; }
static inline int symlink(const char *oldpath, const char *newpath)
{ errno = ENOSYS; return -1; }
static inline int fchmod(int fildes, mode_t mode)
{ errno = ENOSYS; return -1; }
static inline pid_t fork(void)
{ errno = ENOSYS; return -1; }
static inline unsigned int alarm(unsigned int seconds)
{ return 0; }
static inline int fsync(int fd)
{ return _commit(fd); }
static inline pid_t getppid(void)
{ return 1; }
static inline void sync(void)
{}
static inline uid_t getuid(void)
{ return 1; }
static inline struct passwd *getpwnam(const char *name)
{ return NULL; }
static inline int fcntl(int fd, int cmd, ...)
{
	if (cmd == F_GETFD || cmd == F_SETFD)
		return 0;
	errno = EINVAL;
	return -1;
}
/* bash cannot reliably detect negative return codes as failure */
#define exit(code) exit((code) & 0xff)

/*
 * simple adaptors
 */

int mingw_mkdir(const char *path, int mode);
#define mkdir mingw_mkdir

#define WNOHANG 1
pid_t waitpid(pid_t pid, int *status, unsigned options);

#define kill mingw_kill
int mingw_kill(pid_t pid, int sig);

#ifndef NO_OPENSSL
#include <openssl/ssl.h>
static inline int mingw_SSL_set_fd(SSL *ssl, int fd)
{
	return SSL_set_fd(ssl, _get_osfhandle(fd));
}
#define SSL_set_fd mingw_SSL_set_fd

static inline int mingw_SSL_set_rfd(SSL *ssl, int fd)
{
	return SSL_set_rfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_rfd mingw_SSL_set_rfd

static inline int mingw_SSL_set_wfd(SSL *ssl, int fd)
{
	return SSL_set_wfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_wfd mingw_SSL_set_wfd
#endif

/*
 * implementations of missing functions
 */

int pipe(int filedes[2]);
unsigned int sleep (unsigned int seconds);
int mkstemp(char *template);
int gettimeofday(struct timeval *tv, void *tz);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
int getpagesize(void);	/* defined in MinGW's libgcc.a */
struct passwd *getpwuid(uid_t uid);
int setitimer(int type, struct itimerval *in, struct itimerval *out);
int sigaction(int sig, struct sigaction *in, struct sigaction *out);
int link(const char *oldpath, const char *newpath);

/*
 * replacements of existing functions
 */

int mingw_unlink(const char *pathname);
#define unlink mingw_unlink

int mingw_rmdir(const char *path);
#define rmdir mingw_rmdir

int mingw_open (const char *filename, int oflags, ...);
#define open mingw_open

ssize_t mingw_write(int fd, const void *buf, size_t count);
#define write mingw_write

FILE *mingw_fopen (const char *filename, const char *otype);
#define fopen mingw_fopen

FILE *mingw_freopen (const char *filename, const char *otype, FILE *stream);
#define freopen mingw_freopen

int mingw_access(const char *filename, int mode);
#undef access
#define access mingw_access

int mingw_chdir(const char *dirname);
#define chdir mingw_chdir

int mingw_chmod(const char *filename, int mode);
#define chmod mingw_chmod

char *mingw_mktemp(char *template);
#define mktemp mingw_mktemp

char *mingw_getcwd(char *pointer, int len);
#define getcwd mingw_getcwd

char *mingw_getenv(const char *name);
#define getenv mingw_getenv
int mingw_putenv(const char *namevalue);
#define putenv mingw_putenv

struct hostent *mingw_gethostbyname(const char *host);
#define gethostbyname mingw_gethostbyname

void mingw_freeaddrinfo(struct addrinfo *res);
#define freeaddrinfo mingw_freeaddrinfo

int mingw_getaddrinfo(const char *node, const char *service,
		      const struct addrinfo *hints, struct addrinfo **res);
#define getaddrinfo mingw_getaddrinfo

int mingw_getnameinfo(const struct sockaddr *sa, socklen_t salen,
		      char *host, DWORD hostlen, char *serv, DWORD servlen,
		      int flags);
#define getnameinfo mingw_getnameinfo

int mingw_socket(int domain, int type, int protocol);
#define socket mingw_socket

int mingw_connect(int sockfd, struct sockaddr *sa, size_t sz);
#define connect mingw_connect

int mingw_bind(int sockfd, struct sockaddr *sa, size_t sz);
#define bind mingw_bind

int mingw_setsockopt(int sockfd, int lvl, int optname, void *optval, int optlen);
#define setsockopt mingw_setsockopt

int mingw_listen(int sockfd, int backlog);
#define listen mingw_listen

int mingw_accept(int sockfd, struct sockaddr *sa, socklen_t *sz);
#define accept mingw_accept

int mingw_rename(const char*, const char*);
#define rename mingw_rename

#if defined(USE_WIN32_MMAP) || defined(_MSC_VER)
int mingw_getpagesize(void);
#define getpagesize mingw_getpagesize
#endif

struct rlimit {
	unsigned int rlim_cur;
};
#define RLIMIT_NOFILE 0

static inline int getrlimit(int resource, struct rlimit *rlp)
{
	if (resource != RLIMIT_NOFILE) {
		errno = EINVAL;
		return -1;
	}

	rlp->rlim_cur = 2048;
	return 0;
}

/* Use mingw_lstat() instead of lstat()/stat() and
 * mingw_fstat() instead of fstat() on Windows.
 */
#define off_t off64_t
#define lseek _lseeki64
#ifndef ALREADY_DECLARED_STAT_FUNCS
#define stat _stati64
int mingw_lstat(const char *file_name, struct stat *buf);
int mingw_stat(const char *file_name, struct stat *buf);
int mingw_fstat(int fd, struct stat *buf);
#define fstat mingw_fstat
#define lstat mingw_lstat
#define _stati64(x,y) mingw_stat(x,y)
#endif

int mingw_utime(const char *file_name, const struct utimbuf *times);
#define utime mingw_utime

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env,
		     const char *dir,
		     int fhin, int fhout, int fherr);
void mingw_execvp(const char *cmd, char *const *argv);
#define execvp mingw_execvp
void mingw_execv(const char *cmd, char *const *argv);
#define execv mingw_execv

static inline unsigned int git_ntohl(unsigned int x)
{ return (unsigned int)ntohl(x); }
#define ntohl git_ntohl

sig_handler_t mingw_signal(int sig, sig_handler_t handler);
#define signal mingw_signal

/*
 * ANSI emulation wrappers
 */

void winansi_init(void);
int winansi_isatty(int fd);
HANDLE winansi_get_osfhandle(int fd);
#define isatty winansi_isatty

/*
 * git specific compatibility
 */

#define has_dos_drive_prefix(path) (isalpha(*(path)) && (path)[1] == ':')
#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
int mingw_offset_1st_component(const char *path);
#define offset_1st_component mingw_offset_1st_component
#define PATH_SEP ';'
#define PRIuMAX "I64u"

void mingw_open_html(const char *path);
#define open_html mingw_open_html

void mingw_mark_as_git_dir(const char *dir);
#define mark_as_git_dir mingw_mark_as_git_dir

/*
 * helpers
 */

char **make_augmented_environ(const char *const *vars);
void free_environ(char **env);

/**
 * Converts UTF-8 encoded string to UTF-16LE.
 *
 * To support repositories with legacy-encoded file names, invalid UTF-8 bytes
 * 0xa0 - 0xff are converted to corresponding printable Unicode chars \u00a0 -
 * \u00ff, and invalid UTF-8 bytes 0x80 - 0x9f (which would make non-printable
 * Unicode) are converted to hex-code.
 *
 * Lead-bytes not followed by an appropriate number of trail-bytes, over-long
 * encodings and 4-byte encodings > \u10ffff are detected as invalid UTF-8.
 *
 * Maximum space requirement for the target buffer is two wide chars per UTF-8
 * char (((strlen(utf) * 2) + 1) [* sizeof(wchar_t)]).
 *
 * The maximum space is needed only if the entire input string consists of
 * invalid UTF-8 bytes in range 0x80-0x9f, as per the following table:
 *
 *               |                   | UTF-8 | UTF-16 |
 *   Code point  |  UTF-8 sequence   | bytes | words  | ratio
 * --------------+-------------------+-------+--------+-------
 * 000000-00007f | 0-7f              |   1   |   1    |  1
 * 000080-0007ff | c2-df + 80-bf     |   2   |   1    |  0.5
 * 000800-00ffff | e0-ef + 2 * 80-bf |   3   |   1    |  0.33
 * 010000-10ffff | f0-f4 + 3 * 80-bf |   4   |  2 (a) |  0.5
 * invalid       | 80-9f             |   1   |  2 (b) |  2
 * invalid       | a0-ff             |   1   |   1    |  1
 *
 * (a) encoded as UTF-16 surrogate pair
 * (b) encoded as two hex digits
 *
 * Note that, while the UTF-8 encoding scheme can be extended to 5-byte, 6-byte
 * or even indefinite-byte sequences, the largest valid code point \u10ffff
 * encodes as only 4 UTF-8 bytes.
 *
 * @param wcs wide char target buffer
 * @param utf string to convert
 * @param wcslen size of target buffer (in wchar_t's)
 * @param utflen size of string to convert, or -1 if 0-terminated
 * @return length of converted string (_wcslen(wcs)), or -1 on failure (errno
 *         is set to EINVAL or ENAMETOOLONG)
 * @see mbstowcs, MultiByteToWideChar
 */
int utftowcsn(wchar_t *wcs, const char *utf, size_t wcslen, int utflen);
static inline int utftowcs(wchar_t *wcs, const char *utf, size_t wcslen)
{
	return utftowcsn(wcs, utf, wcslen, -1);
}

/**
 * Converts UTF-16LE encoded string to UTF-8.
 *
 * Maximum space requirement for the target buffer is three UTF-8 chars per
 * wide char ((_wcslen(wcs) * 3) + 1).
 *
 * The maximum space is needed only if the entire input string consists of
 * UTF-16 words in range 0x0800-0xd7ff or 0xe000-0xffff (i.e. \u0800-\uffff
 * modulo surrogate pairs), as per the following table:
 *
 *               |                       | UTF-16 | UTF-8 |
 *   Code point  |  UTF-16 sequence      | words  | bytes | ratio
 * --------------+-----------------------+--------+-------+-------
 * 000000-00007f | 0000-007f             |   1    |   1   |  1
 * 000080-0007ff | 0080-07ff             |   1    |   2   |  2
 * 000800-00ffff | 0800-d7ff / e000-ffff |   1    |   3   |  3
 * 010000-10ffff | d800-dbff + dc00-dfff |   2    |   4   |  2
 *
 * Note that invalid code points > 10ffff cannot be represented in UTF-16.
 *
 * @param utf target buffer
 * @param wcs wide string to convert
 * @param utflen size of target buffer
 * @return length of converted string, or -1 on failure (errno is set to EINVAL
 *         or ENAMETOOLONG)
 * @see wcstombs, WideCharToMultiByte
 */
int wcstoutf(char *utf, const wchar_t *wcs, size_t utflen);

/*
 * A replacement of main() that adds win32 specific initialization.
 */

void mingw_startup();
#define main(c,v) dummy_decl_mingw_main(); \
static int mingw_main(); \
int main(int argc, const char **argv) \
{ \
	mingw_startup(); \
	return mingw_main(__argc, __argv); \
} \
static int mingw_main(c,v)

/*
 * Used by Pthread API implementation for Windows
 */
extern int err_win_to_posix(DWORD winerr);

extern const char *get_windows_home_directory();
#define get_home_directory() get_windows_home_directory()
