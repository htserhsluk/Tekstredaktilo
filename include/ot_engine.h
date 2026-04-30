#ifndef OT_ENGINE_H
#define OT_ENGINE_H
#include "common.h"

/*
  Hybrid OT + CRDT approach:

  OT  layer  – transforms an incoming Operation against all operations
               that have been applied locally since the sender's vector
               clock diverged.  Adjusts position offsets correctly.

  CRDT layer – uses Vector Clocks (Lamport-style per-client counters)
               to detect causality and decide ordering when two ops are
               concurrent.  Concurrent ops are broken by (client_id,
               timestamp) so every replica converges to the same text.
 */

int  vc_compare(const VectorClock *a, const VectorClock *b);
void vc_merge(VectorClock *dst, const VectorClock *src);
void vc_increment(VectorClock *vc, int client_id);

/* OT: transform op against other, modifying op->position in-place.
   Returns 0 if the op should be applied, -1 if it should be dropped. */
int  ot_transform(Operation *op, const Operation *other);

/* Apply a (possibly transformed) operation to a document buffer.
   doc      – the text buffer (NUL-terminated)
   doc_size – maximum buffer size
   Returns 0 on success, -1 on error. */
int  apply_operation(char *doc, int doc_size, const Operation *op);

#endif