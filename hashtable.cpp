#include <assert.h>
#include "hashtable.h"
#include <cstddef>
#include <stdlib.h>


static void h_init(HTab * htab, size_t n){
    
    assert(n > 0 && ((n-1) & n) == 0);

    htab->tab = (HNode **)calloc(n,sizeof(HNode *));
    htab->mask = n-1;
    htab->size = 0;

}

static void h_insert(HTab * htab , HNode* node){

    size_t pos = node->hcode & htab->mask;

    HNode* next = htab->tab[pos];

    node->next = next;

    htab->tab[pos] = node;
    htab->size++;

}

static HNode** h_lookup(HTab* htab , HNode * key , bool (*eq)(HNode* , HNode*)){

    if(!htab->tab)return NULL;

    size_t pos = htab->mask & key->hcode;

    HNode ** from = &htab->tab[pos];

    for(HNode * cur ; (cur = *from) != NULL ; from = &cur->next ){

        if(cur->hcode == key->hcode && eq(cur,key)){

            return from;

        }

    }

    return NULL;
}   

static HNode * h_detach(HTab* htab , HNode** from){

    HNode * node = *from;

    *from = node->next;

    htab->size--;

    return node;
}


static void hm_trigger_rehashing(HMap *hmap){

    assert(hmap->older.tab == NULL);

    hmap->older = hmap->newer;

    //initialize new *2 size hashtable
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);

    hmap->migration_pos =0 ;
}


const size_t k_rehashing_limit = 128;

static void hm_help_rehashing(HMap * hmap){

    size_t cur_limit = 0;

    while(cur_limit < k_rehashing_limit && hmap->older.size > 0){

        HNode ** from = &hmap->older.tab[hmap->migration_pos];

        if(!*from){ //if no entry in current bucket move forward

            hmap->migration_pos++;
            continue;
        }

        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        cur_limit++;
    }


    //if migration is complete free up space occupied by older hashtable
    if(hmap->older.size == 0 && hmap->older.tab){

        free(hmap->older.tab);
        hmap->older = HTab{};

    }
}

const size_t k_max_load_factor = 8;

void hm_insert(HMap *hmap, HNode *node) {

    if (!hmap->newer.tab) {
        h_init(&hmap->newer, 4);
    }

    // Always insert into newer
    h_insert(&hmap->newer, node);

    // Check whether resizing is required
    if (!hmap->older.tab) {

        size_t threshold =
            (hmap->newer.mask + 1) * k_max_load_factor;

        if (hmap->newer.size >= threshold) {
            hm_trigger_rehashing(hmap);
        }
    }

    // Perform some migration work
    hm_help_rehashing(hmap);
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    
    hm_help_rehashing(hmap);

    HNode **from = h_lookup(&hmap->newer, key, eq);

    if (!from) {
        from = h_lookup(&hmap->older, key, eq);
    }

    return from ? *from : NULL;
}


HNode *hm_delete(
    HMap *hmap,
    HNode *key,
    bool (*eq)(HNode *, HNode *)
) {
    hm_help_rehashing(hmap);

    if (HNode **from = h_lookup(&hmap->newer, key, eq)) {
        return h_detach(&hmap->newer, from);
    }

    if (HNode **from = h_lookup(&hmap->older, key, eq)) {
        return h_detach(&hmap->older, from);
    }

    return NULL;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);

    *hmap = HMap{};
}

size_t hm_size(HMap *hmap) {
    return hmap->newer.size + hmap->older.size;
}