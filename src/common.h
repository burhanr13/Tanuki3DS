#ifndef TYPES_H
#define TYPES_H

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define eprintf(format, ...) fprintf(stderr, format __VA_OPT__(, ) __VA_ARGS__)
#define printfln(format, ...) printf(format "\n" __VA_OPT__(, ) __VA_ARGS__)

extern bool g_infologs;

#define linfo(format, ...)                                                     \
    (g_infologs ? printf("\e[32m[INFO](%s) " format "\e[0m\n",                 \
                         __func__ __VA_OPT__(, ) __VA_ARGS__)                  \
                : (void) 0)

#define ldebug(format, ...)                                                    \
    printf("\e[36m[DEBUG](%s) " format "\e[0m\n",                              \
           __func__ __VA_OPT__(, ) __VA_ARGS__)

#define lwarn(format, ...)                                                     \
    printf("\e[33m[WARNING](%s) " format "\e[0m\n",                            \
           __func__ __VA_OPT__(, ) __VA_ARGS__)
#define lerror(format, ...)                                                    \
    printf("\e[31m[ERROR](%s) " format "\e[0m\n",                              \
           __func__ __VA_OPT__(, ) __VA_ARGS__)

#define print_vec4(v) printf("[%f,%f,%f,%f] ", (v)[0], (v)[1], (v)[2], (v)[3])

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

#define ubi(n) unsigned _BitInt(n)
#define sbi(n) signed _BitInt(n)

typedef float fvec2[2];
typedef float fvec4[4];

#define BITCAST(from, to, x)                                                   \
    (((union {                                                                 \
         from _f;                                                              \
         to _t;                                                                \
     }) {x})                                                                   \
         ._t)

#define F2I(f) BITCAST(float, u32, f)
#define I2F(i) BITCAST(u32, float, i)

#define BIT(n) (1u << (n))
#define BITL(n) (1ull << (n))
#define MASK(n) (BIT(n) - 1)
#define MASKL(n) (BITL(n) - 1)

#define INVBIT2(n) ((n) & (MASK(1) << 1) ? 1 : 0)
#define INVBIT4(n) ((n) & (MASK(2) << 2) ? 2 + INVBIT2(n >> 2) : INVBIT2(n))
#define INVBIT8(n) ((n) & (MASK(4) << 4) ? 4 + INVBIT4(n >> 4) : INVBIT4(n))
#define INVBIT16(n) ((n) & (MASK(8) << 8) ? 8 + INVBIT8(n >> 8) : INVBIT8(n))
#define INVBIT(n)                                                              \
    ((n) & (MASK(16) << 16) ? 16 + INVBIT16(n >> 16) : INVBIT16(n))
#define INVBITL(n) ((n) & (MASKL(32) << 32) ? 32 + INVBIT(n >> 32) : INVBIT(n))

#define FIFO(T, N)                                                             \
    struct {                                                                   \
        T d[N];                                                                \
        ubi(INVBIT(N)) head;                                                   \
        ubi(INVBIT(N)) tail;                                                   \
        u32 size;                                                              \
    }

#define FIFO_MAX(f) (sizeof((f).d) / sizeof((f).d[0]))
#define FIFO_push(f, v) ((f).d[(f).tail++] = v, (f).size++)
#define FIFO_pop(f, v) (v = (f).d[(f).head++], (f).size--)
#define FIFO_peek(f) ((f).d[(f).head])
#define FIFO_back(f) ((f).d[(f).tail - (typeof((f).tail)) 1])
#define FIFO_foreach(i, f)                                                     \
    for (u32 _i = 0, i = (f).head; _i < (f).size;                              \
         _i++, i = (typeof((f).head)) ((f).head + _i))
#define FIFO_clear(f) ((f).head = (f).tail = (f).size = 0)

#define StaticVector(T, N)                                                     \
    struct {                                                                   \
        T d[N];                                                                \
        size_t size;                                                           \
    }

#define SVec_MAX(v) (sizeof((v).d) / sizeof((v).d[0]))
#define SVec_push(v, e)                                                        \
    ({                                                                         \
        (v).d[(v).size++] = (e);                                               \
        (v).size - 1;                                                          \
    })
#define SVec_full(v) ((v).size == SVec_MAX(v))

#define Vector(T)                                                              \
    struct {                                                                   \
        T* d;                                                                  \
        size_t size;                                                           \
        size_t cap;                                                            \
    }

