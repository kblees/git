/*
 * Fixes git's usage of getenv() on systems with exotic getenv implementations.
 */
#include "../git-compat-util.h"
#include "../cache.h"

#ifdef NO_PTHREADS

#define mutex_init() (void)0
#define mutex_lock() (void)0
#define mutex_unlock() (void)0

#else

#include <pthread.h>

static pthread_mutex_t getenv_mutex;
#define mutex_init() pthread_mutex_init(&getenv_mutex, NULL)
#define mutex_lock() pthread_mutex_lock(&getenv_mutex)
#define mutex_unlock() pthread_mutex_unlock(&getenv_mutex)

#endif

#undef getenv
#ifdef stdlib_getenv
# define unsafe_getenv stdlib_getenv
#else
# define unsafe_getenv getenv
#endif

/* FNV-1 string hash function, see http://www.isthe.com/chongo/tech/comp/fnv */
#define FNV32_BASE ((unsigned int) 0x811c9dc5)
#define FNV32_PRIME ((unsigned int) 0x01000193)

unsigned int strhash(const char *str)
{
	unsigned int c, hash = FNV32_BASE;
	while ((c = (unsigned char) *str++))
		hash = (hash * FNV32_PRIME) ^ c;
	return hash;
}

/*
 * Entry in a pool of strings that have ever been returned by getenv().
 */
struct getenv_pool_entry {
	struct getenv_pool_entry *next;
	char value[1]; /* over-allocated to required size */
};

char *safe_getenv(const char *key)
{
	static struct hash_table getenv_pool;
	static int initialized = 0;
	int hash;
	char *value;
	struct getenv_pool_entry *ent;
	void **pnext;

	/* lazy initialize mutex + getenv_pool */
	if (!initialized) {
		init_hash(&getenv_pool);
		mutex_init();
		initialized = 1;
	}

	/* get value from original getenv */
	mutex_lock();
	value = unsafe_getenv(key);
	if (!value) {
		mutex_unlock();
		return NULL;
	}

	/* lookup and return existing value from pool */
	hash = strhash(value);
	ent = lookup_hash(hash, &getenv_pool);
	while (ent) {
		if (!strcmp(value, ent->value)) {
			mutex_unlock();
			return ent->value;
		}
		ent = ent->next;
	}

	/* not in pool, copy value and insert */
	ent = xmalloc(sizeof(*ent) + strlen(value));
	ent->next = NULL;
	strcpy(ent->value, value);
	pnext = insert_hash(hash, ent, &getenv_pool);
	if (pnext)
		ent->next = *pnext;
	mutex_unlock();
	return ent->value;
}
