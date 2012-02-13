#include "cache.h"
#include "hash.c"
#include "hashmap.h"
#include <stdio.h>

typedef struct test_entry
{
	hashmap_entry base;
	char *key;
} test_entry;

static int cmp_test_entry(const test_entry *e1, const test_entry *e2)
{
	return strcmp(e1->key, e2->key);
}

static void free_test_entry(test_entry *e)
{
	free(e->key);
	free(e);
}

static void test_map(unsigned int size)
{
	hashmap map;
	unsigned int i;
	test_entry e, *pe, *pe2;
	char buf[16];
	char *found;
	hashmap_iter iter;

	e.key = buf;

	// initialize the map
	hashmap_init(&map, (hashmap_cmp_fn) cmp_test_entry, 0);

	// test hashmap_put
	for (i = 0; i < size; i++)
	{
		pe = xmalloc(sizeof(test_entry));
		snprintf(buf, 16, "test%X", i);
		pe->key = xstrdup(buf);
		hashmap_entry_init(&pe->base, strhash(pe->key));
		if (hashmap_put(&map, &pe->base))
			die("size %u, entry %u: %s already exists!", size, i, pe->key);

		pe2 = (test_entry*) hashmap_put(&map, &pe->base);
		if (pe != pe2)
			die("size %u, entry %u: adding %s doesn't return existing entry!",
					size, i, pe->key);
	}

	// test hashmap_remove
	for (i = 1; i < size; i += 3)
	{
		snprintf(buf, 16, "test%X", i);
		hashmap_entry_init(&e.base, strhash(e.key));
		pe = (test_entry*) hashmap_remove(&map, &e.base);
		if (!pe)
			die("size %u, entry %u: failed to remove %s!", size, i, e.key);
		free_test_entry(pe);

		pe2 = (test_entry*) hashmap_remove(&map, &e.base);
		if (pe2)
			die("size %u, entry %u: removed %s twice!", size, i, e.key);
	}

	// test hashmap_get
	for (i = 0; i < size; i++)
	{
		snprintf(buf, 16, "test%X", i);
		hashmap_entry_init(&e.base, strhash(e.key));
		pe = (test_entry*) hashmap_get(&map, &e.base);
		if (((i % 3) == 1) != (pe == NULL))
			die("size %u, entry %u: entry %s %sin map!", size, i, e.key,
					pe ? "" : "not ");
	}

	// test iteration
	found = xcalloc(sizeof(char), size);
	hashmap_iter_init(&map, &iter);
	while ((pe = (test_entry*) hashmap_iter_next(&iter)))
	{
		sscanf(pe->key, "test%X", &i);
		found[i] = 1;
	}
	for (i = 0; i < size; i++)
	{
		if (((i % 3) == 1) != (found[i] == 0))
			die("size %u, entry %u was %sreturned by iterator!\n", size, i,
					found[i] ? "" : "not ");
	}
	free(found);

	// free the map and all remaining entries
	hashmap_free(&map, (hashmap_free_fn) free_test_entry);
}

typedef struct hash_entry
{
	char *key;
	struct hash_entry *next;
	int removed;
} hash_entry;

static int test_iter_fn(void *elem, void *data)
{
	int i;
	if (((hash_entry*)elem)->removed)
		return 0;
	sscanf(((hash_entry*)elem)->key, "test%X", &i);
	((char*)data)[i] = 1;
	return 1;
}

static int hash_free_fn(void *elem, void *data)
{
	free(((hash_entry*)elem)->key);
	free(elem);
	return 0;
}

static void test_hash(unsigned int size)
{
	struct hash_table map;
	unsigned int i;
	hash_entry e, *pe, *pe2, **res;
	char buf[16];
	char *found;

	e.key = buf;

	// initialize the map
	init_hash(&map);

	// test hashmap_put
	for (i = 0; i < size; i++)
	{
		pe = xmalloc(sizeof(hash_entry));
		snprintf(buf, 16, "test%X", i);
		pe->key = xstrdup(buf);
		pe->next = NULL;
		pe->removed = 0;
		res = (hash_entry**) insert_hash(strhash(pe->key), pe, &map);
		if (res) {
			pe2 = *res;
			while (pe2 && strcmp(pe2->key, buf))
				pe2 = pe2->next;
			if (pe2)
				die("size %u, entry %u: %s already exists!", size, i, pe->key);

			pe->next = *res;
			*res = pe;
		}
		res = (hash_entry**) insert_hash(strhash(pe->key), pe, &map);
		if (!res)
			die("size %u, entry %u: %s just added, but not found!", size, i, pe->key);

		pe2 = *res;
		while (pe2 && strcmp(pe2->key, buf))
			pe2 = pe2->next;
		if (pe != pe2)
			die("size %u, entry %u: %s already exists!", size, i, pe->key);
	}

	// test hashmap_remove
	for (i = 1; i < size; i += 3)
	{
		snprintf(buf, 16, "test%X", i);
		pe = (hash_entry*) lookup_hash(strhash(buf), &map);
		while (pe && strcmp(pe->key, buf))
			pe = pe->next;
		if (!pe)
			die("size %u, entry %u: failed to remove %s!", size, i, e.key);
		pe->removed = 1;

		pe2 = (hash_entry*) lookup_hash(strhash(buf), &map);
		while (pe2 && strcmp(pe2->key, buf))
			pe2 = pe2->next;
		if (!pe2->removed)
			die("size %u, entry %u: removed %s twice!", size, i, e.key);
	}

	// test hashmap_get
	for (i = 0; i < size; i++)
	{
		snprintf(buf, 16, "test%X", i);
		e.key = buf;
		e.next = NULL;
		pe = (hash_entry*) lookup_hash(strhash(e.key), &map);
		while (pe && strcmp(pe->key, buf))
			pe = pe->next;
		if (((i % 3) == 1) != (pe->removed == 1))
			die("size %u, entry %u: entry %s %sin map!", size, i, e.key,
					pe ? "" : "not ");
	}

	// test iteration
	found = xcalloc(sizeof(char), size);
	for_each_hash(&map, test_iter_fn, found);
	for (i = 0; i < size; i++)
	{
		if (((i % 3) == 1) != (found[i] == 0))
			die("size %u, entry %u was %sreturned by iterator!\n", size, i,
					found[i] ? "" : "not ");
	}
	free(found);

	for_each_hash(&map, hash_free_fn, NULL);
	free_hash(&map);
}


int main(int argc, const char *argv[])
{
	int i;
	// run test with various sizes
	for (i = 57; i < 1000000; i *= 7.5)
		if (argc > 1)
			test_map(i);
		else
			test_hash(i);

	return 0;
}
