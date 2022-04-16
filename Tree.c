#include "Tree.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HashMap.h"
#include "err.h"
#include "path_utils.h"
#include "readerswriters.h"

#define ROOT "/"

struct Tree {
    HashMap* hmap;
    Monitor* monitor;
};

Tree* tree_new() {
    Tree* tree = malloc(sizeof(Tree));
    if (!tree) return NULL;

    tree->hmap = hmap_new();
    if (!tree->hmap) {
        free(tree);
        return NULL;
    }

    tree->monitor = init_monitor();
    if (!tree->monitor) {
        hmap_free(tree->hmap);
        free(tree);
        return NULL;
    }

    return tree;
}

void tree_free(Tree* tree) {
    if (hmap_size(tree->hmap)) {
        const char* key;
        void* value;
        HashMapIterator it = hmap_iterator(tree->hmap);
        while (hmap_next(tree->hmap, &it, &key, &value))
            tree_free((Tree*)value);
    }

    hmap_free(tree->hmap);
    free_monitor(tree->monitor);
    free(tree);
}

static Monitor** init_mon_path(const char* path, size_t* path_len) {
    *path_len = 1;
    while ((path = split_path(path, NULL))) ++*path_len;

    return malloc(*path_len * sizeof(Tree*));
}

static void free_mon_path(Monitor** mon_path, size_t path_len,
                          bool last_write) {
    if (path_len) {
        if (last_write) end_write(mon_path[--path_len]);
        for (; path_len-- > 0;) end_read(mon_path[path_len]);
    }

    free(mon_path);
}

static Tree* node_find(Tree* tree, const char* path, Monitor** mon_path,
                       bool write) {
    size_t i = 0;

    char component[MAX_FOLDER_NAME_LENGTH + 1];

    Tree* current = tree;
    mon_path[i++] = current->monitor;
    while ((path = split_path(path, component))) {
        begin_read(current->monitor);
        current = (Tree*)hmap_get(current->hmap, component);
        if (!current) {
            free_mon_path(mon_path, i, false);
            return NULL;
        }
        mon_path[i++] = current->monitor;
    }

    if (write)
        begin_write(current->monitor);
    else
        begin_read(current->monitor);

    return current;
}

static Tree* node_find_safe(Tree* tree, const char* path) {
    char component[MAX_FOLDER_NAME_LENGTH + 1];

    while (tree && (path = split_path(path, component)))
        tree = (Tree*)hmap_get(tree->hmap, component);

    return tree;
}

char* tree_list(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return NULL;

    size_t path_len;
    Monitor** mon_path = init_mon_path(path, &path_len);
    if (!mon_path) return NULL;

    Tree* node = node_find(tree, path, mon_path, false);
    if (!node) return NULL;  // path doesn't exist

    char* list = make_map_contents_string(node->hmap);
    free_mon_path(mon_path, path_len, false);
    return list;
}

int tree_create(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;  // path is not valid

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char* parent_path = make_path_to_parent(path, component);
    if (!parent_path) return EEXIST;  // root, so already exists

    size_t path_len;
    Monitor** mon_path = init_mon_path(parent_path, &path_len);
    if (!mon_path) {
        free(parent_path);
        return ENOMEM;
    }

    Tree* parent = node_find(tree, parent_path, mon_path, true);
    free(parent_path);
    if (!parent) return ENOENT;  // parent doesn't exist

    // path already exists
    if (hmap_get(parent->hmap, component)) {
        free_mon_path(mon_path, path_len, true);
        return EEXIST;
    }

    Tree* new_node = tree_new();
    if (new_node) hmap_insert(parent->hmap, component, (void*)new_node);
    free_mon_path(mon_path, path_len, true);
    if (!new_node) return ENOMEM;

    return 0;
}

