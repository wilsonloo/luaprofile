#include "profile.h"
#include "icalltree.h"
#include "imap.h"

struct icalltree_context {
    uint64_t key;
    void* value;
    struct imap_context* children;
};

struct icalltree_context* icalltree_create(uint64_t key, void* value) {
    struct icalltree_context* icalltree = (struct icalltree_context*)pmalloc(sizeof(*icalltree));
    icalltree->key = key;
    icalltree->value = value;
    icalltree->children = imap_create();

    return icalltree;
}

void icalltree_free_child(uint64_t key, void* value, void* ud) {
    icalltree_free((struct icalltree_context*)value);
}
void icalltree_free(struct icalltree_context* icalltree) {
    if (icalltree->value) {
        pfree(icalltree->value);
        icalltree->value = NULL;
    }
    imap_dump(icalltree->children, icalltree_free_child, NULL);
    imap_free(icalltree->children);
    pfree(icalltree);
}

struct icalltree_context* icalltree_get_child(struct icalltree_context* icalltree, uint64_t key) {
    void* child_path = imap_query(icalltree->children, key);
    return (struct icalltree_context*)child_path;
}

struct icalltree_context* icalltree_add_child(struct icalltree_context* icalltree, uint64_t key, void* value) {
    struct icalltree_context* child_path = icalltree_create(key, value);
    imap_set(icalltree->children, key, child_path);
    return child_path;
}

void* icalltree_getvalue(struct icalltree_context* icalltree) {
    return icalltree->value;
}

void icalltree_dump_children(struct icalltree_context* icalltree, observer observer_cb, void* ud) {
    imap_dump(icalltree->children, observer_cb, ud);
}

size_t icalltree_children_size(struct icalltree_context* icalltree) {
    return imap_size(icalltree->children);
}