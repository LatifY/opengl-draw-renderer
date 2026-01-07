#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL/gl.h>
#endif

#include "base/base.h"
#include "os/os.h"
#include "gfx/gfx.h"
#include "gfx/opengl/opengl.h"
#include "gfx/opengl/opengl_helpers.h"

#include "draw/draw.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

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
    UNDO_ERASE,
    UNDO_IMAGE_ADD,
    UNDO_IMAGE_REMOVE
} undo_action_type;

typedef struct
{
    u32 texture_id;
    f32 x, y;
    f32 width, height;
    b32 active;
} pasted_image;

typedef struct
{
    undo_action_type type;
    u32 line_idx;
    draw_lines *backup;
    u32 image_idx;
    pasted_image image_backup;
} undo_action;

app_config load_config(const char *filename)
{
    app_config config = {10.0f, 5.0f, 1.0f, 1.0f, 1.0f}; // defaults
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

static const char *texture_vert = GLSL_SOURCE(
    330,

    layout(location = 0) in vec2 a_pos;
    layout(location = 1) in vec2 a_uv;

    uniform mat3 u_view_mat;

    out vec2 v_uv;

    void main() {
        vec2 pos = (u_view_mat * vec3(a_pos, 1.0)).xy;
        gl_Position = vec4(pos, 0.0, 1.0);
        v_uv = a_uv;
    });

static const char *texture_frag = GLSL_SOURCE(
    330,

    in vec2 v_uv;
    layout(location = 0) out vec4 out_col;

    uniform sampler2D u_texture;

    void main() {
        out_col = texture(u_texture, v_uv);
    });

#ifdef PLATFORM_WIN32
#include <shellapi.h>

static u32 create_texture_from_data(unsigned char *pixels, int width, int height)
{
    u32 texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return texture;
}

static u32 create_texture_from_file(const char *filename, int *out_width, int *out_height)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data)
    {
        printf("Failed to load image: %s\n", filename);
        return 0;
    }

    u32 texture = create_texture_from_data(data, width, height);
    stbi_image_free(data);

    *out_width = width;
    *out_height = height;
    return texture;
}

