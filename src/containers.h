/*
 * ================================================================
 *  containers.h  —  CONTAINER / ARRAY METADATA REGISTRY
 *
 *  The parser calls addContainer() whenever it sees a vector<T>,
 *  list<T>, map<K,V>, set<T>, or T[N] declaration.
 *
 *  Code generators call findContainer() so they can map
 *  .push_back() / .size() / .front() etc. to the correct idiom
 *  for the target language.
 * ================================================================
 */
#ifndef CONTAINERS_H
#define CONTAINERS_H

#include "xpile.h"

/*
 * Record a container variable.
 *   name     — identifier used in source code (e.g. "arr")
 *   kind     — "vector" | "list" | "map" | "set" | "array"
 *   elem     — element type string, e.g. "int" or "int:string" for map<int,string>
 *   ctorArgs — raw constructor argument text, e.g. "26,0"
 */
void addContainer(const char *name, const char *kind,
                  const char *elem, const char *ctorArgs);

/*
 * Look up a container by variable name.
 * Returns a pointer into containers[] or NULL if not found.
 */
ContainerInfo *findContainer(const char *name);

#endif /* CONTAINERS_H */
