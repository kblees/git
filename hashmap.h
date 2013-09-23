#ifndef HASHMAP_H
#define HASHMAP_H

/*
 * Generic implementation of hash-based key value mappings.
 * Supports basic operations get, add/put, remove and iteration.
 *
 * Also contains a set of ready-to-use hash functions for strings, using the
 * FNV-1 algorithm (see http://www.isthe.com/chongo/tech/comp/fnv).
 */

/*
 * Case-sensitive FNV-1 hash of 0-terminated string.
 * str: the string
 * returns hash code
 */
extern unsigned int strhash(const char *buf);

/*
 * Case-insensitive FNV-1 hash of 0-terminated string.
 * str: the string
 * returns hash code
 */
extern unsigned int strihash(const char *buf);

/*
 * Case-sensitive FNV-1 hash of a memory block.
 * buf: start of the memory block
 * len: length of the memory block
 * returns hash code
 */
extern unsigned int memhash(const void *buf, size_t len);

/*
 * Case-insensitive FNV-1 hash of a memory block.
 * buf: start of the memory block
 * len: length of the memory block
 * returns hash code
 */
extern unsigned int memihash(const void *buf, size_t len);

/*
 * Hashmap entry data structure, must be used as first member of user data
 * structures. Consists of a pointer and an int. Ideally it should be followed
 * by an int-sized member to prevent unused memory on 64-bit systems due to
 * alignment.
 */
struct hashmap_entry {
	struct hashmap_entry *next;
	unsigned int hash;
};

/*
 * User-supplied function to test two hashmap entries for equality, shall
 * return 0 if the entries are equal. This function is always called with
 * non-NULL parameters that have the same hash code. When looking up an entry,
 * the key parameter to hashmap_get and hashmap_remove is always passed as
 * second argument.
 */
typedef int (*hashmap_cmp_fn)(const void *entry, const void *entry_or_key);

/*
 * User-supplied function to free a hashmap entry.
 */
typedef void (*hashmap_free_fn)(void *entry);

/*
 * Hashmap data structure, use with hashmap_* functions.
 */
struct hashmap {
	struct hashmap_entry **table;
	hashmap_cmp_fn cmpfn;
	unsigned int size, tablesize;
};

/*
 * Hashmap iterator data structure, use with hasmap_iter_* functions.
 */
struct hashmap_iter {
	struct hashmap *map;
	struct hashmap_entry *next;
	unsigned int tablepos;
};

/*
 * Initializes a hashmap_entry structure.
 * entry: pointer to the entry to initialize
 * hash: hash code of the entry
 * key_only: true if entry is a key-only structure, see hashmap_entry_is_key
 */
static inline void hashmap_entry_init(void *entry, int hash, int key_only)
{
	struct hashmap_entry *e = entry;
	e->hash = hash;
	e->next = key_only ? (struct hashmap_entry*) -1 : NULL;
}

/*
 * Checks if hashmap_entry was initialized with the key_only flag. This is
 * useful if the entry structure is variable-sized (e.g. ending in a FLEX_ARRAY)
 * and the key is part of the variable portion. To prevent dynamic allocation of
 * a full-fledged entry structure for each lookup, a smaller, statically sized
 * structure can be used as key (i.e. replacing the FLEX_ARRAY member with a
 * char pointer). The hashmap_cmp_fn comparison function can then check whether
 * entry_or_key is a full-fledged entry or a key-only structure.
 * entry: pointer to the entry to check
 * returns 0 for key-value entries and non-0 for key-only entries
 */
static inline int hashmap_entry_is_key(const void *entry)
{
	const struct hashmap_entry *e = entry;
	return e->next == (struct hashmap_entry*) -1;
}

/*
 * Initializes a hashmap structure.
 * map: hashmap to initialize
 * equals_function: optional function to test equality of hashmap entries. If
 *  NULL, entries are considered equal if their hash codes are equal.
 * initial_size: optional number of initial entries, 0 if unknown
 */
extern void hashmap_init(struct hashmap *map, hashmap_cmp_fn equals_function,
		size_t initial_size);

/*
 * Frees a hashmap structure and allocated memory.
 * map: hashmap to free
 * free_function: optional function to free the hashmap entries
 */
extern void hashmap_free(struct hashmap *map, hashmap_free_fn free_function);

/*
 * Returns the hashmap entry for the specified key, or NULL if not found.
 * map: the hashmap
 * key: key of the entry to look up
 * returns matching hashmap entry, or NULL if not found
 */
extern void *hashmap_get(const struct hashmap *map, const void *key);

/*
 * Returns the next equal hashmap entry if the map contains duplicates (see
 * hashmap_add).
 * map: the hashmap
 * entry: current entry, obtained via hashmap_get or hashmap_get_next
 * returns next equal hashmap entry, or NULL if not found
 */
extern void *hashmap_get_next(const struct hashmap *map, const void *entry);

/*
 * Adds a hashmap entry. This allows to add duplicate entries (i.e. separate
 * values with the same key according to hashmap_cmp_fn).
 * map: the hashmap
 * entry: the entry to add
 */
extern void hashmap_add(struct hashmap *map, void *entry);

/*
 * Adds or replaces a hashmap entry.
 * map: the hashmap
 * entry: the entry to add or replace
 * returns previous entry, or NULL if the entry is new
 */
extern void *hashmap_put(struct hashmap *map, void *entry);

/*
 * Removes a hashmap entry matching the specified key.
 * map: the hashmap
 * key: key of the entry to remove
 * returns removed entry, or NULL if not found
 */
extern void *hashmap_remove(struct hashmap *map, const void *key);

/*
 * Initializes a hashmap iterator structure.
 * map: the hashmap
 * iter: hashmap iterator structure
 */
extern void hashmap_iter_init(struct hashmap *map, struct hashmap_iter *iter);

/**
 * Returns the next hashmap entry.
 * iter: hashmap iterator
 * returns next entry, or NULL if there are no more entries
 */
extern void *hashmap_iter_next(struct hashmap_iter *iter);

/**
 * Initializes a hashmap iterator and returns the first hashmap entry.
 * map: the hashmap
 * iter: hashmap iterator
 * returns first entry, or NULL if there are no entries
 */
static inline void *hashmap_iter_first(struct hashmap *map,
		struct hashmap_iter *iter)
{
	hashmap_iter_init(map, iter);
	return hashmap_iter_next(iter);
}

#endif