static u32 create_texture_from_clipboard(HWND hwnd, int *out_width, int *out_height)
{
    *out_width = 0;
    *out_height = 0;

    if (!OpenClipboard(hwnd))
    {
        printf("OpenClipboard failed\n");
        return 0;
    }

    // Debug: print available formats and their names
    UINT format = 0;
    printf("Available clipboard formats:\n");
    while ((format = EnumClipboardFormats(format)) != 0)
    {
        char formatName[256] = {0};
        if (GetClipboardFormatNameA(format, formatName, sizeof(formatName)) > 0)
        {
            printf("  %u: %s\n", format, formatName);
        }
        else
        {
            printf("  %u: (standard format)\n", format);
        }
    }

    // Try PNG format first (registered format from browsers/modern apps)
    UINT pngFormat = RegisterClipboardFormatA("PNG");
    if (IsClipboardFormatAvailable(pngFormat))
    {
        printf("PNG format available\n");
        HANDLE hData = GetClipboardData(pngFormat);
        if (hData)
        {
            SIZE_T size = GlobalSize(hData);
            void *pngData = GlobalLock(hData);
            if (pngData && size > 0)
            {
                int width, height, channels;
                unsigned char *pixels = stbi_load_from_memory((unsigned char *)pngData, (int)size, &width, &height, &channels, 4);
                GlobalUnlock(hData);

                if (pixels)
                {
                    printf("PNG decoded: %dx%d\n", width, height);
                    u32 texture = create_texture_from_data(pixels, width, height);
                    stbi_image_free(pixels);
                    CloseClipboard();
                    *out_width = width;
                    *out_height = height;
                    return texture;
                }
            }
            else
            {
                GlobalUnlock(hData);
            }
        }
    }

    // Try file drop (CF_HDROP) - when image files are copied
    if (IsClipboardFormatAvailable(CF_HDROP))
    {
        printf("CF_HDROP (file) available\n");
        HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);
        if (hDrop)
        {
            UINT fileCount = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            if (fileCount > 0)
            {
                char filePath[MAX_PATH];
                DragQueryFileA(hDrop, 0, filePath, MAX_PATH);
                printf("File: %s\n", filePath);

                // Check if it's an image file
                char *ext = strrchr(filePath, '.');
                if (ext && (_stricmp(ext, ".png") == 0 || _stricmp(ext, ".jpg") == 0 ||
                            _stricmp(ext, ".jpeg") == 0 || _stricmp(ext, ".bmp") == 0 ||
                            _stricmp(ext, ".gif") == 0))
                {
                    CloseClipboard();
                    return create_texture_from_file(filePath, out_width, out_height);
                }
            }
        }
    }

    if (IsClipboardFormatAvailable(CF_DIBV5))
    {
        printf("CF_DIBV5 available\n");
        HANDLE hDib = GetClipboardData(CF_DIBV5);
        if (hDib)
        {
            BITMAPV5HEADER *bih = (BITMAPV5HEADER *)GlobalLock(hDib);
            if (bih)
            {
                int width = bih->bV5Width;
                int height = bih->bV5Height < 0 ? -bih->bV5Height : bih->bV5Height;
                int bitCount = bih->bV5BitCount;
                b32 topDown = bih->bV5Height < 0;

                printf("DIBV5: %dx%d, %d bits\n", width, height, bitCount);

                unsigned char *srcData = (unsigned char *)bih + bih->bV5Size;
                unsigned char *pixels = (unsigned char *)malloc(width * height * 4);

                int srcStride = ((width * bitCount + 31) / 32) * 4;

                for (int y = 0; y < height; y++)
                {
                    int srcY = topDown ? y : (height - 1 - y);
                    unsigned char *srcRow = srcData + srcY * srcStride;
                    unsigned char *dstRow = pixels + y * width * 4;

                    for (int x = 0; x < width; x++)
                    {
                        if (bitCount == 32)
                        {
                            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                            dstRow[x * 4 + 3] = srcRow[x * 4 + 3] ? srcRow[x * 4 + 3] : 255;
                        }
                        else if (bitCount == 24)
                        {
                            dstRow[x * 4 + 0] = srcRow[x * 3 + 2];
                            dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
                            dstRow[x * 4 + 2] = srcRow[x * 3 + 0];
                            dstRow[x * 4 + 3] = 255;
                        }
                    }
                }

                u32 texture = create_texture_from_data(pixels, width, height);
                free(pixels);
                GlobalUnlock(hDib);
                CloseClipboard();

                *out_width = width;
                *out_height = height;
                return texture;
            }
            GlobalUnlock(hDib);
        }
    }

    if (IsClipboardFormatAvailable(CF_DIB))
    {
        printf("CF_DIB available\n");
        HANDLE hDib = GetClipboardData(CF_DIB);
        if (hDib)
        {
            BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)GlobalLock(hDib);
            if (bih)
            {
                int width = bih->biWidth;
                int height = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
                int bitCount = bih->biBitCount;
                b32 topDown = bih->biHeight < 0;

                printf("DIB: %dx%d, %d bits\n", width, height, bitCount);

                int colorTableSize = 0;
                if (bitCount <= 8)
                {
                    colorTableSize = (bih->biClrUsed ? bih->biClrUsed : (1 << bitCount)) * sizeof(RGBQUAD);
                }

                unsigned char *srcData = (unsigned char *)bih + bih->biSize + colorTableSize;
                unsigned char *pixels = (unsigned char *)malloc(width * height * 4);

                int srcStride = ((width * bitCount + 31) / 32) * 4;

                for (int y = 0; y < height; y++)
                {
                    int srcY = topDown ? y : (height - 1 - y);
                    unsigned char *srcRow = srcData + srcY * srcStride;
                    unsigned char *dstRow = pixels + y * width * 4;

                    for (int x = 0; x < width; x++)
                    {
                        if (bitCount == 32)
                        {
                            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                            dstRow[x * 4 + 3] = srcRow[x * 4 + 3] ? srcRow[x * 4 + 3] : 255;
                        }
                        else if (bitCount == 24)
                        {
                            dstRow[x * 4 + 0] = srcRow[x * 3 + 2];
                            dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
                            dstRow[x * 4 + 2] = srcRow[x * 3 + 0];
                            dstRow[x * 4 + 3] = 255;
                        }
                    }
                }

                u32 texture = create_texture_from_data(pixels, width, height);
                free(pixels);
                GlobalUnlock(hDib);
                CloseClipboard();

                *out_width = width;
                *out_height = height;
                return texture;
            }
            GlobalUnlock(hDib);
        }
    }

    if (IsClipboardFormatAvailable(CF_BITMAP))
    {
        printf("CF_BITMAP available\n");
        HBITMAP hBitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (hBitmap)
        {
            BITMAP bmp = {0};
            GetObject(hBitmap, sizeof(BITMAP), &bmp);

            int width = bmp.bmWidth;
            int height = bmp.bmHeight;
            printf("BITMAP: %dx%d\n", width, height);

            if (width > 0 && height > 0)
            {
                HDC hdcScreen = GetDC(NULL);
                HDC hdcMem = CreateCompatibleDC(hdcScreen);
                HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

                BITMAPINFOHEADER bi = {0};
                bi.biSize = sizeof(BITMAPINFOHEADER);
                bi.biWidth = width;
                bi.biHeight = -height;
                bi.biPlanes = 1;
                bi.biBitCount = 32;
                bi.biCompression = BI_RGB;

                unsigned char *pixels = (unsigned char *)malloc(width * height * 4);
                GetDIBits(hdcMem, hBitmap, 0, height, pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

                for (int i = 0; i < width * height; i++)
                {
                    unsigned char temp = pixels[i * 4];
                    pixels[i * 4] = pixels[i * 4 + 2];
                    pixels[i * 4 + 2] = temp;
                    pixels[i * 4 + 3] = 255;
                }

                u32 texture = create_texture_from_data(pixels, width, height);
                free(pixels);
                SelectObject(hdcMem, hOldBitmap);
                DeleteDC(hdcMem);
                ReleaseDC(NULL, hdcScreen);
                CloseClipboard();

                *out_width = width;
                *out_height = height;
                return texture;
            }
        }
    }

    printf("No supported clipboard format found\n");
    CloseClipboard();
    return 0;
}
#endif

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
    u32 texture_program = glh_create_shader(texture_vert, texture_frag);

    glUseProgram(basic_program);
    u32 basic_view_mat_loc = glGetUniformLocation(basic_program, "u_view_mat");
    u32 basic_col_loc = glGetUniformLocation(basic_program, "u_col");

    glUseProgram(texture_program);
    u32 texture_view_mat_loc = glGetUniformLocation(texture_program, "u_view_mat");
    u32 texture_sampler_loc = glGetUniformLocation(texture_program, "u_texture");

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

    pasted_image images[128] = {0};
    u32 num_images = 0;
    i32 selected_image = -1;
    b32 dragging_image = false;
    vec2f image_drag_offset = {0, 0};

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