#define Vec_init(v) ((v).d = nullptr, (v).size = 0, (v).cap = 0)
#define Vec_assn(v1, v2)                                                       \
    ((v1).d = (v2).d, (v1).size = (v2).size, (v1).cap = (v2).cap)
#define Vec_free(v) (free((v).d), Vec_init(v))
#define Vec_push(v, e)                                                         \
    ({                                                                         \
        if ((v).size == (v).cap) {                                             \
            (v).cap = (v).cap ? 2 * (v).cap : 8;                               \
            (v).d = (typeof((v).d)) realloc((v).d, (v).cap * sizeof *(v).d);   \
        }                                                                      \
        (v).d[(v).size++] = (e);                                               \
        (v).size - 1;                                                          \
    })
#define Vec_remove(v, i)                                                       \
    ({                                                                         \
        (v).size--;                                                            \
        for (int j = i; j < (v).size; j++) {                                   \
            (v).d[j] = (v).d[j + 1];                                           \
        }                                                                      \
    })
#define SVec_remove Vec_remove
#define Vec_foreach(e, v) for (auto* e = (v).d; e < (v).d + (v).size; e++)
#define SVec_foreach Vec_foreach

// T must have fields: u64 key, T* next, T* prev
// key=0 is empty
#define LRUCache(T, N)                                                         \
    struct {                                                                   \
        T d[N];                                                                \
        T root;                                                                \
        size_t size;                                                           \
    }

#define LRU_MAX(c) (sizeof((c).d) / sizeof((c).d[0]))

#define LRU_init(c) ((c).root.next = (c).root.prev = &(c).root, (c).size = 0)

#define LL_remove(n)                                                           \
    ({                                                                         \
        (n)->next->prev = (n)->prev;                                           \
        (n)->prev->next = (n)->next;                                           \
        (n)->prev = (n)->next = nullptr;                                       \
    })

#define LRU_remove(c, e)                                                       \
    ({                                                                         \
        LL_remove(e);                                                          \
        (e)->key = 0;                                                          \
        (c).size--;                                                            \
    })

#define LRU_use(c, e)                                                          \
    ({                                                                         \
        if (!(e)->prev && !(e)->next) (c).size++;                              \
        if ((e)->prev) (e)->prev->next = (e)->next;                            \
        if ((e)->next) (e)->next->prev = (e)->prev;                            \
        (e)->next = (c).root.next;                                             \
        (e)->next->prev = (e);                                                 \
        (c).root.next = (e);                                                   \
        (e)->prev = &(c).root;                                                 \
    })

#define LRU_eject(c)                                                           \
    ({                                                                         \
        auto e = (c).root.prev;                                                \
        (c).root.prev = (c).root.prev->prev;                                   \
        (c).root.prev->next = &(c).root;                                       \
        e->next = e->prev = nullptr;                                           \
        (c).size--;                                                            \
        e;                                                                     \
    })

#define LRU_mru(c) ((c).root.next)

#define LRU_load(c, k)                                                         \
    ({                                                                         \
        assert(k);                                                             \
        typeof(c.root)* ent = nullptr;                                         \
        for (int i = 0; i < LRU_MAX(c); i++) {                                 \
            if ((c).d[i].key == (k) || (c).d[i].key == 0) {                    \
                ent = &(c).d[i];                                               \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if (!ent) {                                                            \
            ent = LRU_eject(c);                                                \
        }                                                                      \
        LRU_use((c), ent);                                                     \
        ent;                                                                   \
    })

#ifndef __cplusplus

typedef struct {
    char* str;
    char* cur;
    char* end;
} DynString;

static inline void ds_init(DynString* s, size_t len) {
    s->str = malloc(len);
    s->cur = s->str;
    s->end = s->str + len;

    *s->cur = '\0';
}

static inline void ds_free(DynString* s) {
    free(s->str);
    *s = (DynString) {};
}

static inline void ds_printf(DynString* s, const char* fmt, ...) {
    va_list v;
    while (true) {
        int rem = s->end - s->cur;
        va_start(v);
        int n = vsnprintf(s->cur, rem, fmt, v);
        va_end(v);

        if (n < rem) {
            s->cur += n;
            return;
        }

        auto curoff = s->cur - s->str;
        auto curlen = s->end - s->str;
        s->str = realloc(s->str, 2 * curlen);
        s->cur = s->str + curoff;
        s->end = s->str + 2 * curlen;
    }
}

#endif

#endif