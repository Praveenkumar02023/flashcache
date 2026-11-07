#include <cstdint>
#include <stddef.h>    

struct HNode{

    HNode * next;
    uint64_t hcode = 0;

};

struct HTab{

    HNode ** tab = NULL;
    size_t mask = 0;
    size_t size = 0;

};


