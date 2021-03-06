#include <std/map.h>
#include <std/malloc.h>

static void insert(list_t *buckets, size_t num_buckets, string_t key,
                   void *value) {
    struct map_elem *e = malloc(sizeof(*e));
    e->key = string_dup(key);
    e->value = value;
    list_push_back(MAP_BUCKET(buckets, num_buckets, key), &e->next);
}

static void rebalance(map_t map) {
    if (map->len / map->num_buckets < MAP_REBALANCE_THRESHOLD) {
        return;
    }

    // Create a new bucket with larger capacity.
    size_t new_num_buckets = map->num_buckets * 4;
    list_t *new_buckets = malloc(sizeof(list_t) * new_num_buckets);
    for (size_t i = 0; i < new_num_buckets; i++) {
        list_init(&new_buckets[i]);
    }

    // Move elements into the new buckets.
    for (size_t i = 0; i < map->num_buckets; i++) {
        LIST_FOR_EACH(e, &map->buckets[i], struct map_elem, next) {
            insert(new_buckets, new_num_buckets, e->key, e->value);
            free(e);
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->num_buckets = new_num_buckets;
}

static struct map_elem *search(map_t map, string_t key) {
    LIST_FOR_EACH(e, MAP_BUCKET(map->buckets, map->num_buckets, key),
                  struct map_elem, next) {
        if (string_equals(e->key, key)) {
            return e;
        }
    }

    return NULL;
}

map_t map_new(void) {
    map_t map = malloc(sizeof(*map));
    map->len = 0;
    map->num_buckets = 1;
    map->buckets = malloc(sizeof(list_t) * map->num_buckets);
    for (size_t i = 0; i < map->num_buckets; i++) {
        list_init(&map->buckets[i]);
    }
    return map;
}

void map_delete(map_t map) {
    for (size_t i = 0; i < map->num_buckets; i++) {
        LIST_FOR_EACH(e, &map->buckets[i], struct map_elem, next) {
            free(e);
        }
    }
    free(map->buckets);
    free(map);
}

size_t map_len(map_t map) {
    return map->len;
}

bool map_is_empty(map_t map) {
    return map->len == 0;
}

void *map_get(map_t map, string_t key) {
    struct map_elem *e = search(map, key);
    return e ? e->value : NULL;
}

void *map_set(map_t map, string_t key, void *value) {
    rebalance(map);

    struct map_elem *e = search(map, key);
    if (e) {
        void *old_value = e->value;
        e->value = value;
        return old_value;
    }

    insert(map->buckets, map->num_buckets, string_dup(key), value);
    map->len++;
    return NULL;
}

void *map_remove(map_t map, string_t key) {
    struct map_elem *e = search(map, key);
    if (e) {
        void *value = e->value;
        list_remove(&e->next);
        string_delete(e->key);
        free(e);
        map->len--;
        return value;
    } else {
        return NULL;
    }
}

void *map_get_handle(map_t map, handle_t *key) {
    string_t tmp = string_from_bytes(key, sizeof(*key));
    return map_get(map, tmp);
}

void *map_set_handle(map_t map, handle_t *key, void *value) {
    string_t tmp = string_from_bytes(key, sizeof(*key));
    return map_set(map, tmp, value);
}

void *map_remove_handle(map_t map, handle_t *key) {
    string_t tmp = string_from_bytes(key, sizeof(*key));
    return map_remove(map, tmp);
}
