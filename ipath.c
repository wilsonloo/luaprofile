#include "profile.h"
#include "ipath.h"
#include "imap.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

struct ipath_context {
    uint64_t key;
    void* value;
    struct imap_context* children;
};

struct ipath_context* ipath_create(uint64_t key, void* value) {
    struct ipath_context* ipath = (struct ipath_context*)pmalloc(sizeof(*ipath));
    ipath->key = key;
    ipath->value = value;
    ipath->children = imap_create();

    return ipath;
}

void ipath_free_child(uint64_t key, void* value, void* ud) {
    ipath_free((struct ipath_context*)value);
}
void ipath_free(struct ipath_context* ipath) {
    if (ipath->value) {
        pfree(ipath->value);
        ipath->value = NULL;
    }
    imap_dump(ipath->children, ipath_free_child, NULL);
    imap_free(ipath->children);
    pfree(ipath);
}

struct ipath_context* ipath_get_child(struct ipath_context* ipath, uint64_t key) {
    void* child_path = imap_query(ipath->children, key);
    return (struct ipath_context*)child_path;
}

struct ipath_context* ipath_add_child(struct ipath_context* ipath, uint64_t key, void* value) {
    struct ipath_context* child_path = ipath_create(key, value);
    imap_set(ipath->children, key, child_path);
    return child_path;
}

void* ipath_getvalue(struct ipath_context* ipath) {
    return ipath->value;
}

void ipath_dump_children(struct ipath_context* ipath, observer observer_cb, void* ud) {
    imap_dump(ipath->children, observer_cb, ud);
}

size_t ipath_children_size(struct ipath_context* ipath) {
    return imap_size(ipath->children);
}