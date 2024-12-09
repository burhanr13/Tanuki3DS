#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define eprintf(format, ...) fprintf(stderr, format __VA_OPT__(, ) __VA_ARGS__)
#define println() printf("\n")
#define printfln(format, ...) printf(format "\n"__VA_OPT__(, ) __VA_ARGS__)

extern bool g_infologs;

#define linfo(format, ...)                                                     \
    (g_infologs ? printf("\e[32m[INFO](%s) " format "\e[0m\n",                 \
                         __func__ __VA_OPT__(, ) __VA_ARGS__)                  \
                : (void) 0)

#define lwarn(format, ...)                                                     \
    printf("\e[43;30m[WARNING](%s) " format "\e[0m\n",                         \
           __func__ __VA_OPT__(, ) __VA_ARGS__)
#define lerror(format, ...)                                                    \
    printf("\e[41m[ERROR](%s) " format "\e[0m\n",                              \
           __func__ __VA_OPT__(, ) __VA_ARGS__)

#define print_fvec(v) printf("[%f,%f,%f,%f] ", (v)[0], (v)[1], (v)[2], (v)[3])

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

typedef float fvec[4];
typedef float fvec2[2];

#define BIT(n) (1U << (n))
#define BITL(n) (1UL << (n))
#define MASK(n) (BIT(n) - 1)
#define MASKL(n) (BITL(n) - 1)

#define FIFO(T, N)                                                             \
    struct {                                                                   \
        T d[N];                                                                \
        u32 head;                                                              \
        u32 tail;                                                              \
        u32 size;                                                              \
    }

#define FIFO_MAX(f) (sizeof((f).d) / sizeof((f).d[0]))
#define FIFO_push(f, v)                                                        \
    ((f).d[(f).tail++] = v, (f).tail &= FIFO_MAX(f) - 1, (f).size++)
#define FIFO_pop(f, v)                                                         \
    (v = (f).d[(f).head++], (f).head &= FIFO_MAX(f) - 1, (f).size--)
#define FIFO_peek(f) ((f).d[(f).head])
#define FIFO_foreach(i, f)                                                     \
    for (u32 _i = 0, i = (f).head; _i < (f).size;                              \
         _i++, i = (i + 1) & (FIFO_MAX(f) - 1))
#define FIFO_clear(f) ((f).d[0] = (f).head = (f).tail = (f).size = 0)

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

#define Vec_init(v) ((v).d = NULL, (v).size = 0, (v).cap = 0)
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
#define Vec_foreach(e, v)                                                      \
    for (typeof((v).d[0])* e = (v).d; e < (v).d + (v).size; e++)
#define SVec_foreach Vec_foreach

#define LRUCache(T, N)                                                         \
    struct {                                                                   \
        T d[N];                                                                \
        T root;                                                                \
        size_t size;                                                           \
    }

#define LRU_init(c) ((c).root.next = (c).root.prev = &(c).root, (c).size = 0)

#define LL_remove(n)                                                           \
    ({                                                                         \
        (n)->next->prev = (n)->prev;                                           \
        (n)->prev->next = (n)->next;                                           \
        (n)->prev = (n)->next = NULL;                                          \
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
        typeof(&(c).root) e = (c).root.prev;                                   \
        (c).root.prev = (c).root.prev->prev;                                   \
        (c).root.prev->next = &(c).root;                                       \
        e->next = e->prev = NULL;                                              \
        (c).size--;                                                            \
        e;                                                                     \
    })

#endif