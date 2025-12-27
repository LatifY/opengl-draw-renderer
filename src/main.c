#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "base/base.h"
#include "os/os.h"
#include "gfx/gfx.h"
#include "gfx/opengl/opengl.h"
#include "gfx/opengl/opengl_helpers.h"

#include "draw/draw.h"

#define WIDTH 1280
#define HEIGHT 720

#define INTERP_MARGIN 0.01f

typedef struct
{
    f32 zoom_speed;
    f32 zoom_smoothness;
    f32 default_color_r;
    f32 default_color_g;
    f32 default_color_b;
} app_config;

typedef enum
{
    UNDO_DRAW,
    UNDO_ERASE
} undo_action_type;

typedef struct
{
    undo_action_type type;
    u32 line_idx;
    draw_lines *backup;
} undo_action;

app_config load_config(const char *filename)
{
    app_config config = {10.0f, 5.0f, 1.0f, 1.0f, 1.0f}; // Defaults
    FILE *f = fopen(filename, "r");
    if (f)
    {
        char line[128];
        while (fgets(line, sizeof(line), f))
        {
            char key[64];
            f32 val;
            if (sscanf(line, "%s %f", key, &val) == 2)
            {
                if (strcmp(key, "zoom_speed") == 0)
                    config.zoom_speed = val;
                else if (strcmp(key, "zoom_smoothness") == 0)
                    config.zoom_smoothness = val;
                else if (strcmp(key, "default_color_r") == 0)
                    config.default_color_r = val;
                else if (strcmp(key, "default_color_g") == 0)
                    config.default_color_g = val;
                else if (strcmp(key, "default_color_b") == 0)
                    config.default_color_b = val;
            }
        }
        fclose(f);
    }
    return config;
}

static const char *basic_vert = GLSL_SOURCE(
    330,

    layout(location = 0) in vec2 a_pos;

    uniform mat3 u_view_mat;

    void main() {
        vec2 pos = (u_view_mat * vec3(a_pos, 1.0)).xy;
        gl_Position = vec4(pos, 0.0, 1.0);
    });

static const char *basic_frag = GLSL_SOURCE(
    330,

    layout(location = 0) out vec4 out_col;

    uniform vec4 u_col;

    void main() {
        out_col = u_col;
    });

void mga_err(mga_error err)
{
    printf("MGA ERROR %d: %s", err.code, err.msg);
}
int main(void)
{
    mga_desc desc = {
        .desired_max_size = MGA_MiB(16),
        .desired_block_size = MGA_KiB(256),
        .error_callback = mga_err};
    mg_arena *perm_arena = mga_create(&desc);

    app_config config = load_config("settings.txt");

    gfx_window *win = gfx_win_create(perm_arena, WIDTH, HEIGHT, STR8("OpenGL Drawing C"));
    gfx_win_make_current(win);

    u32 basic_program = glh_create_shader(basic_vert, basic_frag);

    glUseProgram(basic_program);
    u32 basic_view_mat_loc = glGetUniformLocation(basic_program, "u_view_mat");
    u32 basic_col_loc = glGetUniformLocation(basic_program, "u_col");

    draw_lines_shaders *shaders = draw_lines_shaders_create(perm_arena);
    draw_point_allocator *point_allocator = draw_point_alloc_create(perm_arena);

    /*u32 w = 500;
    u32 h = 400;
    vec2f* points = MGA_PUSH_ZERO_ARRAY(perm_arena, vec2f, w * h);

    srand(time(NULL));

    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            f32 v_y = -500.0f + ((f32)y / h) * 1000.0f;
            //v_y += ((f32)rand() / (f32)RAND_MAX) * 3.0f - 1.5f;
            v_y += (x % 2) * 4.0f - 2.0f;
            f32 v_x = -500.0f + ((f32)x / w) * 1000.0f;
            if ((y % 2) == 1) {
                v_x = -v_x;
            }

            points[x + y * w] = (vec2f){ v_x, v_y };
        }
    }*/

    u32 num_lines = 0;
    draw_lines *lines[1024] = {0};
    undo_action undo_stack[1024];
    u32 undo_count = 0;

    vec2f rect_verts[] = {
        {-250.0f, 250.0f},
        {-250.0f, -250.0f},
        {250.0f, -250.0f},
        {250.0f, 250.0f}};

    u32 rect_indices[] = {
        0, 1, 2,
        0, 2, 3};

    u32 vertex_array = 0;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);

    u32 vertex_buffer = glh_create_buffer(GL_ARRAY_BUFFER, sizeof(rect_verts), rect_verts, GL_DYNAMIC_DRAW);
    u32 index_buffer = glh_create_buffer(GL_ELEMENT_ARRAY_BUFFER, sizeof(rect_indices), rect_indices, GL_STATIC_DRAW);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Dark background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    viewf view = {
        .center = {0, 0},
        .aspect_ratio = (f32)win->width / win->height,
        .width = win->width,
        .rotation = 0.0f};
    mat3f view_mat = {0};
    mat3f inv_view_mat = {0};
    mat3f_from_view(&view_mat, view);
    mat3f_inverse(&inv_view_mat, &view_mat);

    gfx_win_process_events(win);

    vec2f prev_mouse_pos = win->mouse_pos;
    vec2f prev_point = prev_mouse_pos;
    vec2f prev_prev_point = prev_mouse_pos;

    b32 erase = false;
    b32 extending_point = false;

    f32 target_zoom_width = view.width;
    vec4f current_color = {config.default_color_r, config.default_color_g, config.default_color_b, 1.0f};

