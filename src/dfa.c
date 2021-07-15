#include "dfa.h"
#include "vec.h"

static void ComputeEpsilonClosure(nfa_t *nfa, bitset_t *set, char **accept, anchor_t *anchor);
static bitset_t *MoveOnChar(nfa_t *nfa, bitset_t *set, char c);
static dfa_node_t *FindDfaState(dfa_t *dfa, bitset_t *stateSet);

void DfaNodeInit(dfa_node_t *node)
{
    node->edges = NULL;
    node->acceptString = NULL;
    node->anchor = ANCHOR_NONE;
    node->equivalentNfaIndices = bitset_create();
}

void DfaNodeAddEdge(dfa_node_t *node, char id, dfa_node_t *ptr)
{
    dfa_node_edge_t *edge = GC_malloc(sizeof(dfa_node_edge_t));
    edge->id = id;
    edge->ptr = ptr;
    HASH_ADD(hh, node->edges, id, sizeof(char), edge);
}

dfa_node_t *DfaNodeFollowEdge(dfa_node_t *node, char id)
{
    dfa_node_edge_t *edge;
    HASH_FIND(hh, node->edges, &id, sizeof(char), edge);
    if (!edge)
    {
        return NULL;
    }
    return edge->ptr;
}

dfa_t *ConstructDfa(nfa_t *nfa)
{
    bitset_t *nfaSet = bitset_create();
    bitset_set(nfaSet, nfa->start);
    dfa_node_t *current = GC_malloc(sizeof(dfa_node_t));
    DfaNodeInit(current);
    ComputeEpsilonClosure(nfa, nfaSet, &current->acceptString, &current->anchor);
    current->equivalentNfaIndices = nfaSet;
    vec_dfa_node_t stack;
    vec_init(&stack);
    vec_push(&stack, current);
    dfa_t *dfa = GC_malloc(sizeof(dfa_t));
    vec_init(&dfa->nodes);
    dfa->start = 0;
    while (stack.length > 0)
    {
        char *acceptString;
        anchor_t anchor;
        dfa_node_t *current = vec_pop(&stack);
        for (int c = 0; c < 0x80; ++c)
        {
            dfa_node_t *nextState;
            nfaSet = MoveOnChar(nfa, current->equivalentNfaIndices, c);
            if (nfaSet)
            {
                ComputeEpsilonClosure(nfa, nfaSet, &acceptString, &anchor);
            }
            if (!nfaSet)
            {
                nextState = NULL;
            }
            else
            {
                nextState = FindDfaState(dfa, nfaSet);
                if (!nextState)
                {
                    nextState = GC_malloc(sizeof(dfa_node_t));
                    DfaNodeInit(nextState);
                    nextState->equivalentNfaIndices = nfaSet;
                    nextState->acceptString = acceptString;
                    nextState->anchor = anchor;
                    vec_push(&dfa->nodes, nextState);
                }
            }
            dfa_node_edge_t *edge = GC_malloc(sizeof(dfa_node_edge_t));
            edge->id = c;
            edge->ptr = nextState;
            HASH_ADD(hh, current->edges, id, sizeof(char), edge);
        }
    }
    return dfa;
}

static void ComputeEpsilonClosure(nfa_t *nfa, bitset_t *set, char **accept, anchor_t *anchor)
{
    vec_int_t stack;
    vec_init(&stack);
    *accept = NULL;
    int acceptNum = -1;
    for (int c = 0; c < nfa->nodes.length; ++c)
    {
        if (bitset_get(set, c))
        {
            vec_push(&stack, c);
        }
    }

    while (stack.length > 0)
    {
        int i = vec_pop(&stack);
        nfa_node_t *p = nfa->nodes.data[i];
        if (p->acceptString && (i < acceptNum || acceptNum == -1))
        {
            acceptNum = i;
            *accept = p->acceptString;
            *anchor = p->anchor;
        }

        if (p->edge == EDGE_EPSILON)
        {
            for (int i = 0; i < 2; ++i)
            {
                if (p->next[i])
                {
                    i = p->next[i]->index;
                    if (!bitset_get(set, i))
                    {
                        bitset_set(set, i);
                        vec_push(&stack, i);
                    }
                }
            }
        }
    }
}

static bitset_t *MoveOnChar(nfa_t *nfa, bitset_t *set, char c)
{
    bitset_t *outset = NULL;
    for (int i = 0; i < nfa->nodes.length; ++i)
    {
        if (bitset_get(set, i))
        {
            nfa_node_t *p = nfa->nodes.data[i];
            if (p->edge == c ||
                (p->edge == EDGE_CHARACTER_CLASS && bitset_get(p->characterClass, c)))
            {
                if (!outset)
                {
                    outset = bitset_create();
                }
                bitset_set(outset, p->index);
            }
        }
    }
    return outset;
}

static dfa_node_t *FindDfaState(dfa_t *dfa, bitset_t *stateSet)
{
    dfa_node_t *node;
    int i;
    vec_foreach(&dfa->nodes, node, i)
    {
        if (bitsets_equal(node->equivalentNfaIndices, stateSet))
        {
            return node;
        }
    }
    return NULL;
}
