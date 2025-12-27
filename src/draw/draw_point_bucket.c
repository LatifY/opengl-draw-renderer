#include "draw_point_bucket.h"

#include <stdio.h>
#include <string.h>

draw_point_allocator* draw_point_alloc_create(mg_arena* backing_arena) {
    mg_arena* arena = backing_arena;

    b32 owned_arena = false;

    if (arena == NULL) {
        owned_arena = true;

        mga_desc desc = {
            // This should allow for a little over 2 million points
            .desired_max_size = MGA_MiB(16),
            .desired_block_size = MGA_MiB(1),
        };
        arena = mga_create(&desc);
    }

    draw_point_allocator* point_alloc = MGA_PUSH_ZERO_STRUCT(arena, draw_point_allocator);

    point_alloc->owned_arena = owned_arena;
    point_alloc->backing_arena = arena;

    return point_alloc;
}
void draw_point_alloc_destroy(draw_point_allocator* point_alloc) {
    if (point_alloc == NULL) {
        fprintf(stderr, "Cannot destroy NULL point allocator\n");
        return;
    }

    if (point_alloc->owned_arena) {
        mga_destroy(point_alloc->backing_arena);
    }
}

draw_point_bucket* draw_point_alloc_alloc(draw_point_allocator* point_alloc) {
    if (point_alloc == NULL) {
        fprintf(stderr, "Cannot alloc with NULL point allocator\n");
        return NULL;
    }

    // Free node in list exists
    if (point_alloc->free_first != NULL) {
        draw_point_bucket* out = point_alloc->free_first;

        SLL_POP_FRONT(point_alloc->free_first, point_alloc->free_last);

        out->size = 0;
        out->next = NULL;
        memset(out->points, 0, sizeof(vec2f) * DRAW_POINT_BUCKET_SIZE);

        return out;
    }

    draw_point_bucket* out = MGA_PUSH_ZERO_STRUCT(point_alloc->backing_arena, draw_point_bucket);

    return out;
}
void draw_point_alloc_free(draw_point_allocator* point_alloc, draw_point_bucket* bucket) {
    if (point_alloc == NULL) {
        fprintf(stderr, "Cannot free with NULL point allocator\n");
        return;
    }

    SLL_PUSH_FRONT(point_alloc->free_first, point_alloc->free_last, bucket);
}

void draw_point_list_add(draw_point_list* list, vec2f point) {
    if (list == NULL) {
        fprintf(stderr, "Cannot add point to NULL list\n");
        return;
    }

    list->size++;

    if (list->last == NULL || list->last->size == DRAW_POINT_BUCKET_SIZE) {
        draw_point_bucket* bucket = draw_point_alloc_alloc(list->allocator);
        bucket->size = 1;
        bucket->points[0] = point;

        SLL_PUSH_BACK(list->first, list->last, bucket);

        return;
    }

    list->last->points[list->last->size++] = point;
}
void draw_point_list_clear(draw_point_list* list) {
    if (list == NULL) {
        fprintf(stderr, "Cannot clear NULL list\n");
        return;
    }

    while (list->first != NULL) {
        draw_point_bucket* bucket = list->first;
        SLL_POP_FRONT(list->first, list->last);

        draw_point_alloc_free(list->allocator, bucket);
    }

    list->size = 0;
}

