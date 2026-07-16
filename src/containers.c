/*
 * ================================================================
 *  containers.c  —  CONTAINER / ARRAY METADATA REGISTRY
 * ================================================================
 */
#include <string.h>
#include "xpile.h"
#include "globals.h"
#include "containers.h"

/* ── addContainer ───────────────────────────────────────────────── */
void addContainer(const char *name, const char *kind,
                  const char *elem, const char *ctorArgs) {
    if (containerCount >= MAX_CONTAINERS) return;
    ContainerInfo *c = &containers[containerCount++];
    memset(c, 0, sizeof(*c));
    strncpy(c->name,     name,             MAX_NAME_LEN - 1);
    strncpy(c->kind,     kind,             15);
    strncpy(c->elemType, elem     ? elem     : "", 31);
    strncpy(c->ctorArgs, ctorArgs ? ctorArgs : "", 39);
}

/* ── findContainer ──────────────────────────────────────────────── */
ContainerInfo *findContainer(const char *name) {
    for (int i = 0; i < containerCount; i++)
        if (!strcmp(containers[i].name, name))
            return &containers[i];
    return NULL;
}
