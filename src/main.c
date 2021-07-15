#include "nfa.h"

int main(int argc, char **argv)
{
    nfa_t *nfa = ConstructNfa("test set", 8, NULL);
    for (int i = 0; i < nfa->nodes.length; i++)
    {
        free(nfa->nodes.data[i]);
    }
    return 0;
}
