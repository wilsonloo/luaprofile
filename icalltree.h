#ifndef _ICALLTREE_H_
#define _ICALLTREE_H_

#include <unistd.h>
#include <stdint.h>

struct icalltree_context;

struct icalltree_context* icalltree_create(uint64_t key, void* value);
void icalltree_free(struct icalltree_context* icalltree);

struct icalltree_context* icalltree_get_child(struct icalltree_context* icalltree, uint64_t key);
struct icalltree_context* icalltree_add_child(struct icalltree_context* icalltree, uint64_t key, void* value);
void* icalltree_getvalue(struct icalltree_context* icalltree);

typedef void(*observer)(uint64_t key, void* value, void* ud);
void icalltree_dump_children(struct icalltree_context* icalltree, observer observer_cb, void* ud);
size_t icalltree_children_size(struct icalltree_context* icalltree);


#endif
