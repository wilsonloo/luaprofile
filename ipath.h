#ifndef _IPATH_H_
#define _IPATH_H_

#include <unistd.h>
#include <stdint.h>

struct ipath_context;

struct ipath_context* ipath_create(uint64_t key, void* value);
void ipath_free(struct ipath_context* ipath);

struct ipath_context* ipath_get_child(struct ipath_context* ipath, uint64_t key);
struct ipath_context* ipath_add_child(struct ipath_context* ipath, uint64_t key, void* value);
void* ipath_getvalue(struct ipath_context* ipath);

typedef void(*observer)(uint64_t key, void* value, void* ud);
void ipath_dump_children(struct ipath_context* ipath, observer observer_cb, void* ud);
size_t ipath_children_size(struct ipath_context* ipath);


#endif
