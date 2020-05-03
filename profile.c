#include "profile.h"
#include "imap.h"
#include "icalltree.h"

#include "lobject.h"
#include "lstate.h"

#define MAX_CALL_SIZE               1024
#define MAX_CO_SIZE                 1024
#define NANOSEC                     1000000000
#define MICROSEC                    1000000

static const char KEY = 'k';


#ifdef USE_RDTSC
    #include "rdtsc.h"
    static inline uint64_t
    gettime() {
        return rdtsc();
    }

    static inline double
    realtime(uint64_t t) {
        return (double) t / (2000000000);
    }
#else
    static inline uint64_t
    gettime() {
        struct timespec ti;
        // clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);
        // clock_gettime(CLOCK_MONOTONIC, &ti);  
        clock_gettime(CLOCK_REALTIME, &ti);  // would be faster

        long sec = ti.tv_sec & 0xffff;
        long nsec = ti.tv_nsec;

        return sec * NANOSEC + nsec;
    }

    static inline double
    realtime(uint64_t t) {
        return (double)t / NANOSEC;
    }
#endif



struct call_frame {
    const void* point;
    const void* prototype;
    bool  tail;
    uint64_t record_time;
    uint64_t call_time;
    uint64_t ret_time;
    uint64_t sub_cost;
    uint64_t real_cost;
    uint64_t alloc_count;
};

struct call_state {
    lua_State*  co;
    uint64_t    leave_time;
    int         top;
    struct call_frame   call_list[0];
};

struct profile_context {
    uint64_t start;
    struct imap_context* cs_map;
    struct icalltree_context* calltree;
    struct call_state* cur_cs;
};

struct calltree_node {
    struct calltree_node* parent;
    const void* point;
    const char* source;
    const char* name;
    char flag;
    int line;
    int depth;
    uint64_t ret_time;
    uint64_t count;
    uint64_t record_time;
    uint64_t alloc_count;
};

static struct calltree_node* 
calltree_node_create() {
    struct calltree_node* node = (struct calltree_node*)pmalloc(sizeof(*node));
    node->point = NULL;
    node->source = NULL;
    node->name = NULL;
    node->line = 0;
    node->depth = 0;
    node->ret_time = 0;
    node->count = 0;
    node->record_time = 0;
    node->parent = NULL;
    node->alloc_count = 0;
    node->flag = '\0';
    return node;
}


static struct profile_context *
profile_create() {
    struct profile_context* context = (struct profile_context*)pmalloc(sizeof(*context));
    
    context->start = 0;
    context->cs_map = imap_create();
    context->calltree = NULL;
    context->cur_cs = NULL;
    return context;
}

static void
_ob_free_call_state(uint64_t key, void* value, void* ud) {
    pfree(value);
}
static void
profile_free(struct profile_context* context) {
    if (context->calltree) {
        icalltree_free(context->calltree);
        context->calltree = NULL;
    }

    imap_dump(context->cs_map, _ob_free_call_state, NULL);
    imap_free(context->cs_map);
    pfree(context);
}


static inline struct call_frame *
push_callframe(struct call_state* cs) {
    if(cs->top >= MAX_CALL_SIZE) {
        assert(false);
    }
    return &cs->call_list[cs->top++];
}

static inline struct call_frame *
pop_callframe(struct call_state* cs) {
    if(cs->top<=0) {
        assert(false);
    }
    return &cs->call_list[--cs->top];
}

static inline struct call_frame *
cur_callframe(struct call_state* cs) {
    if(cs->top<=0) {
        return NULL;
    }

    uint64_t idx = cs->top-1;
    return &cs->call_list[idx];
}


static inline struct profile_context *
_get_profile(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void *)&KEY);
    struct profile_context* addr = (struct profile_context*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return addr;
}

