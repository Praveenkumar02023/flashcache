
#include "avl.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

// Each test node stores an integer key and embeds an AVLNode.
struct TestNode {
    int key;
    AVLNode avl;
};

static_assert(std::is_standard_layout<TestNode>::value,
              "TestNode must be standard-layout");

// Recover the containing TestNode from its embedded AVLNode.
static TestNode *container_of(AVLNode *node) {
    return reinterpret_cast<TestNode *>(
        reinterpret_cast<char *>(node) - offsetof(TestNode, avl)
    );
}

// Compare two AVL nodes by their keys.
static bool less(AVLNode *lhs, AVLNode *rhs) {
    return container_of(lhs)->key < container_of(rhs)->key;
}

// Find a key in the AVL tree.
static AVLNode *avl_search(AVLNode *root, int key) {
    while (root) {
        int current_key = container_of(root)->key;

        if (key < current_key) {
            root = root->left;
        } else if (key > current_key) {
            root = root->right;
        } else {
            return root;
        }
    }

    return NULL;
}

// Insert a node into the AVL tree.
static void avl_insert(AVLNode **root, AVLNode *new_node) {
    AVLNode *parent = NULL;
    AVLNode **from = root;

    while (*from) {
        parent = *from;

        if (less(new_node, parent)) {
            from = &parent->left;
        } else {
            from = &parent->right;
        }
    }

    *from = new_node;
    new_node->parent = parent;

    // Restore AVL balance and update the root.
    *root = avl_fix(new_node);
}

// Remove a node with the given key.
static bool avl_remove(AVLNode **root, int key) {
    AVLNode *node = avl_search(*root, key);

    if (!node) {
        return false;
    }

    *root = avl_del(node);
    return true;
}

// Verify the entire AVL tree recursively.
static void validate(
    AVLNode *node,
    AVLNode *expected_parent,
    long long min_key,
    long long max_key
) {
    if (!node) {
        return;
    }

    TestNode *current = container_of(node);

    // 1. Parent pointer must be correct.
    assert(node->parent == expected_parent);

    // 2. BST ordering must be correct.
    assert(current->key > min_key);
    assert(current->key < max_key);

    // 3. Validate the left subtree.
    validate(
        node->left,
        node,
        min_key,
        current->key
    );

    // 4. Validate the right subtree.
    validate(
        node->right,
        node,
        current->key,
        max_key
    );

    // 5. Verify stored height.
    uint32_t expected_height = 1 + std::max(
        avl_height(node->left),
        avl_height(node->right)
    );

    assert(node->height == expected_height);

    // 6. Verify stored subtree count.
    uint32_t expected_count = 1 +
        avl_cnt(node->left) +
        avl_cnt(node->right);

    assert(node->cnt == expected_count);

    // 7. Verify AVL balance factor.
    int balance =
        (int)avl_height(node->left) -
        (int)avl_height(node->right);

    assert(balance >= -1 && balance <= 1);
}

// Validate the root and all nodes.
static void validate_tree(AVLNode *root) {
    if (root) {
        assert(root->parent == NULL);
    }

    validate(root, NULL, LLONG_MIN, LLONG_MAX);
}

// Collect keys using in-order traversal.
static void inorder(
    AVLNode *node,
    std::vector<int> &result
) {
    if (!node) {
        return;
    }

    inorder(node->left, result);
    result.push_back(container_of(node)->key);
    inorder(node->right, result);
}

// Compare AVL contents against std::set.
static void verify(
    AVLNode *root,
    const std::set<int> &reference
) {
    // Verify all structural invariants.
    validate_tree(root);

    // Verify total number of nodes.
    assert(avl_cnt(root) == reference.size());

    // Collect sorted keys from the AVL tree.
    std::vector<int> actual;
    inorder(root, actual);

    // Convert reference set into a vector.
    std::vector<int> expected(
        reference.begin(),
        reference.end()
    );

    // Both structures must contain identical sorted keys.
    assert(actual == expected);
}

