#include "nfa.h"
#include <ctype.h>
#include <string.h>
#include <threads.h>

typedef vec_t(size_t) vec_size_t;

typedef struct {
  size_t start;
  size_t end;
} nfa_node_pair_t;
typedef enum {
  TOK_LITERAL,
  TOK_EOS,
  TOK_PLUS,
  TOK_STAR,
  TOK_QUESTION,
  TOK_LEFT_BRACE,
  TOK_RIGHT_BRACE,
  TOK_LEFT_BRACKET,
  TOK_RIGHT_BRACKET,
  TOK_DASH,
  TOK_DOT,
  TOK_PIPE,
  TOK_CARAT,
  TOK_DOLLAR,
  TOK_LEFT_PAREN,
  TOK_RIGHT_PAREN,
} token_t;

typedef struct {
  char key;
  token_t token;
  UT_hash_handle hh;
} token_map_entry_t;

typedef struct {
  vec_nfa_node_t nodes;
  vec_size_t discardedNodes;
  char *inputBuf;
  char *input;
  char lexeme;
  macro_t *macros;
  bool inQuote;
  vec_str_t inputStack;
  token_t currentTok;
} regex_parser_state_t;

static thread_local token_map_entry_t *sTokenMap = NULL;

static void CleanupTokenMap(void);
static size_t AllocateNfaNode(regex_parser_state_t *state);
static void DiscardNfaNode(regex_parser_state_t *state, size_t node);
static char *ExpandMacro(regex_parser_state_t *state);
static char HexToBinary(char c);
static char OctalToBinary(char c);
static char ProcessEscapeCodes(regex_parser_state_t *state);
static token_t Advance(regex_parser_state_t *state);
static int ThompsonConstruct(regex_parser_state_t *state);
static void ConcatenateExpressions(regex_parser_state_t *state, size_t *pStart,
                                   size_t *pEnd);
static void ParseExpression(regex_parser_state_t *state, size_t *pStart,
                            size_t *pEnd);
static void ParseTerm(regex_parser_state_t *state, size_t *pStart,
                      size_t *pEnd);
static void ParseFactor(regex_parser_state_t *state, size_t *pStart,
                        size_t *pEnd);
static bool CanBeExpressionStart(token_t token);
static void DoDash(regex_parser_state_t *state, bitset_t *bitset);

void NfaNodeInit(nfa_node_t *node) {
  node->acceptString = NULL;
  node->anchor = ANCHOR_NONE;
  node->edge = EDGE_EMPTY;
  node->characterClass = bitset_create();
  node->next[0] = NULL;
  node->next[1] = NULL;
}

nfa_t *ConstructNfa(const char *regex, size_t len, macro_t *macros) {
  regex_parser_state_t parserState = {.input = NULL,
                                      .inputBuf = GC_malloc(len),
                                      .lexeme = '\0',
                                      .macros = NULL};
  strncpy(parserState.inputBuf, regex, len);
  parserState.input = parserState.inputBuf;
  vec_init(&parserState.nodes);
  vec_init(&parserState.discardedNodes);
  parserState.inQuote = false;
  vec_init(&parserState.inputStack);
  nfa_t *nfa = GC_malloc(sizeof(nfa_t));
  nfa->start = ThompsonConstruct(&parserState);
  nfa->nodes = parserState.nodes;
  vec_deinit(&parserState.discardedNodes);
  return nfa;
}

static void CleanupTokenMap() { HASH_CLEAR(hh, sTokenMap); }

static size_t AllocateNfaNode(regex_parser_state_t *state) {
  if (state->discardedNodes.length > 0) {
    return vec_pop(&state->discardedNodes);
  }

  nfa_node_t *node = GC_malloc(sizeof(nfa_node_t));
  NfaNodeInit(node);
  node->index = state->nodes.length;
  vec_push(&state->nodes, node);
  return node->index;
}

static void DiscardNfaNode(regex_parser_state_t *state, size_t node) {
  vec_push(&state->discardedNodes, node);
  NfaNodeInit(state->nodes.data[node]);
}

