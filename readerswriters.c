#include "readerswriters.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "err.h"

// Implementation:
// Each node has a readers and writers problem implemented, where readers
// are threads that won't change anything directly in this node (eg. won't add
// or remove any of the node's children or the node itself).
// Readers allow other reader threads to access children of the node.
//
// Writer is a process that blocks a node, so that it can get exclusive access
// to the node and the subtree with the root at this node, that is no reader
// (or writer) can be in the subtree with a root that has a writer on it.
//
// Threads performing functions create and remove are writers on the parent
// of path, and readers on every ancestor of this parent.
//
// Threads performing list are readers on every node of the path, since they
// don't modify the last node.
//
// In order to avoid problems the threads performing move function are writers
// on the latest common ancestor of two paths, and readers on every ancestor of
// the lca.

struct Monitor {
    size_t read_count;
    size_t write_count;
    size_t read_wait;
    size_t write_wait;

    bool woke_write;
    size_t woke_read;

    pthread_mutex_t mutex;
    pthread_cond_t read_cond;
    pthread_cond_t write_cond;
};

Monitor* init_monitor() {
    Monitor* m = malloc(sizeof(Monitor));
    if (!m) return NULL;

    m->read_count = 0;
    m->write_count = 0;
    m->read_wait = 0;
    m->write_wait = 0;

    m->woke_write = false;
    m->woke_read = 0;

    int err;
    if ((err = pthread_mutex_init(&m->mutex, NULL)))
        syserr(err, "mutex init failed");
    if ((err = pthread_cond_init(&m->read_cond, NULL)))
        syserr(err, "cond init failed");
    if ((err = pthread_cond_init(&m->write_cond, NULL)))
        syserr(err, "cond init failed");
    return m;
}

void free_monitor(Monitor* m) {
    int err;
    if ((err = pthread_mutex_destroy(&m->mutex)))
        syserr(err, "mutex destroy failed");
    if ((err = pthread_cond_destroy(&m->read_cond)))
        syserr(err, "cond destroy failed");
    if ((err = pthread_cond_destroy(&m->write_cond)))
        syserr(err, "cond destroy failed");

    free(m);
}

void begin_write(Monitor* m) {
    int err;
    if ((err = pthread_mutex_lock(&m->mutex))) syserr(err, "mutex lock failed");

    while (m->write_count > 0 || m->read_count > 0 || m->write_wait > 0 ||
           m->read_wait > 0) {
        m->write_wait++;
        if ((err = pthread_cond_wait(&m->write_cond, &m->mutex)))
            syserr(err, "cond wait failed");
        m->write_wait--;
        if (m->woke_write) {
            m->woke_write = false;
            break;
        }
    }

    m->write_count++;
    if ((err = pthread_mutex_unlock(&m->mutex)))
        syserr(err, "mutex unlock failed");
}

void end_write(Monitor* m) {
    int err;
    if ((err = pthread_mutex_lock(&m->mutex))) syserr(err, "mutex lock failed");

    m->write_count--;

    if (m->read_wait > 0 && m->write_count == 0 && m->read_count == 0) {
        m->woke_read = m->read_wait;
        if ((err = pthread_cond_broadcast(&m->read_cond)))
            syserr(err, "cond broadcast failed");
    } else if (m->write_wait > 0 && m->write_count == 0 && m->read_count == 0) {
        m->woke_write = true;
        if ((err = pthread_cond_signal(&m->write_cond)))
            syserr(err, "cond signal failed");
    }
    if ((err = pthread_mutex_unlock(&m->mutex)))
        syserr(err, "mutex unlock failed");
}

void begin_read(Monitor* m) {
    int err;
    if ((err = pthread_mutex_lock(&m->mutex))) syserr(err, "mutex lock failed");

    while (m->write_wait > 0 || m->write_count > 0) {
        m->read_wait++;
        if ((err = pthread_cond_wait(&m->read_cond, &m->mutex)))
            syserr(err, "cond wait failed");
        m->read_wait--;
        if (m->woke_read > 0) {
            m->woke_read--;
            break;
        }
    }
    m->read_count++;

    if ((err = pthread_mutex_unlock(&m->mutex)))
        syserr(err, "mutex unlock failed");
}

void end_read(Monitor* m) {
    int err;
    if ((err = pthread_mutex_lock(&m->mutex))) syserr(err, "mutex lock failed");

    m->read_count--;
    if (m->read_count == 0 && m->write_count == 0 && m->write_wait > 0 &&
        m->woke_read == 0) {
        m->woke_write = true;
        if ((err = pthread_cond_signal(&m->write_cond)))
            syserr(err, "cond signal failed");
    } else if (m->write_count == 0 && m->read_count == 0) {
        m->woke_read = m->read_wait;
        if ((err = pthread_cond_broadcast(&m->read_cond)))
            syserr(err, "cond broadcast failed");
    }

    if ((err = pthread_mutex_unlock(&m->mutex)))
        syserr(err, "mutex unlock failed");
}