// Run deterministic and randomized tests.
int main() {
    AVLNode *root = NULL;

    // std::map keeps TestNode addresses stable.
    std::map<int, TestNode> nodes;

    // Reference implementation.
    std::set<int> reference;

    // Insert one key into both structures.
    auto insert = [&](int key) {
        if (reference.count(key)) {
            return;
        }

        TestNode &entry = nodes[key];
        TestNode *node = &entry;

        node->key = key;
        avl_init(&node->avl);

        avl_insert(&root, &node->avl);
        reference.insert(key);

        verify(root, reference);
    };

    // Delete one key from both structures.
    auto remove = [&](int key) {
        bool removed = avl_remove(&root, key);
        bool expected = reference.erase(key) > 0;

        assert(removed == expected);

        if (expected) {
            nodes.erase(key);
        }

        verify(root, reference);
    };

    // -------------------------------------------------
    // Test 1: Ascending insertion.
    // -------------------------------------------------
    std::cout << "Test 1: Ascending insertion\n";

    for (int i = 1; i <= 1000; i++) {
        insert(i);
    }

    // -------------------------------------------------
    // Test 2: Ascending deletion.
    // -------------------------------------------------
    std::cout << "Test 2: Ascending deletion\n";

    for (int i = 1; i <= 1000; i++) {
        remove(i);
    }

    assert(root == NULL);

    // -------------------------------------------------
    // Test 3: Descending insertion.
    // -------------------------------------------------
    std::cout << "Test 3: Descending insertion\n";

    for (int i = 1000; i >= 1; i--) {
        insert(i);
    }

    // -------------------------------------------------
    // Test 4: Descending deletion.
    // -------------------------------------------------
    std::cout << "Test 4: Descending deletion\n";

    for (int i = 1000; i >= 1; i--) {
        remove(i);
    }

    assert(root == NULL);

    // -------------------------------------------------
    // Test 5: Randomized insertion and deletion.
    // -------------------------------------------------
    std::cout << "Test 5: Randomized operations\n";

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> key_dist(1, 5000);
    std::uniform_int_distribution<int> op_dist(0, 1);

    for (int i = 0; i < 20000; i++) {
        int key = key_dist(rng);

        if (op_dist(rng) == 0) {
            insert(key);
        } else {
            remove(key);
        }
    }

    // -------------------------------------------------
    // Test 6: Delete all remaining nodes.
    // -------------------------------------------------
    std::cout << "Test 6: Final cleanup\n";

    while (!reference.empty()) {
        remove(*reference.begin());
    }

    assert(root == NULL);
    assert(nodes.empty());

    std::cout << "\nAll AVL tests passed!\n";

    return 0;
}

static int32_t cmp(AVLNode *node, void *key) {
    int current_key = container_of(node)->key;
    int target_key = *static_cast<int *>(key);

    if (current_key < target_key) return -1;
    if (current_key > target_key) return 1;

    return 0;
}

static void search_and_insert(
    AVLNode **root,
    AVLNode *new_node,
    bool (*less)(AVLNode *, AVLNode *)
) {
    // Find the insertion position.
    AVLNode *parent = NULL;
    AVLNode **from = root;

    for (AVLNode *node = *root; node; ) {
        from = less(new_node, node)
            ? &node->left
            : &node->right;

        parent = node;
        node = *from;
    }

    // Link the new node.
    *from = new_node;
    new_node->parent = parent;

    // Restore AVL balance.
    *root = avl_fix(new_node);
}

static AVLNode *search_and_delete(
    AVLNode **root,
    int32_t (*cmp)(AVLNode *, void *),
    void *key
) {
    for (AVLNode *node = *root; node; ) {
        int32_t r = cmp(node, key);

        if (r < 0) {
            node = node->right;
        } else if (r > 0) {
            node = node->left;
        } else {
            *root = avl_del(node);
            return node;
        }
    }

    return NULL;
}
