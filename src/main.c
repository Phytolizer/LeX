#include "nfa.h"

int main(int argc, char **argv)
{
    GC_INIT();
    nfa_t *nfa = ConstructNfa("test set", 8, NULL);
    return 0;
}