#define NUM_COLORS 10
    vec4f colors[NUM_COLORS] = {
        {0.15f, 0.15f, 0.15f, 1.0f},
        {0.95f, 0.95f, 0.95f, 1.0f},
        {0.91f, 0.30f, 0.24f, 1.0f},
        {0.18f, 0.80f, 0.44f, 1.0f},
        {0.20f, 0.60f, 0.86f, 1.0f},
        {0.95f, 0.77f, 0.06f, 1.0f},
        {0.61f, 0.35f, 0.71f, 1.0f},
        {0.90f, 0.49f, 0.13f, 1.0f},
        {0.10f, 0.74f, 0.61f, 1.0f},
        {0.94f, 0.47f, 0.65f, 1.0f}};
    int color_idx = 0;
    b32 eraser_mode = false;
    f32 brush_size = 5.0f;
    f32 eraser_size = 25.0f;

    f32 btn_size = 32.0f;
    f32 btn_padding = 6.0f;
    f32 panel_padding = 12.0f;
    f32 panel_width = btn_size + panel_padding * 2;
    f32 panel_x = 16.0f;
    f32 panel_y = 16.0f;
    f32 section_gap = 16.0f;
    rectf color_buttons[NUM_COLORS];
    for (int i = 0; i < NUM_COLORS; i++)
    {
        int row = i / 2;
        int col = i % 2;
        color_buttons[i] = (rectf){
            panel_x + panel_padding + col * (btn_size / 2 + 2),
            panel_y + panel_padding + row * (btn_size / 2 + 2),
            btn_size / 2,
            btn_size / 2};
    }
    f32 tools_start_y = panel_y + panel_padding + 5 * (btn_size / 2 + 2) + section_gap;
    rectf eraser_button = {panel_x + panel_padding, tools_start_y, btn_size, btn_size};
    rectf size_up_button = {panel_x + panel_padding, tools_start_y + btn_size + btn_padding, btn_size, btn_size};
    rectf size_down_button = {panel_x + panel_padding, tools_start_y + 2 * (btn_size + btn_padding), btn_size, btn_size};
    rectf export_button = {panel_x + panel_padding, tools_start_y + 3 * (btn_size + btn_padding) + section_gap, btn_size, btn_size};
    f32 panel_height = tools_start_y + 4 * (btn_size + btn_padding) + section_gap - panel_y + panel_padding;
    rectf panel_rect = {panel_x, panel_y, panel_width, panel_height};

    f32 canvas_width = 210.0f * 4.0f;
    f32 canvas_height = 297.0f * 4.0f;

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

        // handle mouse drag
        static b32 middle_dragging = false;
        static vec2f drag_start_mouse;
        static vec2f drag_start_center;

        if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_MIDDLE))
        {
            middle_dragging = true;
            drag_start_mouse = win->mouse_pos;
            drag_start_center = view.center;
        }
        if (GFX_IS_MOUSE_JUST_UP(win, GFX_MB_MIDDLE))
        {
            middle_dragging = false;
        }

        if (middle_dragging)
        {
            vec2f delta = vec2f_sub(win->mouse_pos, drag_start_mouse);
            delta.x *= view.width / win->width;
            delta.y *= (view.width / view.aspect_ratio) / win->height;

            view.center = vec2f_sub(drag_start_center, delta);
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
                else if (ua->type == UNDO_IMAGE_ADD && num_images > 0)
                {
                    glDeleteTextures(1, &images[num_images - 1].texture_id);
                    images[num_images - 1].active = false;
                    num_images--;
                }
                else if (ua->type == UNDO_IMAGE_REMOVE)
                {
                    images[num_images] = ua->image_backup;
                    images[num_images].active = true;
                    num_images++;
                }
            }
        }