static char *ExpandMacro(regex_parser_state_t *state) {
  char *p = strchr(state->input, '}');
  if (!p) {
    fprintf(stderr, "in '%s': missing '}'", state->inputBuf);
    exit(1);
  }

  *p++ = '\0';
  macro_t *macro;
  HASH_FIND_STR(state->macros, state->input, macro);
  if (!macro) {
    fprintf(stderr, "in '%s': unknown macro '%s'", state->inputBuf,
            state->input);
    exit(1);
  }
  state->input = p;
  return macro->definition;
}

static char HexToBinary(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }

  return toupper(c) - 'A' + 10;
}

static char OctalToBinary(char c) { return c - '0'; }

static char ProcessEscapeCodes(regex_parser_state_t *state) {
  if (state->input[0] != '\\') {
    char c = state->input[0];
    ++state->input;
    return c;
  }

  ++state->input;
  char c;
  switch (tolower(state->input[0])) {
  case '\0':
    return '\\';
  case 'b':
    c = '\b';
    break;
  case 'f':
    c = '\f';
    break;
  case 'n':
    c = '\n';
    break;
  case 'r':
    c = '\r';
    break;
  case 't':
    c = '\t';
    break;
  case 'e':
    c = '\x1b';
    break;
  case '^':
    ++state->input;
    c = toupper(state->input[0]) - '@';
    break;
  case 'x':
    c = 0;
    ++state->input;
#define IS_HEX_DIGIT(c)                                                        \
  (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f') ||                 \
   ((c) >= 'A' && (c) <= 'F'))

    if (IS_HEX_DIGIT(state->input[0])) {
      c = HexToBinary(state->input[0]);
      ++state->input;
    }
    if (IS_HEX_DIGIT(state->input[0])) {
      c <<= 4;
      c |= HexToBinary(state->input[0]);
      ++state->input;
    }
    --state->input;
    break;

#undef IS_HEX_DIGIT

  default:
#define IS_OCT_DIGIT(c) ((c) >= '0' && (c) <= '7')

    if (!IS_OCT_DIGIT(state->input[0])) {
      return state->input[0];
    } else {
      ++state->input;
      c = OctalToBinary(state->input[0]);
      ++state->input;
      if (IS_OCT_DIGIT(state->input[0])) {
        c <<= 3;
        c |= OctalToBinary(state->input[0]);
        ++state->input;
      }
      if (IS_OCT_DIGIT(state->input[0])) {
        c <<= 3;
        c |= OctalToBinary(state->input[0]);
        ++state->input;
      }
      --state->input;
    }
    break;

#undef IS_OCT_DIGIT
  }
  ++state->input;
  return c;
}

static token_t Advance(regex_parser_state_t *state) {
  if (!sTokenMap) {
#define ADD_TOKEN_MAP_ENTRY(k, t)                                              \
  do {                                                                         \
    token_map_entry_t *entry = GC_malloc(sizeof(token_map_entry_t));           \
    entry->key = k;                                                            \
    entry->token = t;                                                          \
    HASH_ADD(hh, sTokenMap, key, sizeof(char), entry);                         \
  } while (0)
    ADD_TOKEN_MAP_ENTRY('{', TOK_LEFT_BRACE);
    ADD_TOKEN_MAP_ENTRY('}', TOK_RIGHT_BRACE);
    ADD_TOKEN_MAP_ENTRY('(', TOK_LEFT_PAREN);
    ADD_TOKEN_MAP_ENTRY(')', TOK_RIGHT_PAREN);
    ADD_TOKEN_MAP_ENTRY('[', TOK_LEFT_BRACKET);
    ADD_TOKEN_MAP_ENTRY(']', TOK_RIGHT_BRACKET);
    ADD_TOKEN_MAP_ENTRY('|', TOK_PIPE);
    ADD_TOKEN_MAP_ENTRY('.', TOK_DOT);
    ADD_TOKEN_MAP_ENTRY('$', TOK_DOLLAR);
    ADD_TOKEN_MAP_ENTRY('^', TOK_CARAT);
    ADD_TOKEN_MAP_ENTRY('*', TOK_STAR);
    ADD_TOKEN_MAP_ENTRY('+', TOK_PLUS);
    ADD_TOKEN_MAP_ENTRY('?', TOK_QUESTION);
    ADD_TOKEN_MAP_ENTRY('-', TOK_DASH);
    atexit(CleanupTokenMap);
#undef ADD_TOKEN_MAP_ENTRY
  }

  while (state->input[0] == '\0') {
    if (state->inputStack.length > 0) {
      state->input = vec_pop(&state->inputStack);
      continue;
    }

    state->currentTok = TOK_EOS;
    state->lexeme = '\0';
    return state->currentTok;
  }

  if (!state->inQuote) {
    while (state->input[0] == '{') {
      vec_push(&state->inputStack, state->input);
      state->input = ExpandMacro(state);
    }
  }
  if (state->input[0] == '"') {
    state->inQuote = !state->inQuote;
    ++state->input;
    if (state->input[0] == '\0') {
      state->currentTok = TOK_EOS;
      state->lexeme = '\0';
      return state->currentTok;
    }
  }
  bool sawEsc = state->input[0] == '\\';
  if (!state->inQuote) {
    if (isspace(state->input[0])) {
      state->currentTok = TOK_EOS;
      state->lexeme = '\0';
      return state->currentTok;
    }
    state->lexeme = ProcessEscapeCodes(state);
  } else {
    if (sawEsc && state->input[1] == '"') {
      state->input += 2;
      state->lexeme = '"';
    } else {
      state->lexeme = state->input[0];
      ++state->input;
    }
  }

  if (state->inQuote || sawEsc) {
    state->currentTok = TOK_LITERAL;
  } else {
    token_map_entry_t *entry;
    HASH_FIND(hh, sTokenMap, &state->lexeme, sizeof(char), entry);
    if (entry == NULL) {
      state->currentTok = TOK_LITERAL;
    } else {
      state->currentTok = entry->token;
    }
  }
  return state->currentTok;
}

static int ThompsonConstruct(regex_parser_state_t *state) {
  size_t start = 0;
  size_t end = 0;
  anchor_t anchor = ANCHOR_NONE;
  if (state->currentTok == TOK_CARAT) {
    start = AllocateNfaNode(state);
    anchor |= ANCHOR_LINE_START;
    Advance(state);
    size_t snext;
    ParseExpression(state, &snext, &end);
    state->nodes.data[start]->next[0] = state->nodes.data[snext];
  } else {
    ParseExpression(state, &start, &end);
  }

  if (state->currentTok == TOK_DOLLAR) {
    Advance(state);
    size_t endNext = AllocateNfaNode(state);
    state->nodes.data[end]->next[0] = state->nodes.data[endNext];
    state->nodes.data[end]->edge = EDGE_CHARACTER_CLASS;
    bitset_set(state->nodes.data[end]->characterClass, '\n');
    bitset_set(state->nodes.data[end]->characterClass, '\r');
    end = endNext;
    anchor |= ANCHOR_LINE_END;
  }

  while (isspace(state->input[0])) {
    ++state->input;
  }
  state->nodes.data[end]->acceptString = strdup(state->input);
  state->nodes.data[end]->anchor = anchor;
  Advance(state);
  return start;
}

static void ConcatenateExpressions(regex_parser_state_t *state, size_t *pStart,
                                   size_t *pEnd) {
  nfa_node_pair_t expr2;
  if (CanBeExpressionStart(state->currentTok)) {
    ParseFactor(state, pStart, pEnd);
  }

  while (CanBeExpressionStart(state->currentTok)) {
    ParseFactor(state, &expr2.start, &expr2.end);
    memcpy(state->nodes.data[*pEnd], state->nodes.data[expr2.start],
           sizeof(nfa_node_t));
    DiscardNfaNode(state, expr2.start);
    *pEnd = expr2.end;
  }
}

static void ParseExpression(regex_parser_state_t *state, size_t *pStart,
                            size_t *pEnd) {
  nfa_node_pair_t expr2;
  ConcatenateExpressions(state, pStart, pEnd);
  while (state->currentTok == TOK_PIPE) {
    Advance(state);
    ConcatenateExpressions(state, &expr2.start, &expr2.end);
    size_t p = AllocateNfaNode(state);
    state->nodes.data[p]->next[1] = state->nodes.data[expr2.start];
    state->nodes.data[p]->next[0] = state->nodes.data[*pStart];
    *pStart = p;

    p = AllocateNfaNode(state);
    state->nodes.data[*pEnd]->next[0] = state->nodes.data[p];
    state->nodes.data[expr2.end]->next[0] = state->nodes.data[p];
    *pEnd = p;
  }
}

static void ParseTerm(regex_parser_state_t *state, size_t *pStart,
                      size_t *pEnd) {
  if (state->currentTok == TOK_LEFT_PAREN) {
    Advance(state);
    ParseExpression(state, pStart, pEnd);
    if (state->currentTok == TOK_RIGHT_PAREN) {
      Advance(state);
    } else {
      fprintf(stderr, "missing close parenthesis in regex\n");
      exit(1);
    }
  } else {
    size_t start = AllocateNfaNode(state);
    *pStart = start;
    *pEnd = AllocateNfaNode(state);
    state->nodes.data[start]->next[0] = state->nodes.data[*pEnd];
    if (state->currentTok != TOK_DOT && state->currentTok != TOK_LEFT_BRACKET) {
      state->nodes.data[start]->edge = state->lexeme;
      Advance(state);
    } else {
      state->nodes.data[start]->edge = EDGE_CHARACTER_CLASS;
      if (state->currentTok == TOK_DOT) {
        bitset_set(state->nodes.data[start]->characterClass, '\n');
        bitset_set(state->nodes.data[start]->characterClass, '\r');
        state->nodes.data[start]->inverted = true;
      } else {
        Advance(state);
        if (state->currentTok == TOK_CARAT) {
          Advance(state);
          bitset_set(state->nodes.data[start]->characterClass, '\n');
          bitset_set(state->nodes.data[start]->characterClass, '\r');
          state->nodes.data[start]->inverted = true;
        }
        if (state->currentTok == TOK_RIGHT_BRACKET) {
          for (char c = 0; c <= ' '; ++c) {
            bitset_set(state->nodes.data[start]->characterClass, c);
          }
        } else {
          DoDash(state, state->nodes.data[start]->characterClass);
        }
      }
      Advance(state);
    }
  }
}

static void ParseFactor(regex_parser_state_t *state, size_t *pStart,
                        size_t *pEnd) {
  ParseTerm(state, pStart, pEnd);
  if (state->currentTok == TOK_STAR || state->currentTok == TOK_PLUS ||
      state->currentTok == TOK_QUESTION) {
    size_t start = AllocateNfaNode(state);
    size_t end = AllocateNfaNode(state);
    state->nodes.data[start]->next[0] = state->nodes.data[*pStart];
    state->nodes.data[*pEnd]->next[0] = state->nodes.data[end];

    if (state->currentTok == TOK_STAR || state->currentTok == TOK_QUESTION) {
      state->nodes.data[start]->next[1] = state->nodes.data[end];
    }

    if (state->currentTok == TOK_STAR || state->currentTok == TOK_PLUS) {
      state->nodes.data[*pEnd]->next[1] = state->nodes.data[*pStart];
    }

    *pStart = start;
    *pEnd = end;
    Advance(state);
  }
}

static bool CanBeExpressionStart(token_t token) {
  switch (token) {
  case TOK_RIGHT_PAREN:
  case TOK_DOLLAR:
  case TOK_PIPE:
  case TOK_EOS:
    return false;
  case TOK_STAR:
  case TOK_PLUS:
  case TOK_QUESTION:
    fprintf(stderr, "found closure symbol in invalid position\n");
    exit(1);
  case TOK_RIGHT_BRACKET:
    fprintf(stderr, "found unmatched closing bracket\n");
    exit(1);
  case TOK_CARAT:
    fprintf(stderr, "found invalid ^ symbol not at start of regex\n");
    exit(1);
  default:
    return true;
  }
}

static void DoDash(regex_parser_state_t *state, bitset_t *bitset) {
  char first;
  while (state->currentTok != TOK_EOS &&
         state->currentTok != TOK_RIGHT_BRACKET) {
    if (state->currentTok != TOK_DASH) {
      first = state->lexeme;
      bitset_set(bitset, first);
    } else {
      Advance(state);
      for (; first <= state->lexeme; ++first) {
        bitset_set(bitset, first);
      }
    }
  }
}
