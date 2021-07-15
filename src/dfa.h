#ifndef LEX_DFA_H
#define LEX_DFA_H

#define uthash_malloc(sz) GC_malloc(sz)
#define uthash_free(ptr, sz)

#include "nfa.h"
#include <gc.h>
#include <uthash.h>
#include <vec.h>

typedef struct
{
    char id;
    struct DFA_NODE *ptr;
    UT_hash_handle hh;
} dfa_node_edge_t;

typedef struct DFA_NODE
{
    dfa_node_edge_t *edges;
    char *acceptString;
    anchor_t anchor;
    bitset_t *equivalentNfaIndices;
} dfa_node_t;

typedef vec_t(dfa_node_t *) vec_dfa_node_t;

typedef struct
{
    vec_dfa_node_t nodes;
    size_t start;
} dfa_t;

void DfaNodeInit(dfa_node_t *node);
void DfaNodeAddEdge(dfa_node_t *node, char id, dfa_node_t *ptr);
dfa_node_t *DfaNodeFollowEdge(dfa_node_t *node, char id);
dfa_t *ConstructDfa(nfa_t *nfa);

#endif // LEX_DFA_H