static struct icalltree_context* 
get_frame_path(struct profile_context* context, struct call_state* cs, lua_Debug* far) {
    if (!context->calltree) {
        struct calltree_node* node = calltree_node_create();
        node->name = "total";
        node->source = node->name;
        context->calltree = icalltree_create(0, node);
    }
    struct icalltree_context* path = context->calltree;

    int i = 0;
    for (i = 0; i < cs->top; i++) {
        struct call_frame* cur_cf = &(cs->call_list[i]);
        uint64_t k = (uint64_t)((uintptr_t)cur_cf->prototype);
        struct icalltree_context* child_path = icalltree_get_child(path, k);
        if (!child_path) {
            struct calltree_node* path_parent = (struct calltree_node*)icalltree_getvalue(path);
            struct calltree_node* node = calltree_node_create();

            node->point = cur_cf->prototype;
            node->parent = path_parent;
            node->depth = path_parent->depth + 1;
            node->ret_time = 0;
            node->record_time = 0;
            node->count = 0;
            node->alloc_count = 0;
            child_path = icalltree_add_child(path, k, node);
        }

        path = child_path;
    }
    
    struct calltree_node* cur_node = (struct calltree_node*)icalltree_getvalue(path);
    if (cur_node->name == NULL) {
        const char* name = NULL;
        #ifdef USE_EXPORT_NAME
            lua_getinfo(cs->co, "nSl", far);
            name = far->name;
        #else
            lua_getinfo(cs->co, "Sl", far);
        #endif
        int line = far->linedefined;
        const char* source = far->source;
        char flag = far->what[0];
        if (flag == 'C') {
            lua_Debug ar2;
            int i=0;
            int ret = 0;
            do {
                i++;
                ret = lua_getstack(cs->co, i, &ar2);
                flag = 'C';
                if(ret) {
                    lua_getinfo(cs->co, "Sl", &ar2);
                    if(ar2.what[0] != 'C') {
                        line = ar2.currentline;
                        source = ar2.source;
                        break;
                    }
                }
            }while(ret);
        }

        cur_node->name = name ? name : "null";
        cur_node->source = source ? source : "null";
        cur_node->line = line;
        cur_node->flag = flag;
    }
    
    return path;
}

static void
_resolve_hook(lua_State* L, lua_Debug* far) {
    uint64_t cur_time = gettime();
    struct profile_context* context = _get_profile(L);
    if(context->start == 0) {
        return;
    }

    int event = far->event;
    struct call_state* cs = context->cur_cs;
    if (!context->cur_cs || context->cur_cs->co != L) {
        uint64_t key = (uint64_t)((uintptr_t)L);
        cs = imap_query(context->cs_map, key);
        if (cs == NULL) {
            cs = (struct call_state*)pmalloc(sizeof(struct call_state) + sizeof(struct call_frame)*MAX_CALL_SIZE);
            cs->co = L;
            cs->top = 0;
            cs->leave_time = 0;
            imap_set(context->cs_map, key, cs);
        }

        if (context->cur_cs) {
            context->cur_cs->leave_time = cur_time;
        }
        context->cur_cs = cs;
    }
    if (cs->leave_time > 0) {
        assert(cur_time >= cs->leave_time);
        uint64_t co_cost = cur_time - cs->leave_time;

        int i = 0;
        for (; i < cs->top; i++) {
            cs->call_list[i].sub_cost += co_cost;
        }
        cs->leave_time = 0;
    }
    assert(cs->co == L);

    if (event == LUA_HOOKCALL || event == LUA_HOOKTAILCALL) {
        lua_getinfo(L, "f", far);
        const void* point = lua_topointer(L, -1);

        struct call_frame* frame = push_callframe(cs);
        frame->point = point;
        frame->tail = event == LUA_HOOKTAILCALL;
        frame->record_time = cur_time;
        frame->sub_cost = 0;
        frame->call_time = gettime();

        frame->prototype = point;
        if (far->i_ci && ttisclosure(far->i_ci->func)) {
            Closure *cl = clvalue(far->i_ci->func);
            if (cl && cl->c.tt == LUA_TLCL) {
                frame->prototype = cl->l.p;
            }
        }
    } else if (event == LUA_HOOKRET) {
        int len = cs->top;
        if(len <= 0) {
            return;
        }
        bool tail_call = false;
        do {
            struct calltree_node* cur_path = (struct calltree_node*)icalltree_getvalue(get_frame_path(context, cs, far));

            struct call_frame* cur_frame = pop_callframe(cs);
            uint64_t total_cost = cur_time - cur_frame->call_time;
            uint64_t real_cost = total_cost - cur_frame->sub_cost;
            assert(cur_time >= cur_frame->call_time && total_cost >= cur_frame->sub_cost);
            cur_frame->ret_time = cur_time;
            cur_frame->real_cost = real_cost;

            cur_path->ret_time = cur_path->ret_time == 0 ? cur_time : cur_path->ret_time;
            cur_path->record_time += real_cost;
            cur_path->count++;

            struct call_frame* pre_frame = cur_callframe(cs);
            tail_call = pre_frame ? cur_frame->tail : false;
        }while(tail_call);
    }
}


struct dump_call_path_arg {
    lua_State* L;
    uint64_t record_time;
    uint64_t count;
    uint64_t index;
};

