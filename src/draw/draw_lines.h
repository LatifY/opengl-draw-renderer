#ifndef DRAW_LINES_H
#define DRAW_LINES_H

#include "base/base.h"
#include "draw_point_bucket.h"
#include "gfx/gfx.h"

// Contents defined in draw backends
typedef struct draw_lines_shaders draw_lines_shaders;

draw_lines_shaders* draw_lines_shaders_create(mg_arena* arena);
void draw_lines_shaders_destroy(draw_lines_shaders* shaders);

typedef struct {
    vec4f color;
    f32 width;

    // This will not always be 100% accurate, but it should always contain the lines
    rectf bounding_box;

    draw_point_allocator* allocator;
    draw_point_list points;

    struct _draw_lines_backend* backend;
} draw_lines;

// Creates lines with the specified points
draw_lines* draw_lines_from_points(mg_arena* arena, draw_point_allocator* allocator, vec2f* points, u32 num_points, vec4f col, f32 line_width);
// Creates an empty lines object
draw_lines* draw_lines_create(mg_arena* arena, draw_point_allocator* allocator, vec4f col, f32 line_width);
void draw_lines_destroy(draw_lines* lines);

// Deletes all the points
void draw_lines_clear(draw_lines* lines);
void draw_lines_reinit(draw_lines* lines, vec4f col, f32 width);

void draw_lines_draw(const draw_lines* lines, const draw_lines_shaders* shaders, const gfx_window* win, viewf view);
// Updates the geometry of the lines with the new color and width
void draw_lines_update(draw_lines* lines, vec4f col, f32 line_width);
void draw_lines_add_point(draw_lines* lines, vec2f point);
void draw_lines_change_last(draw_lines* lines, vec2f new_last);

b32 draw_lines_collide_circle(draw_lines* lines, circlef circle);

#endif // DRAW_LINES_H

