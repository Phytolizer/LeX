#ifndef LEX_NFA_H
#define LEX_NFA_H

#define uthash_malloc(sz) GC_malloc(sz)
#define uthash_free(ptr, sz)

#include <bitset.h>
#include <gc.h>
#include <stdbool.h>
#include <uthash.h>
#include <vec.h>

typedef enum
{
    EDGE_EMPTY = -3,
    EDGE_CHARACTER_CLASS = -2,
    EDGE_EPSILON = -1,
} edge_t;

typedef enum
{
    ANCHOR_NONE = 0,
    ANCHOR_LINE_START = 1 << 0,
    ANCHOR_LINE_END = 1 << 1,
    ANCHOR_BOTH = ANCHOR_LINE_START | ANCHOR_LINE_END,
} anchor_t;

typedef struct NFA
{
    char *acceptString;
    struct NFA *next[2];
    edge_t edge;
    anchor_t anchor;
    bitset_t *characterClass;
    bool inverted;
    size_t index;
} nfa_node_t;

void NfaNodeInit(nfa_node_t *node);

typedef vec_t(nfa_node_t *) vec_nfa_node_t;

typedef struct
{
    vec_nfa_node_t nodes;
    size_t start;
} nfa_t;

typedef struct
{
    char *name;
    char *definition;
    UT_hash_handle hh;
} macro_t;

nfa_t *ConstructNfa(const char *regex, size_t len, macro_t *macros);

#endif // LEX_NFA_H
