#include <string.h>
#include <stdio.h>
#include "ot_engine.h"

void vc_increment(VectorClock *vc, int client_id) {
    if (client_id < 0 || client_id >= MAX_VC_SIZE) return;
    vc->clock[client_id]++;
    if (client_id >= vc->size) vc->size = client_id + 1;
}

void vc_merge(VectorClock *dst, const VectorClock *src) {
    int n = src->size > dst->size ? src->size : dst->size;
    for (int i = 0; i < n; i++) {
        if (src->clock[i] > dst->clock[i])
            dst->clock[i] = src->clock[i];
    }
    if (src->size > dst->size) dst->size = src->size;
}

/*
 * Returns:
 *   1  → a happened-after b
 *  -1  → a happened-before b
 *   0  → identical
 *   2  → concurrent
 */
int vc_compare(const VectorClock *a, const VectorClock *b) {
    int n = a->size > b->size ? a->size : b->size;
    int a_gt = 0, b_gt = 0;
    for (int i = 0; i < n; i++) {
        uint32_t ai = (i < a->size) ? a->clock[i] : 0;
        uint32_t bi = (i < b->size) ? b->clock[i] : 0;
        if (ai > bi) a_gt = 1;
        if (bi > ai) b_gt = 1;
    }
    if ( a_gt && !b_gt) return  1;
    if (!a_gt &&  b_gt) return -1;
    if (!a_gt && !b_gt) return  0;
    return 2; /* concurrent */
}

/* ═══════════════════════════════════════════════════════════════
 *  OT – Operational Transformation
 *
 *  INSERT vs INSERT
 *    If other.pos <= op.pos  →  op.pos += len(other.text)
 *    Tie-break on (timestamp, client_id) for determinism
 *
 *  DELETE vs INSERT
 *    If other.pos < op.pos   →  op.pos += len(other.text)
 *
 *  INSERT vs DELETE
 *    If other.pos <= op.pos
 *       If op.pos < other.pos + other.length → op absorbed → drop
 *       Else op.pos -= other.length
 *
 *  DELETE vs DELETE
 *    Compute overlap and shrink length accordingly.
 * ═══════════════════════════════════════════════════════════════ */

int ot_transform(Operation *op, const Operation *other) {
    if (op->type == OP_INSERT && other->type == OP_INSERT) {
        if (other->position < op->position) {
            op->position += (int)strlen(other->text);
        } else if (other->position == op->position) {
            /* tie-break: lower client_id wins (goes first) → higher shifts right */
            if (other->client_id < op->client_id ||
               (other->client_id == op->client_id && other->timestamp < op->timestamp)) {
                op->position += (int)strlen(other->text);
            }
        }
        return 0;
    }
    if (op->type == OP_DELETE && other->type == OP_INSERT) {
        if (other->position <= op->position) {
            op->position += (int)strlen(other->text);
        }
        return 0;
    }
    if (op->type == OP_INSERT && other->type == OP_DELETE) {
        if (op->position > other->position) {
            if (op->position < other->position + other->length) {
                /* our insert falls inside the deleted range – move to deletion start */
                op->position = other->position;
            } else {
                op->position -= other->length;
            }
        }
        return 0;
    }
    if (op->type == OP_DELETE && other->type == OP_DELETE) {
        /* Four sub-cases based on range overlap */
        int op_end = op->position + op->length;
        int oth_end = other->position + other->length;
        if (oth_end <= op->position) {
            /* other is entirely before op */
            op->position -= other->length;
        } else if (other->position >= op_end) {
            /* other is entirely after op – no change */
        } else {
            /* overlap: shrink op by the overlapping portion */
            int overlap_start = op->position > other->position ? op->position : other->position;
            int overlap_end = op_end < oth_end ? op_end : oth_end;
            int overlap = overlap_end - overlap_start;
            if (other->position < op->position) op->position = other->position;
            op->length -= overlap;
            if (op->length < 0) op->length = 0;
        }
        return 0;
    }
    return 0;
}

// Apply an operation to the document buffer
int apply_operation(char *doc, int doc_size, const Operation *op) {
    int doc_len = (int)strlen(doc);
    if (op->type == OP_INSERT) {
        int ins_len = (int)strlen(op->text);
        int pos = op->position;
        if (pos < 0) pos = 0;
        if (pos > doc_len) pos = doc_len;
        if (doc_len + ins_len + 1 > doc_size) {
            fprintf(stderr, "[OT] INSERT would overflow document buffer\n");
            return -1;
        }
        /* shift everything after pos to the right */
        memmove(doc + pos + ins_len, doc + pos, doc_len - pos + 1);
        memcpy(doc + pos, op->text, ins_len);
        return 0;
    }
    if (op->type == OP_DELETE) {
        int pos = op->position;
        int len = op->length;
        if (pos < 0) pos = 0;
        if (pos >= doc_len) return 0; // nothing to delete
        if (pos + len > doc_len) len = doc_len - pos;
        if (len <= 0) return 0;
        memmove(doc + pos, doc + pos + len, doc_len - pos - len + 1);
        return 0;
    }
    return -1;
}