int tree_remove(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;  // path is not valid

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char* parent_path = make_path_to_parent(path, component);
    if (!parent_path) return EBUSY;  // path is root

    size_t path_len;
    Monitor** mon_path = init_mon_path(parent_path, &path_len);
    if (!mon_path) {
        free(parent_path);
        return ENOMEM;
    }

    Tree* parent = node_find(tree, parent_path, mon_path, true);
    free(parent_path);
    if (!parent) return ENOENT;  // path doesn't exist

    Tree* node = hmap_get(parent->hmap, component);
    if (!node) {  // path doesn't exist
        free_mon_path(mon_path, path_len, true);
        return ENOENT;
    }

    if (hmap_size(node->hmap)) {  // path is not empty
        free_mon_path(mon_path, path_len, true);
        return ENOTEMPTY;
    }

    hmap_remove(parent->hmap, component);
    tree_free(node);
    free_mon_path(mon_path, path_len, true);

    return 0;
}

static int move_check_cases(Tree* tree, const char* source, const char* target,
                            size_t idx) {
    /** illegal cases **/
    // 0. source or target are root
    if (!strcmp(source, ROOT)) return EBUSY;
    if (!strcmp(target, ROOT)) return EEXIST;

    // is source/target equal to common path
    bool source_eq = !strcmp(source + idx, ROOT);
    bool target_eq = !strcmp(target + idx, ROOT);

    // 1. source and target are equal
    // if path exists then target exists so EEXIST
    // if path does't exist then source doesn't exist so ENOENT
    if (source_eq && target_eq) {
        size_t path_len;
        Monitor** mon_path = init_mon_path(source, &path_len);
        if (!mon_path) return ENOMEM;

        Tree* node = node_find(tree, source, mon_path, false);
        if (!node)
            return ENOENT;
        else {
            free_mon_path(mon_path, path_len, false);
            return EEXIST;
        }
    }

    // 2. source is begining of target eg. s: /a/b/ -> t: /a/b/c/d/
    /// if source exists then I remove it,
    /// so target's parent doesn't exist: ENOENT
    /// if source doesn't exist then also ENOENT
    if (source_eq) return ENOENT;

    // 3. target is begining of source eg. s: /a/b/c/d/ -> t: /a/b/
    /// if target exists then EEXIST
    /// if target doesn't exist then ENOENT because source doesn't exist
    if (target_eq) {
        size_t path_len;
        Monitor** mon_path = init_mon_path(target, &path_len);
        if (!mon_path) return ENOMEM;

        Tree* node = node_find(tree, target, mon_path, false);
        if (!node) return ENOENT;

        free_mon_path(mon_path, path_len, false);
        return EEXIST;
    }

    return 0;
}

int tree_move(Tree* tree, const char* source, const char* target) {
    if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;

    char common_path[MAX_PATH_LENGTH + 1];
    size_t idx = find_common_path(source, target, common_path);

    int c = move_check_cases(tree, source, target, idx);
    if (c) return c;

    // Since I checked other cases, I know the paths will seperate at some
    // point, so I can block the latest common ancestor of the paths

    size_t path_len;
    Monitor** mon_path = init_mon_path(common_path, &path_len);
    if (!mon_path) return ENOMEM;

    Tree* lca = node_find(tree, common_path, mon_path, true);
    if (!lca) return ENOENT;  // paths don't exist

    // Latest common ancestor of the two paths is blocked, so I can safely look
    // for the source and target nodes

    const char* source_rest = source + idx;

    char s_last[MAX_FOLDER_NAME_LENGTH + 1];
    char* sp_path = make_path_to_parent(source_rest, s_last);

    Tree* s_parent = node_find_safe(lca, sp_path);
    free(sp_path);

    Tree* s_node;
    if (!s_parent || !(s_node = hmap_get(s_parent->hmap, s_last))) {
        // source doesn't exist
        free_mon_path(mon_path, path_len, true);
        return ENOENT;
    }

    const char* target_rest = target + idx;

    char t_last[MAX_FOLDER_NAME_LENGTH + 1];
    char* tp_path = make_path_to_parent(target_rest, t_last);

    Tree* t_parent = node_find_safe(lca, tp_path);
    free(tp_path);

    if (!t_parent || hmap_get(t_parent->hmap, t_last)) {
        free_mon_path(mon_path, path_len, true);
        if (!t_parent)  // target's parent doesn't exist
            return ENOENT;
        else  // target exists
            return EEXIST;
    }

    hmap_insert(t_parent->hmap, t_last, (void*)s_node);
    hmap_remove(s_parent->hmap, s_last);
    free_mon_path(mon_path, path_len, true);

    return 0;
}