#ifndef DRAW_H
#define DRAW_H

#include "base/base.h"

#define DRAW_BACKEND_OPENGL

#include "draw_lines.h"
#include "draw_point_bucket.h"

draw_lines *draw_lines_clone(mg_arena *arena, draw_lines *src);

#endif // DRAW_H

