#include <assert.h>
#include "avl.h"

static uint32_t max(uint32_t lhs, uint32_t rhs){

    return lhs > rhs ? lhs : rhs;

}

// Maintain the height and cnt fields.
static void avl_update(AVLNode *node) {
    node->height = 1 + max(
        avl_height(node->left),
        avl_height(node->right)
    );

    node->cnt = 1
        + avl_cnt(node->left)
        + avl_cnt(node->right);
}

static AVLNode * left_rot(AVLNode* node){

    AVLNode* parent = node->parent;

    AVLNode* new_node = node->right;

    AVLNode* left_of_new_node = new_node->left;

    new_node->left = node;

    node->right = left_of_new_node;

    if(left_of_new_node) left_of_new_node->parent = node;

    node->parent = new_node;
    new_node->parent = parent;          

    avl_update(node);
    avl_update(new_node);

    return new_node;
}

static AVLNode * right_rot(AVLNode* node){

    AVLNode* parent = node->parent;

    AVLNode* new_node = node->left;

    AVLNode* right_of_new_node = new_node->right;

    new_node->right = node;
    node->left = right_of_new_node;

    if(right_of_new_node) right_of_new_node->parent = node;

    node->parent = new_node;
    new_node->parent = parent;

    avl_update(node);
    avl_update(new_node);

    return new_node;
}

static AVLNode* avl_fix_left(AVLNode * node){

    if(avl_height(node->left->right) > avl_height(node->left->left)){

        node->left = left_rot(node->left);
        node->left->parent = node;

    }

    return right_rot(node);
}

static AVLNode* avl_fix_right(AVLNode * node){

    if(avl_height(node->right->right) < avl_height(node->right->left)){

        node->right = right_rot(node->right);
        node->right->parent = node;

    }

    return left_rot(node);
}

AVLNode *avl_fix(AVLNode *node) {
    while (node) {
        avl_update(node);

        // Calculate balance factor.
        int32_t bf =
            (int32_t)avl_height(node->left) -
            (int32_t)avl_height(node->right);

        AVLNode *parent = node->parent;

        // Remember where the current subtree is attached.
        AVLNode **from = NULL;

        if (parent) {
            from = (parent->left == node)
                ? &parent->left
                : &parent->right;
        }

        // Perform rotation if necessary.
        if (bf == +2) {
            node = avl_fix_left(node);
        } else if (bf == -2) {
            node = avl_fix_right(node);
        }

        // Reconnect the subtree.
        node->parent = parent;

        if (from) {
            *from = node;
        } else {
            // No parent means this is the overall root.
            return node;
        }

        // Move upwards.
        node = parent;
    }

    return NULL;
}

