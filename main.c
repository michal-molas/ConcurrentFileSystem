#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "HashMap.h"
#include "Tree.h"
#include "path_utils.h"

void example_move_test() {
    printf("EXAMPLE MOVE TEST\n");
    Tree* tree = tree_new();

    tree_create(tree, "/a/");
    tree_create(tree, "/b/");
    tree_create(tree, "/a/b/");
    tree_create(tree, "/a/b/c/");
    tree_create(tree, "/a/b/d/");
    tree_create(tree, "/b/a/");
    tree_create(tree, "/b/a/d/");

    tree_move(tree, "/a/b/", "/b/x/");

    tree_free(tree);
}

void random_path(char* path, size_t low, size_t high, size_t no_ltrs) {
    int n = rand() % (high - low + 1) + low;
    for (int i = 0; i < n; i++) {
        path[2 * i] = '/';
        path[2 * i + 1] = 'a' + rand() % no_ltrs;
    }
    path[2 * n] = '/';
    path[2 * n + 1] = '\0';
}

void* creator(void* tree) {
    for (int k = 0; k < 20; k++) {
        char path[32];
        random_path(path, 1, 3, 4);
        tree_create((Tree*)tree, path);
    }

    return NULL;
}

void* remover(void* tree) {
    for (int k = 0; k < 20; k++) {
        char path[32];
        random_path(path, 1, 3, 4);
        tree_remove((Tree*)tree, path);
    }

    return NULL;
}

void* lister(void* tree) {
    for (int k = 0; k < 20; k++) {
        char path[32];
        random_path(path, 0, 3, 4);
        char* list = tree_list((Tree*)tree, path);
        if (list) free(list);
    }

    return NULL;
}

void* mover(void* tree) {
    for (int k = 0; k < 20; k++) {
        char path_s[32];
        random_path(path_s, 1, 3, 4);
        char path_t[32];
        random_path(path_t, 1, 3, 4);
        tree_move((Tree*)tree, path_s, path_t);
    }

    return NULL;
}

#define N 50

void random_async_test() {
    printf("RANDOM ASYNC TEST\n");

    Tree* tree = tree_new();

    srand(time(NULL));

    pthread_t c[N], r[N], m[N], l[N];
    for (int i = 0; i < N; i++) {
        pthread_create(&c[i], NULL, creator, tree);
        pthread_create(&r[i], NULL, remover, tree);
        pthread_create(&m[i], NULL, mover, tree);
        pthread_create(&l[i], NULL, lister, tree);
    }

    for (size_t i = 0; i < N; i++) pthread_join(c[i], NULL);
    for (size_t i = 0; i < N; i++) pthread_join(r[i], NULL);
    for (size_t i = 0; i < N; i++) pthread_join(m[i], NULL);
    for (size_t i = 0; i < N; i++) pthread_join(l[i], NULL);

    tree_free(tree);
}

#define ITER 100

void* move_tester1(void* tree) {
    for (int i = 0; i < ITER; i++) {
        tree_move((Tree*)tree, "/a/b/", "/b/x/");
        tree_move((Tree*)tree, "/b/x/", "/a/b/");
    }

    return NULL;
}

void* move_tester2(void* tree) {
    for (int i = 0; i < ITER; i++) {
        tree_move((Tree*)tree, "/b/x/", "/a/b/");
        tree_move((Tree*)tree, "/a/b/", "/b/x/");
    }

    return NULL;
}

void move_example_test_async() {
    printf("ASYNC EXAMPLE MOVE TEST\n");

    Tree* tree = tree_new();

    tree_create(tree, "/a/");
    tree_create(tree, "/b/");
    tree_create(tree, "/a/b/");
    tree_create(tree, "/a/b/c/");
    tree_create(tree, "/a/b/d/");
    tree_create(tree, "/b/a/");
    tree_create(tree, "/b/a/d/");

    pthread_t t[2];
    pthread_create(&t[0], NULL, move_tester1, (void*)tree);
    pthread_create(&t[1], NULL, move_tester2, (void*)tree);

    pthread_join(t[0], NULL);
    pthread_join(t[1], NULL);

    tree_free(tree);
}

int main(void) {
    example_move_test();
    move_example_test_async();
    random_async_test();

    return 0;
}