static void _dump_call_path(struct icalltree_context* path, struct dump_call_path_arg* arg);
static void _dump_call_path_child(uint64_t key, void* value, void* ud) {
    struct dump_call_path_arg* arg = (struct dump_call_path_arg*)ud;
    _dump_call_path((struct icalltree_context*)value, arg);
    lua_seti(arg->L, -2, ++arg->index);
}
static void _dump_call_path(struct icalltree_context* path, struct dump_call_path_arg* arg) {
    lua_checkstack(arg->L, 3);
    lua_newtable(arg->L);

    struct dump_call_path_arg child_arg;
    child_arg.L = arg->L;
    child_arg.record_time = 0;
    child_arg.count = 0;
    child_arg.index = 0;

    if (icalltree_children_size(path) > 0) {
        lua_newtable(arg->L);
        icalltree_dump_children(path, _dump_call_path_child, &child_arg);
        lua_setfield(arg->L, -2, "children");
    }

    struct calltree_node* node = (struct calltree_node*)icalltree_getvalue(path);
    uint64_t count = node->count > child_arg.count ? node->count : child_arg.count;
    uint64_t rt = realtime(node->record_time) * MICROSEC;
    uint64_t record_time = rt > child_arg.record_time ? rt : child_arg.record_time;

    arg->record_time += record_time;
    arg->count += count;

    char name[512] = {0};
    snprintf(name, sizeof(name)-1, "%s %s:%d", node->name ? node->name : "", node->source ? node->source : "", node->line);
    lua_pushstring(arg->L, name);
    lua_setfield(arg->L, -2, "name");

    lua_pushinteger(arg->L, count);
    lua_setfield(arg->L, -2, "count");

    lua_pushinteger(arg->L, record_time);
    lua_setfield(arg->L, -2, "value");

    lua_pushinteger(arg->L, node->ret_time);
    lua_setfield(arg->L, -2, "rettime");
}
static void dump_call_path(lua_State* L, struct icalltree_context* path) {
    struct dump_call_path_arg arg;
    arg.L = L;
    arg.record_time = 0;
    arg.count = 0;
    arg.index = 0;
    _dump_call_path(path, &arg);
}


static int 
get_all_coroutines(lua_State* L, lua_State** result, int maxsize) {
    int i = 0;
    struct global_State* lG = L->l_G;
    result[i++] = lG->mainthread;

    struct GCObject* obj = lG->allgc;
    while (obj && i < maxsize) {
        if (obj->tt == LUA_TTHREAD) {
            result[i++] = gco2th(obj);
        }
        obj = obj->next;
    }
    return i;
}

static int
_lstart(lua_State* L) {
    struct profile_context* context = _get_profile(L);
    if (context) {
        return 0;
    }

    // init registry
    context = profile_create();
    lua_pushlightuserdata(L, context);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void *)&KEY);
    context->start = gettime();

    lua_State* states[MAX_CO_SIZE] = {0};
    int i = get_all_coroutines(L, states, MAX_CO_SIZE);
    for (i = i - 1; i >= 0; i--) {
        lua_sethook(states[i], _resolve_hook, LUA_MASKCALL | LUA_MASKRET, 0);
    }
    return 0;
}

static int
_lstop(lua_State* L) {
    struct profile_context* context = _get_profile(L);
    if (!context) {
        return 0;
    }
    lua_State* states[MAX_CO_SIZE] = {0};
    int i = get_all_coroutines(L, states, MAX_CO_SIZE);
    for (i = i - 1; i >= 0; i--) {
        lua_sethook(states[i], NULL, 0, 0);
    }
    profile_free(context);

    lua_pushlightuserdata(L, (void *)&KEY);
    lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);
    return 0;
}

static int
_lmark(lua_State* L) {
    struct profile_context* context = _get_profile(L);
    if (!context) {
        return 0;
    }
    lua_State* co = lua_tothread(L, 1);
    if(co == NULL) {
        co = L;
    }
    if(context->start != 0) {
        lua_sethook(co, _resolve_hook, LUA_MASKCALL | LUA_MASKRET, 0);
    }
    lua_pushboolean(L, context->start != 0);
    return 1;
}

static int
_lunmark(lua_State* L) {
    struct profile_context* context = _get_profile(L);
    if (!context) {
        return 0;
    }
    lua_State* co = lua_tothread(L, 1);
    if(co == NULL) {
        co = L;
    }
    lua_sethook(co, NULL, 0, 0);
    return 0;
}

static int
_ldump(lua_State* L) {
    struct profile_context* context = _get_profile(L);
    if (context && context->calltree) {
        uint64_t record_time = realtime(gettime() - context->start) * MICROSEC;
        lua_pushinteger(L, record_time);
        dump_call_path(L, context->calltree);
        return 2;
    }
    return 0;
}

int
luaopen_profile_c(lua_State* L) {
    luaL_checkversion(L);
     luaL_Reg l[] = {
        {"start", _lstart},
        {"stop", _lstop},
        {"mark", _lmark},
        {"unmark", _lunmark},
        {"dump", _ldump},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    return 1;
}