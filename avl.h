#pragma once

#include <stddef.h>
#include <stdint.h>

struct AVLNode{

    AVLNode* parent;
    AVLNode* left;
    AVLNode* right;

    uint32_t height;
    uint32_t cnt;
};

inline void avl_init(AVLNode * node){

    node->parent = node->left = node->right = NULL;
    node->height = 1;
    node->cnt = 1;

}

inline uint32_t avl_height(AVLNode *node) {
    return node ? node->height : 0;
}

inline uint32_t avl_cnt(AVLNode *node) {
    return node ? node->cnt : 0;
}

AVLNode *avl_fix(AVLNode * node);
AVLNode * avl_del(AVLNode * node);
