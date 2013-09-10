#include "cache.h"
#include "hashmap.h"
#include <stdio.h>

typedef struct test_entry
{
	hashmap_entry ent;
	/* key and value as two \0-terminated strings */
	char key[FLEX_ARRAY];
} test_entry;

typedef struct test_key
{
	hashmap_entry ent;
	char *key;
} test_key;

static const char *get_key(const test_entry *e)
{
	return hashmap_entry_is_key(e) ? ((test_key*) e)->key : e->key;
}

static const char *get_value(const test_entry *e)
{
	return e->key + strlen(e->key) + 1;
}

static int test_entry_cmp(const test_entry *e1, const test_entry *e2)
{
	return strcmp(e1->key, get_key(e2));
}

static int test_entry_cmp_icase(const test_entry *e1, const test_entry *e2)
{
	return strcasecmp(e1->key, get_key(e2));
}

static test_entry *alloc_test_entry(int hash, char *key, int klen, char *value,
		int vlen)
{
	test_entry *entry = malloc(sizeof(test_entry) + klen + vlen + 2);
	hashmap_entry_init(entry, hash, 0);
	memcpy(entry->key, key, klen + 1);
	memcpy(entry->key + klen + 1, value, vlen + 1);
	return entry;
}

/*
 * Test insert performance of hashmap.[ch]
 * Usage: time echo "perfhashmap size rounds" | test-hashmap
 */
static void perf_hashmap(unsigned int size, unsigned int rounds)
{
	hashmap map;
	char buf[16];
	test_entry **entries;
	unsigned int i, j;

	entries = malloc(size * sizeof(test_entry*));
	for (i = 0; i < size; i++) {
		snprintf(buf, sizeof(buf), "%i", i);
		entries[i] = alloc_test_entry(0, buf, strlen(buf), "", 0);
	}

	for (j = 0; j < rounds; j++) {
		// initialize the map
		hashmap_init(&map, (hashmap_cmp_fn) test_entry_cmp, 0);

		// add entries
		for (i = 0; i < size; i++) {
			unsigned int hash = strhash(entries[i]->key);
			hashmap_entry_init(entries[i], hash, 0);
			hashmap_add(&map, entries[i]);
		}

		hashmap_free(&map, NULL);
	}
}

typedef struct hash_entry
{
	struct hash_entry *next;
	char key[FLEX_ARRAY];
} hash_entry;

/*
 * Test insert performance of hash.[ch]
 * Usage: time echo "perfhashtable size rounds" | test-hashmap
 */
static void perf_hashtable(unsigned int size, unsigned int rounds)
{
	struct hash_table map;
	char buf[16];
	hash_entry **entries, **res;
	unsigned int i, j;

	entries = malloc(size * sizeof(hash_entry*));
	for (i = 0; i < size; i++) {
		snprintf(buf, sizeof(buf), "%i", i);
		entries[i] = malloc(sizeof(hash_entry) + strlen(buf) + 1);
		strcpy(entries[i]->key, buf);
	}

	for (j = 0; j < rounds; j++) {
		// initialize the map
		init_hash(&map);

		// add entries
		for (i = 0; i < size; i++) {
			unsigned int hash = strhash(entries[i]->key);
			res = (hash_entry**) insert_hash(hash, entries[i], &map);
			if (res) {
				entries[i]->next = *res;
				*res = entries[i];
			} else {
				entries[i]->next = NULL;
			}
		}

		free_hash(&map);
	}
}

#define DELIM " \t\r\n"

/*
 * Read stdin line by line and print result of commands to stdout:
 *
 * hash key -> strhash(key) memhash(key) strihash(key) memihash(key)
 * put key value -> NULL / old value
 * get key -> NULL / value
 * remove key -> NULL / old value
 * iterate -> key1 value1\nkey2 value2\n...
 * size -> tablesize numentries
 *
 * perfhashmap size rounds -> hashmap.[ch]: add <size> entries <rounds> times
 * perfhashtable size rounds -> hash.[ch]: add <size> entries <rounds> times
 */
int main(int argc, char *argv[])
{
	char line[1024];
	hashmap map;
	int icase;

	/* init hash map */
	icase = argc > 1 && !strcmp("ignorecase", argv[1]);
	hashmap_init(&map, (hashmap_cmp_fn) (icase ? test_entry_cmp_icase
			: test_entry_cmp), 0);

	/* process commands from stdin */
	while (fgets(line, sizeof(line), stdin)) {
		char *cmd, *p1 = NULL, *p2 = NULL;
		int l1 = 0, l2 = 0, hash = 0;
		test_entry *entry;

		/* break line into command and up to two parameters */
		cmd = strtok(line, DELIM);
		/* ignore empty lines */
		if (!cmd || *cmd == '#')
			continue;

		p1 = strtok(NULL, DELIM);
		if (p1) {
			l1 = strlen(p1);
			hash = icase ? strihash(p1) : strhash(p1);
			p2 = strtok(NULL, DELIM);
			if (p2)
				l2 = strlen(p2);
		}

		if (!strcmp("hash", cmd) && l1) {

			/* print results of different hash functions */
			printf("%u %u %u %u\n", strhash(p1), memhash(p1, l1),
					strihash(p1), memihash(p1, l1));

		} else if (!strcmp("add", cmd) && l1 && l2) {

			/* create entry with key = p1, value = p2 */
			entry = alloc_test_entry(hash, p1, l1, p2, l2);

			/* add to hashmap */
			hashmap_add(&map, entry);

		} else if (!strcmp("put", cmd) && l1 && l2) {

			/* create entry with key = p1, value = p2 */
			entry = alloc_test_entry(hash, p1, l1, p2, l2);

			/* add / replace entry */
			entry = hashmap_put(&map, entry);

			/* print and free replaced entry, if any */
			puts(entry ? get_value(entry) : "NULL");
			free(entry);

		} else if (!strcmp("get", cmd) && l1) {

			/* setup static key */
			test_key key;
			hashmap_entry_init(&key, hash, 1);
			key.key = p1;

			/* lookup entry in hashmap */
			entry = hashmap_get(&map, &key);

			/* print result */
			if (!entry)
				puts("NULL");
			while (entry) {
				puts(get_value(entry));
				entry = hashmap_get_next(&map, entry);
			}

		} else if (!strcmp("remove", cmd) && l1) {

			/* setup static key */
			test_key key;
			hashmap_entry_init(&key, hash, 1);
			key.key = p1;

			/* remove entry from hashmap */
			entry = hashmap_remove(&map, &key);

			/* print result and free entry*/
			puts(entry ? get_value(entry) : "NULL");
			free(entry);

		} else if (!strcmp("iterate", cmd)) {

			hashmap_iter iter;
			hashmap_iter_init(&map, &iter);
			while ((entry = hashmap_iter_next(&iter)))
				printf("%s %s\n", get_key(entry), get_value(entry));

		} else if (!strcmp("size", cmd)) {

			/* print table sizes */
			printf("%u %u\n", map.tablesize, map.size);

		} else if (!strcmp("perfhashmap", cmd) && l1 && l2) {

			perf_hashmap(atoi(p1), atoi(p2));

		} else if (!strcmp("perfhashtable", cmd) && l1 && l2) {

			perf_hashtable(atoi(p1), atoi(p2));

		} else {

			printf("Unknown command %s\n", cmd);

		}
	}

	hashmap_free(&map, free);
	return 0;
}
