/*
 * name-hash.c
 *
 * Hashing names in the index state
 *
 * Copyright (C) 2008 Linus Torvalds
 */
#define NO_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"

/*
 * This removes bit 5 if bit 6 is set.
 *
 * That will make US-ASCII characters hash to their upper-case
 * equivalent. We could easily do this one whole word at a time,
 * but that's for future worries.
 */
static inline unsigned char icase_hash(unsigned char c)
{
	return c & ~((c & 0x40) >> 1);
}

static unsigned int hash_name(const char *name, int namelen)
{
	unsigned int hash = 0x123;

	while (namelen--) {
		unsigned char c = *name++;
		c = icase_hash(c);
		hash = hash*101 + c;
	}
	return hash;
}

struct dir_entry {
	struct dir_entry *next;
	struct dir_entry *parent;
	struct cache_entry *ce;
	int nr;
	unsigned int namelen;
};

static struct dir_entry *find_dir_entry(struct index_state *istate,
		const char *name, unsigned int namelen)
{
	unsigned int hash = hash_name(name, namelen);
	struct dir_entry *dir;

	for (dir = lookup_hash(hash, &istate->dir_hash); dir; dir = dir->next)
		if (dir->namelen == namelen &&
		    !strncasecmp(dir->ce->name, name, namelen))
			return dir;
	return NULL;
}

static struct dir_entry *hash_dir_entry(struct index_state *istate,
		struct cache_entry *ce, int namelen, int add)
{
	/*
	 * Throw each directory component in the hash for quick lookup
	 * during a git status. Directory components are stored with their
	 * closing slash.
	 */
	struct dir_entry *dir, *p;

	/* get length of parent directory */
	while (namelen > 0 && !is_dir_sep(ce->name[namelen - 1]))
		namelen--;
	if (namelen <= 0)
		return NULL;

	/* lookup existing entry for that directory */
	dir = find_dir_entry(istate, ce->name, namelen);
	if (add && !dir) {
		/* not found, create it and add to hash table */
		void **pdir;
		unsigned int hash = hash_name(ce->name, namelen);

		dir = xcalloc(1, sizeof(struct dir_entry));
		dir->namelen = namelen;
		dir->ce = ce;

		pdir = insert_hash(hash, dir, &istate->dir_hash);
		if (pdir) {
			dir->next = *pdir;
			*pdir = dir;
		}

		/* recursively add missing parent directories */
		dir->parent = hash_dir_entry(istate, ce, namelen - 1, add);
	}

	/* add or release reference to this entry (and parents if 0) */
	p = dir;
	if (add) {
		while (p && !(p->nr++))
			p = p->parent;
	} else {
		while (p && p->nr && !(--p->nr))
			p = p->parent;
	}

	return dir;
}

static void hash_index_entry(struct index_state *istate, struct cache_entry *ce)
{
	void **pos;
	unsigned int hash;

	if (ce->ce_flags & CE_HASHED)
		return;
	ce->ce_flags |= CE_HASHED;
	ce->next = NULL;
	hash = hash_name(ce->name, ce_namelen(ce));
	pos = insert_hash(hash, ce, &istate->name_hash);
	if (pos) {
		ce->next = *pos;
		*pos = ce;
	}

	if (ignore_case && !(ce->ce_flags & CE_UNHASHED))
		hash_dir_entry(istate, ce, ce_namelen(ce), 1);
}

static void lazy_init_name_hash(struct index_state *istate)
{
	int nr;

	if (istate->name_hash_initialized)
		return;
	for (nr = 0; nr < istate->cache_nr; nr++)
		hash_index_entry(istate, istate->cache[nr]);
	istate->name_hash_initialized = 1;
}

void add_name_hash(struct index_state *istate, struct cache_entry *ce)
{
	/* if already hashed, add reference to directory entries */
	if (ignore_case && (ce->ce_flags & CE_STATE_MASK) == CE_STATE_MASK)
		hash_dir_entry(istate, ce, ce_namelen(ce), 1);

	ce->ce_flags &= ~CE_UNHASHED;
	if (istate->name_hash_initialized)
		hash_index_entry(istate, ce);
}

/*
 * We don't actually *remove* it, we can just mark it invalid so that
 * we won't find it in lookups.
 *
 * Not only would we have to search the lists (simple enough), but
 * we'd also have to rehash other hash buckets in case this makes the
 * hash bucket empty (common). So it's much better to just mark
 * it.
 */
void remove_name_hash(struct index_state *istate, struct cache_entry *ce)
{
	/* if already hashed, release reference to directory entries */
	if (ignore_case && (ce->ce_flags & CE_STATE_MASK) == CE_HASHED)
		hash_dir_entry(istate, ce, ce_namelen(ce), 0);

	ce->ce_flags |= CE_UNHASHED;
}

static int slow_same_name(const char *name1, int len1, const char *name2, int len2)
{
	if (len1 != len2)
		return 0;

	while (len1) {
		unsigned char c1 = *name1++;
		unsigned char c2 = *name2++;
		len1--;
		if (c1 != c2) {
			c1 = toupper(c1);
			c2 = toupper(c2);
			if (c1 != c2)
				return 0;
		}
	}
	return 1;
}

static int same_name(const struct cache_entry *ce, const char *name, int namelen, int icase)
{
	int len = ce_namelen(ce);

	/*
	 * Always do exact compare, even if we want a case-ignoring comparison;
	 * we do the quick exact one first, because it will be the common case.
	 */
	if (len == namelen && !cache_name_compare(name, namelen, ce->name, len))
		return 1;

	if (!icase)
		return 0;

	return slow_same_name(name, namelen, ce->name, len);
}

struct cache_entry *index_name_exists(struct index_state *istate, const char *name, int namelen, int icase)
{
	unsigned int hash = hash_name(name, namelen);
	struct cache_entry *ce;

	lazy_init_name_hash(istate);
	ce = lookup_hash(hash, &istate->name_hash);

	while (ce) {
		if (!(ce->ce_flags & CE_UNHASHED)) {
			if (same_name(ce, name, namelen, icase))
				return ce;
		}
		ce = ce->next;
	}

	/*
	 * When looking for a directory (trailing '/'), it might be a
	 * submodule or a directory. Despite submodules being directories,
	 * they are stored in the name hash without a closing slash.
	 * When ignore_case is 1, directories are stored in a separate hash
	 * with their closing slash.
	 *
	 * The side effect of this storage technique is we have need to
	 * remove the slash from name and perform the lookup again without
	 * the slash.  If a match is made, S_ISGITLINK(ce->mode) will be
	 * true.
	 */
	if (icase && name[namelen - 1] == '/') {
		struct dir_entry *dir = find_dir_entry(istate, name, namelen);
		if (dir && dir->nr)
			return dir->ce;

		ce = index_name_exists(istate, name, namelen - 1, icase);
		if (ce && S_ISGITLINK(ce->ce_mode))
			return ce;
	}
	return NULL;
}

static int free_dir_entry(void *entry, void *unused)
{
	struct dir_entry *dir = entry;
	while (dir) {
		struct dir_entry *next = dir->next;
		free(dir);
		dir = next;
	}
	return 0;
}

void free_name_hash(struct index_state *istate)
{
	if (!istate->name_hash_initialized)
		return;
	istate->name_hash_initialized = 0;
	if (ignore_case)
		/* free directory entries */
		for_each_hash(&istate->dir_hash, free_dir_entry, NULL);

	free_hash(&istate->name_hash);
	free_hash(&istate->dir_hash);
}
