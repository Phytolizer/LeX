#include "nfa.h"
#include <string.h>

typedef vec_t(size_t) vec_size_t;

typedef struct
{
    nfa_node_t *start;
    nfa_node_t *end;
} nfa_node_pair_t;

typedef struct
{
    vec_nfa_node_t nodes;
    vec_size_t discardedNodes;
    char *inputBuf;
    char *input;
    char lexeme;
    macro_t *macros;
} regex_parser_state_t;

static size_t AllocateNfaNode(regex_parser_state_t *state);
static void DiscardNfaNode(regex_parser_state_t *state, size_t node);
static int ThompsonConstruct(regex_parser_state_t *state);

void NfaNodeInit(nfa_node_t *node)
{
    node->acceptString = NULL;
    node->anchor = ANCHOR_NONE;
    node->edge = EDGE_EMPTY;
    node->characterClass = bitset_create();
    node->next[0] = NULL;
    node->next[1] = NULL;
}

void NfaNodeDeinit(nfa_node_t *node)
{
    bitset_free(node->characterClass);
}

nfa_t *ConstructNfa(const char *regex, size_t len, macro_t *macros)
{
    regex_parser_state_t parserState = {
        .input = NULL, .inputBuf = malloc(len), .lexeme = '\0', .macros = NULL};
    strncpy(parserState.inputBuf, regex, len);
    parserState.input = parserState.inputBuf;
    vec_init(&parserState.nodes);
    vec_init(&parserState.discardedNodes);
    nfa_t *nfa = malloc(sizeof(nfa_t));
    nfa->start = ThompsonConstruct(&parserState);
    nfa->nodes = parserState.nodes;
    vec_deinit(&parserState.discardedNodes);
    free(parserState.inputBuf);
    return nfa;
}

static size_t AllocateNfaNode(regex_parser_state_t *state)
{
    if (state->discardedNodes.length > 0)
    {
        return vec_pop(&state->discardedNodes);
    }

    nfa_node_t *node = malloc(sizeof(nfa_node_t));
    NfaNodeInit(node);
    vec_push(&state->nodes, node);
    return state->nodes.length - 1;
}

static void DiscardNfaNode(regex_parser_state_t *state, size_t node)
{
    vec_push(&state->discardedNodes, node);
    NfaNodeDeinit(state->nodes.data[node]);
    NfaNodeInit(state->nodes.data[node]);
}

static int ThompsonConstruct(regex_parser_state_t *state)
{
}
