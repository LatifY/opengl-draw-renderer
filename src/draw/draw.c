#include "draw.h"
#include "draw_lines.h"

draw_lines *draw_lines_clone(mg_arena *arena, draw_lines *src)
{
    draw_lines *dst = draw_lines_create(arena, src->allocator, src->color, src->width);
    draw_point_bucket *bucket = src->points.first;
    while (bucket) {
        for (u32 i = 0; i < bucket->size; i++) {
            draw_lines_add_point(dst, bucket->points[i]);
        }
        bucket = bucket->next;
    }
    return dst;
}