#ifdef PLATFORM_WIN32
        if (GFX_IS_KEY_DOWN(win, GFX_KEY_LCONTROL) && GFX_IS_KEY_JUST_DOWN(win, GFX_KEY_V))
        {
            printf("Pasting image from clipboard...\n");
            int img_w = 0, img_h = 0;
            u32 tex = create_texture_from_clipboard(NULL, &img_w, &img_h);
            printf("Pasted image size: %d x %d\n", img_w, img_h);
            if (tex != 0)
            {
                f32 scale = 1.0f;
                if (img_w > 400 || img_h > 400)
                {
                    scale = 400.0f / (img_w > img_h ? img_w : img_h);
                }
                images[num_images] = (pasted_image){
                    .texture_id = tex,
                    .x = mouse_pos.x,
                    .y = mouse_pos.y,
                    .width = img_w * scale,
                    .height = img_h * scale,
                    .active = true};
                num_images++;
                selected_image = num_images - 1;
                undo_stack[undo_count++] = (undo_action){UNDO_IMAGE_ADD, 0, NULL, num_images - 1, {0}};
            }
        }
#endif

        b32 click_on_ui = false;
        vec2f screen_mouse_pos = win->mouse_pos;

        if (GFX_IS_MOUSE_DOWN(win, GFX_MB_LEFT) || GFX_IS_MOUSE_JUST_UP(win, GFX_MB_LEFT))
        {
            for (int i = 0; i < NUM_COLORS; i++)
            {
                if (vec2f_in_rectf(screen_mouse_pos, color_buttons[i]))
                {
                    click_on_ui = true;
                    if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
                    {
                        color_idx = i;
                        current_color = colors[i];
                        eraser_mode = false;
                    }
                    break;
                }
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, eraser_button))
            {
                click_on_ui = true;
                if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
                {
                    eraser_mode = true;
                }
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, size_up_button))
            {
                click_on_ui = true;
                if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
                {
                    brush_size += 2.0f;
                    if (brush_size > 50.0f)
                        brush_size = 50.0f;
                    eraser_size += 5.0f;
                    if (eraser_size > 100.0f)
                        eraser_size = 100.0f;
                }
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, size_down_button))
            {
                click_on_ui = true;
                if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
                {
                    brush_size -= 2.0f;
                    if (brush_size < 1.0f)
                        brush_size = 1.0f;
                    eraser_size -= 5.0f;
                    if (eraser_size < 5.0f)
                        eraser_size = 5.0f;
                }
            }
            if (!click_on_ui && vec2f_in_rectf(screen_mouse_pos, export_button))
            {
                click_on_ui = true;
                if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT))
                {
                    int export_w = (int)canvas_width * 2;
                    int export_h = (int)canvas_height * 2;

                    u32 fbo, fbo_tex;
                    glGenFramebuffers(1, &fbo);
                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

                    glGenTextures(1, &fbo_tex);
                    glBindTexture(GL_TEXTURE_2D, fbo_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, export_w, export_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);

                    glViewport(0, 0, export_w, export_h);
                    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    viewf export_view = {
                        .center = {0, 0},
                        .aspect_ratio = canvas_width / canvas_height,
                        .width = canvas_width,
                        .rotation = 0.0f};
                    mat3f export_view_mat = {0};
                    mat3f_from_view(&export_view_mat, export_view);

                    for (u32 img_i = 0; img_i < num_images; img_i++)
                    {
                        if (!images[img_i].active)
                            continue;
                        pasted_image *img = &images[img_i];
                        glUseProgram(texture_program);
                        glUniformMatrix3fv(texture_view_mat_loc, 1, GL_FALSE, export_view_mat.m);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, img->texture_id);
                        glUniform1i(texture_sampler_loc, 0);

                        f32 hw = img->width / 2;
                        f32 hh = img->height / 2;
                        f32 tex_verts[] = {
                            img->x - hw, img->y + hh, 0.0f, 0.0f,
                            img->x - hw, img->y - hh, 0.0f, 1.0f,
                            img->x + hw, img->y - hh, 1.0f, 1.0f,
                            img->x + hw, img->y + hh, 1.0f, 0.0f};

                        glBindVertexArray(vertex_array);
                        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tex_verts), tex_verts);
                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), NULL);
                        glEnableVertexAttribArray(1);
                        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void *)(2 * sizeof(f32)));
                        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                        glDisableVertexAttribArray(1);
                    }

                    for (u32 ln = 0; ln < num_lines; ln++)
                    {
                        draw_lines_draw(lines[ln], shaders, win, export_view);
                    }

                    unsigned char *pixels = (unsigned char *)malloc(export_w * export_h * 4);
                    glReadPixels(0, 0, export_w, export_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                    stbi_flip_vertically_on_write(1);
                    stbi_write_png("export.png", export_w, export_h, 4, pixels, export_w * 4);

                    free(pixels);
                    glDeleteFramebuffers(1, &fbo);
                    glDeleteTextures(1, &fbo_tex);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, win->width, win->height);
                    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                }
            }
        }

        if (GFX_IS_MOUSE_JUST_DOWN(win, GFX_MB_LEFT) && !click_on_ui)
        {
            selected_image = -1;
            for (i32 img_i = num_images - 1; img_i >= 0; img_i--)
            {
                if (!images[img_i].active)
                    continue;
                pasted_image *img = &images[img_i];
                f32 hw = img->width / 2;
                f32 hh = img->height / 2;
                if (mouse_pos.x >= img->x - hw && mouse_pos.x <= img->x + hw &&
                    mouse_pos.y >= img->y - hh && mouse_pos.y <= img->y + hh)
                {
                    selected_image = img_i;
                    dragging_image = true;
                    image_drag_offset = (vec2f){mouse_pos.x - img->x, mouse_pos.y - img->y};
                    click_on_ui = true;
                    break;
                }
            }
        }

        if (dragging_image && selected_image >= 0)
        {
            if (GFX_IS_MOUSE_DOWN(win, GFX_MB_LEFT))
            {
                images[selected_image].x = mouse_pos.x - image_drag_offset.x;
                images[selected_image].y = mouse_pos.y - image_drag_offset.y;
                click_on_ui = true;
            }
            else
            {
                dragging_image = false;
            }
        }

        if (GFX_IS_KEY_JUST_DOWN(win, GFX_KEY_DELETE) && selected_image >= 0)
        {
            undo_stack[undo_count++] = (undo_action){UNDO_IMAGE_REMOVE, 0, NULL, selected_image, images[selected_image]};
            glDeleteTextures(1, &images[selected_image].texture_id);
            images[selected_image].active = false;
            for (i32 j = selected_image; j < (i32)num_images - 1; j++)
            {
                images[j] = images[j + 1];
            }
            num_images--;
            selected_image = -1;
        }

        erase = eraser_mode;

        if (click_on_ui)
        {
            prev_point = mouse_pos;
            prev_prev_point = mouse_pos;
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

            glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);

            glBindVertexArray(vertex_array);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

            vec2f canvas_verts[] = {
                {-canvas_width / 2, canvas_height / 2},
                {-canvas_width / 2, -canvas_height / 2},
                {canvas_width / 2, -canvas_height / 2},
                {canvas_width / 2, canvas_height / 2}};

            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(canvas_verts), canvas_verts);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

            glDisableVertexAttribArray(0);
        }

        for (u32 img_i = 0; img_i < num_images; img_i++)
        {
            if (!images[img_i].active)
                continue;
            pasted_image *img = &images[img_i];
            glUseProgram(texture_program);
            glUniformMatrix3fv(texture_view_mat_loc, 1, GL_FALSE, view_mat.m);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, img->texture_id);
            glUniform1i(texture_sampler_loc, 0);

            f32 hw = img->width / 2;
            f32 hh = img->height / 2;
            f32 tex_verts[] = {
                img->x - hw, img->y + hh, 0.0f, 0.0f,
                img->x - hw, img->y - hh, 0.0f, 1.0f,
                img->x + hw, img->y - hh, 1.0f, 1.0f,
                img->x + hw, img->y + hh, 1.0f, 0.0f};

            glBindVertexArray(vertex_array);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tex_verts), tex_verts);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), NULL);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void *)(2 * sizeof(f32)));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            glDisableVertexAttribArray(1);

            if (selected_image == (i32)img_i)
            {
                glUseProgram(basic_program);
                glUniformMatrix3fv(basic_view_mat_loc, 1, GL_FALSE, view_mat.m);
                glUniform4f(basic_col_loc, 0.2f, 0.6f, 1.0f, 1.0f);

                f32 border = 3.0f;
                vec2f top[] = {{img->x - hw - border, img->y + hh + border}, {img->x - hw - border, img->y + hh}, {img->x + hw + border, img->y + hh}, {img->x + hw + border, img->y + hh + border}};
                vec2f bottom[] = {{img->x - hw - border, img->y - hh}, {img->x - hw - border, img->y - hh - border}, {img->x + hw + border, img->y - hh - border}, {img->x + hw + border, img->y - hh}};
                vec2f left[] = {{img->x - hw - border, img->y + hh}, {img->x - hw - border, img->y - hh}, {img->x - hw, img->y - hh}, {img->x - hw, img->y + hh}};
                vec2f right[] = {{img->x + hw, img->y + hh}, {img->x + hw, img->y - hh}, {img->x + hw + border, img->y - hh}, {img->x + hw + border, img->y + hh}};

                glBindVertexArray(vertex_array);
                glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);

                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(top), top);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bottom), bottom);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(left), left);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(right), right);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }
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
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2f), NULL);

            glUniform4f(basic_col_loc, 0.12f, 0.12f, 0.14f, 0.95f);
            {
                rectf r = panel_rect;
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + r.h},
                    {r.x + r.w, r.y + r.h},
                    {r.x + r.w, r.y}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            glUniform4f(basic_col_loc, 0.22f, 0.22f, 0.25f, 1.0f);
            {
                rectf r = {panel_rect.x + 1, panel_rect.y + 1, panel_rect.w - 2, panel_rect.h - 2};
                vec2f verts[] = {
                    {r.x, r.y},
                    {r.x, r.y + 1},
                    {r.x + r.w, r.y + 1},
                    {r.x + r.w, r.y}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            for (int i = 0; i < NUM_COLORS; i++)
            {
                vec4f col = colors[i];
                rectf r = color_buttons[i];
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                f32 radius = r.w / 2;
                int segments = 24;

                if (!eraser_mode && i == color_idx)
                {
                    glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 0.9f);
                    for (int seg = 0; seg < segments; seg++)
                    {
                        f32 angle1 = (f32)seg / segments * 6.28318f;
                        f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                        vec2f p0 = {cx, cy};
                        vec2f p1 = {cx + cosf(angle1) * (radius + 3), cy + sinf(angle1) * (radius + 3)};
                        vec2f p2 = {cx + cosf(angle2) * (radius + 3), cy + sinf(angle2) * (radius + 3)};
                        vec2f tri[] = {p0, p1, p2, p0};
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                        glDrawArrays(GL_TRIANGLES, 0, 3);
                    }
                }

                glUniform4f(basic_col_loc, col.x, col.y, col.z, col.w);
                for (int seg = 0; seg < segments; seg++)
                {
                    f32 angle1 = (f32)seg / segments * 6.28318f;
                    f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                    vec2f p0 = {cx, cy};
                    vec2f p1 = {cx + cosf(angle1) * radius, cy + sinf(angle1) * radius};
                    vec2f p2 = {cx + cosf(angle2) * radius, cy + sinf(angle2) * radius};
                    vec2f tri[] = {p0, p1, p2, p0};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }
            }

            {
                rectf r = eraser_button;
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                f32 radius = r.w / 2 - 2;
                int segments = 24;

                if (eraser_mode)
                {
                    glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 0.9f);
                    for (int seg = 0; seg < segments; seg++)
                    {
                        f32 angle1 = (f32)seg / segments * 6.28318f;
                        f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                        vec2f p0 = {cx, cy};
                        vec2f p1 = {cx + cosf(angle1) * (radius + 4), cy + sinf(angle1) * (radius + 4)};
                        vec2f p2 = {cx + cosf(angle2) * (radius + 4), cy + sinf(angle2) * (radius + 4)};
                        vec2f tri[] = {p0, p1, p2, p0};
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                        glDrawArrays(GL_TRIANGLES, 0, 3);
                    }
                }

                glUniform4f(basic_col_loc, 0.28f, 0.28f, 0.32f, 1.0f);
                for (int seg = 0; seg < segments; seg++)
                {
                    f32 angle1 = (f32)seg / segments * 6.28318f;
                    f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                    vec2f p0 = {cx, cy};
                    vec2f p1 = {cx + cosf(angle1) * radius, cy + sinf(angle1) * radius};
                    vec2f p2 = {cx + cosf(angle2) * radius, cy + sinf(angle2) * radius};
                    vec2f tri[] = {p0, p1, p2, p0};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }

                glUniform4f(basic_col_loc, 0.85f, 0.85f, 0.85f, 1.0f);
                vec2f eraser_icon[] = {
                    {cx - 8, cy + 4},
                    {cx - 8, cy - 4},
                    {cx + 4, cy - 4},
                    {cx + 4, cy + 4}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(eraser_icon), eraser_icon);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                glUniform4f(basic_col_loc, 0.6f, 0.6f, 0.65f, 1.0f);
                vec2f eraser_top[] = {
                    {cx + 4, cy + 4},
                    {cx + 4, cy - 4},
                    {cx + 8, cy - 4},
                    {cx + 8, cy + 4}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(eraser_top), eraser_top);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            {
                rectf r = size_up_button;
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                f32 radius = r.w / 2 - 2;
                int segments = 24;

                glUniform4f(basic_col_loc, 0.28f, 0.28f, 0.32f, 1.0f);
                for (int seg = 0; seg < segments; seg++)
                {
                    f32 angle1 = (f32)seg / segments * 6.28318f;
                    f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                    vec2f p0 = {cx, cy};
                    vec2f p1 = {cx + cosf(angle1) * radius, cy + sinf(angle1) * radius};
                    vec2f p2 = {cx + cosf(angle2) * radius, cy + sinf(angle2) * radius};
                    vec2f tri[] = {p0, p1, p2, p0};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }

                glUniform4f(basic_col_loc, 0.85f, 0.85f, 0.85f, 1.0f);
                vec2f plus_h[] = {{cx - 7, cy - 1.5f}, {cx - 7, cy + 1.5f}, {cx + 7, cy + 1.5f}, {cx + 7, cy - 1.5f}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(plus_h), plus_h);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
                vec2f plus_v[] = {{cx - 1.5f, cy - 7}, {cx - 1.5f, cy + 7}, {cx + 1.5f, cy + 7}, {cx + 1.5f, cy - 7}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(plus_v), plus_v);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            {
                rectf r = size_down_button;
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                f32 radius = r.w / 2 - 2;
                int segments = 24;

                glUniform4f(basic_col_loc, 0.28f, 0.28f, 0.32f, 1.0f);
                for (int seg = 0; seg < segments; seg++)
                {
                    f32 angle1 = (f32)seg / segments * 6.28318f;
                    f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                    vec2f p0 = {cx, cy};
                    vec2f p1 = {cx + cosf(angle1) * radius, cy + sinf(angle1) * radius};
                    vec2f p2 = {cx + cosf(angle2) * radius, cy + sinf(angle2) * radius};
                    vec2f tri[] = {p0, p1, p2, p0};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }

                glUniform4f(basic_col_loc, 0.85f, 0.85f, 0.85f, 1.0f);
                vec2f minus_h[] = {{cx - 7, cy - 1.5f}, {cx - 7, cy + 1.5f}, {cx + 7, cy + 1.5f}, {cx + 7, cy - 1.5f}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(minus_h), minus_h);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
            }

            {
                rectf r = export_button;
                f32 cx = r.x + r.w / 2;
                f32 cy = r.y + r.h / 2;
                f32 radius = r.w / 2 - 2;
                int segments = 24;

                glUniform4f(basic_col_loc, 0.20f, 0.55f, 0.35f, 1.0f);
                for (int seg = 0; seg < segments; seg++)
                {
                    f32 angle1 = (f32)seg / segments * 6.28318f;
                    f32 angle2 = (f32)(seg + 1) / segments * 6.28318f;
                    vec2f p0 = {cx, cy};
                    vec2f p1 = {cx + cosf(angle1) * radius, cy + sinf(angle1) * radius};
                    vec2f p2 = {cx + cosf(angle2) * radius, cy + sinf(angle2) * radius};
                    vec2f tri[] = {p0, p1, p2, p0};
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }

                glUniform4f(basic_col_loc, 1.0f, 1.0f, 1.0f, 1.0f);
                vec2f arrow_body[] = {{cx - 1.5f, cy - 6}, {cx - 1.5f, cy + 2}, {cx + 1.5f, cy + 2}, {cx + 1.5f, cy - 6}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(arrow_body), arrow_body);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

                vec2f arrow_head[] = {{cx, cy + 7}, {cx - 5, cy + 1}, {cx + 5, cy + 1}, {cx, cy + 7}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(arrow_head), arrow_head);
                glDrawArrays(GL_TRIANGLES, 0, 3);

                vec2f base_line[] = {{cx - 8, cy - 8}, {cx - 8, cy - 6}, {cx + 8, cy - 6}, {cx + 8, cy - 8}};
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(base_line), base_line);
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

    for (u32 i = 0; i < num_images; i++)
    {
        if (images[i].active)
        {
            glDeleteTextures(1, &images[i].texture_id);
        }
    }

    draw_lines_shaders_destroy(shaders);
    draw_point_alloc_destroy(point_allocator);

    glDeleteBuffers(1, &vertex_buffer);
    glDeleteBuffers(1, &index_buffer);
    glDeleteVertexArrays(1, &vertex_array);

    glDeleteProgram(basic_program);
    glDeleteProgram(texture_program);

    gfx_win_destroy(win);

    mga_destroy(perm_arena);

    return 0;
}