#define NUM_COLORS 8
    vec4f colors[NUM_COLORS] = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.5f, 0.0f, 1.0f},
        {1.0f, 0.4f, 0.7f, 1.0f}};
    int color_idx = 0;
    b32 eraser_mode = false;
    f32 brush_size = 5.0f;
    f32 eraser_size = 25.0f;

    f32 btn_size = 30.0f;
    f32 btn_padding = 10.0f;
    f32 start_x = 20.0f;
    f32 start_y = 20.0f;
    f32 eraser_pad = 50.0f;
    rectf color_buttons[NUM_COLORS];
    for (int i = 0; i < NUM_COLORS; i++)
    {
        color_buttons[i] = (rectf){start_x, start_y + i * (btn_size + btn_padding), btn_size, btn_size};
    }
    rectf eraser_button = {start_x, start_y + eraser_pad + NUM_COLORS * (btn_size + btn_padding), btn_size, btn_size};
    rectf size_up_button = {start_x, start_y + eraser_pad + (NUM_COLORS + 1) * (btn_size + btn_padding), btn_size, btn_size};
    rectf size_down_button = {start_x, start_y + eraser_pad + (NUM_COLORS + 2) * (btn_size + btn_padding), btn_size, btn_size};

    os_time_init();

    u64 prev_frame = os_now_usec();
    while (!win->should_close)
    {
        u64 cur_frame = os_now_usec();
        f32 delta = (f32)(cur_frame - prev_frame) / 1e6;
        prev_frame = cur_frame;

#ifndef PLATFORM_WASM
        gfx_win_process_events(win);
#endif

        // Update

        f32 move_speed = view.width;

        view.aspect_ratio = (f32)win->width / win->height;

        // Smooth zoom
        target_zoom_width *= 1.0f + (-config.zoom_speed * win->mouse_scroll * delta);
        view.width += (target_zoom_width - view.width) * config.zoom_smoothness * delta;

        if (GFX_IS_KEY_DOWN(win, GFX_KEY_W))
        {
            view.center.y -= move_speed * delta;
        }
        if (GFX_IS_KEY_DOWN(win, GFX_KEY_S))
        {
            view.center.y += move_speed * delta;
        }
        if (GFX_IS_KEY_DOWN(win, GFX_KEY_A))
        {
            view.center.x -= move_speed * delta;
        }
        if (GFX_IS_KEY_DOWN(win, GFX_KEY_D))
        {
            view.center.x += move_speed * delta;
        }

        mat3f_from_view(&view_mat, view);

        mat3f_inverse(&inv_view_mat, &view_mat);

        vec2f mouse_pos = (vec2f){
            2.0f * win->mouse_pos.x / win->width - 1.0f,
            -(2.0f * win->mouse_pos.y / win->height - 1.0f),
        };
        mouse_pos = mat3f_mul_vec2f(&inv_view_mat, mouse_pos);

        if (GFX_IS_KEY_DOWN(win, GFX_KEY_LCONTROL) && GFX_IS_KEY_JUST_DOWN(win, GFX_KEY_Z))
        {
            if (undo_count > 0)
            {
                undo_action *ua = &undo_stack[--undo_count];
                if (ua->type == UNDO_DRAW && num_lines > 0)
                {
                    draw_lines_clear(lines[num_lines - 1]);
                    num_lines--;
                }
                else if (ua->type == UNDO_ERASE && ua->backup)
                {
                    lines[num_lines++] = ua->backup;
                }
            }
        }

        b32 click_on_ui = false;
        vec2f screen_mouse_pos = win->mouse_pos;

        if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
        {
            for (int i = 0; i < NUM_COLORS; i++)
            {
                if (vec2f_in_rectf(screen_mouse_pos, color_buttons[i]))
                {
                    color_idx = i;
                    current_color = colors[i];
                    eraser_mode = false;
                    click_on_ui = true;
                    break;
                }
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, eraser_button))
            {
                eraser_mode = true;
                click_on_ui = true;
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, size_up_button))
            {
                brush_size += 2.0f;
                if (brush_size > 50.0f)
                    brush_size = 50.0f;
                eraser_size += 5.0f;
                if (eraser_size > 100.0f)
                    eraser_size = 100.0f;
                click_on_ui = true;
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, size_down_button))
            {
                brush_size -= 2.0f;
                if (brush_size < 1.0f)
                    brush_size = 1.0f;
                eraser_size -= 5.0f;
                if (eraser_size < 5.0f)
                    eraser_size = 5.0f;
                click_on_ui = true;
            }
        }

        erase = eraser_mode;

        if (click_on_ui)
        {
        }
        else if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
        {
            if (!erase)
            {
                num_lines++;

                if (lines[num_lines - 1] == NULL)
                {
                    lines[num_lines - 1] = draw_lines_create(perm_arena, point_allocator, current_color, brush_size);
                }
                else
                {
                    draw_lines_reinit(lines[num_lines - 1], current_color, brush_size);
                }

                draw_lines_add_point(lines[num_lines - 1], mouse_pos);

                prev_point = mouse_pos;
                prev_prev_point = prev_point;

                undo_stack[undo_count++] = (undo_action){UNDO_DRAW, num_lines - 1, NULL};
            }
        }
        else if (!erase && num_lines > 0 &&
                 (GFX_IS_MOUSE_DOWN(win, GFX_MB_LEFT) || GFX_IS_MOUSE_JUST_UP(win, GFX_MB_LEFT)) &&
                 !vec2f_eq(mouse_pos, prev_mouse_pos))
        {
            if (!vec2f_eq(mouse_pos, prev_point))
            {
                f32 prev_dist = vec2f_dist(prev_point, mouse_pos);
                u32 num_points = (u32)roundf(prev_dist / (view.width * INTERP_MARGIN)) + 1;

                extending_point = false;

                // Catmull-Rom endpoint interpolation coefficients
                // https://danceswithcode.net/engineeringnotes/interpolation/interpolation.html
                vec2f c0 = prev_point;
                vec2f c1 = vec2f_scl(vec2f_sub(mouse_pos, prev_prev_point), 0.5f);
                vec2f c2 = vec2f_add(vec2f_sub(mouse_pos, vec2f_scl(prev_point, 2.0f)), prev_prev_point);
                c2 = vec2f_scl(c2, 0.5f);

                f32 t_interval = 1.0f / (f32)(num_points + 1);
                f32 t = t_interval;

                for (u32 i = 0; i < num_points; i++)
                {
                    vec2f p = c0;
                    p = vec2f_add(p, vec2f_scl(c1, t));
                    p = vec2f_add(p, vec2f_scl(c2, t * t));

                    draw_lines_add_point(lines[num_lines - 1], p);

                    t += t_interval;
                }

                prev_prev_point = prev_point;
                prev_point = mouse_pos;
            }
        }
        prev_mouse_pos = mouse_pos;

        for (i64 i = 0; i < num_lines; i++)
        {
            if (erase && GFX_IS_MOUSE_DOWN(win, GFX_MB_LEFT) && draw_lines_collide_circle(lines[i], (circlef){mouse_pos, eraser_size}))
            {
                draw_lines *backup = draw_lines_clone(perm_arena, lines[i]);
                undo_stack[undo_count++] = (undo_action){UNDO_ERASE, i, backup};

                draw_lines_clear(lines[i]);
                draw_lines *cleared_line = lines[i];

                num_lines--;

                for (i64 j = i; j < num_lines; j++)
                {
                    lines[j] = lines[j + 1];
                }
                lines[num_lines] = cleared_line;

                i--;
            }
        }

        gfx_win_clear(win);

        // Draw

        // Rect draw (Canvas)
        {
            glUseProgram(basic_program);
            glUniformMatrix3fv(basic_view_mat_loc, 1, GL_FALSE, view_mat.m);

            glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f); // White canvas

            glBindVertexArray(vertex_array);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

            // A4 Ratio approx (210x297) scaled up
            f32 cw = 210.0f * 4.0f;
            f32 ch = 297.0f * 4.0f;
            vec2f canvas_verts[] = {
                {-cw / 2, ch / 2},
                {-cw / 2, -ch / 2},
                {cw / 2, -ch / 2},
                {cw / 2, ch / 2}};

            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(canvas_verts), canvas_verts);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

            glDisableVertexAttribArray(0);
        }

        for (u32 i = 0; i < num_lines; i++)
        {
            draw_lines_draw(lines[i], shaders, win, view);
        }

        {
            glUseProgram(basic_program);

            mat3f ui_mat = {0};
            ui_mat.m[0] = 2.0f / win->width;
            ui_mat.m[4] = -2.0f / win->height;
            ui_mat.m[8] = 1.0f;
            ui_mat.m[6] = -1.0f;
            ui_mat.m[7] = 1.0f;

            glUniformMatrix3fv(basic_view_mat_loc, 1, GL_FALSE, ui_mat.m);

            glBindVertexArray(vertex_array);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);

            for (int i = 0; i < NUM_COLORS; i++)
            {
                vec4f col = colors[i];

                if (i == 0)
                {
                    glUniform4f(basic_col_loc, 0.3f, 0.3f, 0.3f, 1.0f);
                    rectf br = {color_buttons[i].x - 2, color_buttons[i].y - 2, color_buttons[i].w + 4, color_buttons[i].h + 4};
                    vec2f bverts[] = {
                        {br.x, br.y},
                        {br.x, br.y + br.h},
                        {br.x + br.w, br.y + br.h},
                        {br.x + br.w, br.y}};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bverts), bverts);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                }

                glUniform4f(basic_col_loc, col.x, col.y, col.z, col.w);

                rectf r = color_buttons[i];
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + r.h},
                    {r.x + r.w, r.y + r.h},
                    {r.x + r.w, r.y}};

                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);

                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            glUniform4f(basic_col_loc, 1.0f, 0.4f, 0.7f, 1.0f);
            {
                rectf r = eraser_button;
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + r.h},
                    {r.x + r.w, r.y + r.h},
                    {r.x + r.w, r.y}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            glUniform4f(basic_col_loc, 0.5f, 0.5f, 0.5f, 1.0f);
            {
                rectf r = size_up_button;
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + r.h},
                    {r.x + r.w, r.y + r.h},
                    {r.x + r.w, r.y}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

                glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                vec2f plus_h[] = {{cx - 8, cy - 2}, {cx - 8, cy + 2}, {cx + 8, cy + 2}, {cx + 8, cy - 2}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(plus_h), plus_h);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                vec2f plus_v[] = {{cx - 2, cy - 8}, {cx - 2, cy + 8}, {cx + 2, cy + 8}, {cx + 2, cy - 8}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(plus_v), plus_v);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            glUniform4f(basic_col_loc, 0.5f, 0.5f, 0.5f, 1.0f);
            {
                rectf r = size_down_button;
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + r.h},
                    {r.x + r.w, r.y + r.h},
                    {r.x + r.w, r.y}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

                glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                vec2f minus_h[] = {{cx - 8, cy - 2}, {cx - 8, cy + 2}, {cx + 8, cy + 2}, {cx + 8, cy - 2}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(minus_h), minus_h);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            if (!eraser_mode)
            {
                glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);
                rectf r = color_buttons[color_idx];
                vec2f center = {r.x + r.w / 2, r.y + r.h / 2};
                f32 s = 5.0f;
                vec2f dot_verts[] = {
                    {center.x - s, center.y - s},
                    {center.x - s, center.y + s},
                    {center.x + s, center.y + s},
                    {center.x + s, center.y - s}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(dot_verts), dot_verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }
            else
            {
                glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);
                rectf r = eraser_button;
                vec2f center = {r.x + r.w / 2, r.y + r.h / 2};
                f32 s = 5.0f;
                vec2f dot_verts[] = {
                    {center.x - s, center.y - s},
                    {center.x - s, center.y + s},
                    {center.x + s, center.y + s},
                    {center.x + s, center.y - s}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(dot_verts), dot_verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            glDisableVertexAttribArray(0);
        }

        {
            glUseProgram(basic_program);
            glUniformMatrix3fv(basic_view_mat_loc, 1, GL_FALSE, view_mat.m);
            glBindVertexArray(vertex_array);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);

            f32 cursor_size = erase ? eraser_size : brush_size;
            vec4f cursor_color = erase ? (vec4f){1.0f, 0.4f, 0.7f, 0.6f} : (vec4f){current_color.x, current_color.y, current_color.z, 0.6f};

            glUniform4f(basic_col_loc, cursor_color.x, cursor_color.y, cursor_color.z, cursor_color.w);

            int segments = 32;
            for (int seg = 0; seg < segments; seg++)
            {
                f32 angle1 = (f32)seg / segments * 6.28318f;
                f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                vec2f p0 = mouse_pos;
                vec2f p1 = {mouse_pos.x + cosf(angle1) * cursor_size, mouse_pos.y + sinf(angle1) * cursor_size};
                vec2f p2 = {mouse_pos.x + cosf(angle2) * cursor_size, mouse_pos.y + sinf(angle2) * cursor_size};

                vec2f tri_verts[] = {p0, p1, p2, p0};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri_verts), tri_verts);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }

            glDisableVertexAttribArray(0);
        }

        gfx_win_swap_buffers(win);

#ifdef PLATFORM_WASM
        gfx_win_process_events(win);
#endif

        os_sleep_ms(2);
    }

    for (u32 i = 0; i < num_lines; i++)
    {
        draw_lines_destroy(lines[i]);
    }

    draw_lines_shaders_destroy(shaders);
    draw_point_alloc_destroy(point_allocator);

    glDeleteBuffers(1, &vertex_buffer);
    glDeleteBuffers(1, &index_buffer);
    glDeleteVertexArrays(1, &vertex_array);

    glDeleteProgram(basic_program);

    gfx_win_destroy(win);

    mga_destroy(perm_arena);

    return 0;
}
