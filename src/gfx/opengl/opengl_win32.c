#include "base/base_defs.h"

#ifdef PLATFORM_WIN32

#include "gfx/gfx.h"
#include "opengl.h"

#include <stdio.h>

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL/gl.h>

// TODO: error checking

typedef struct _gfx_win_backend {
    HINSTANCE instance;
    WNDCLASS win_class;
    HWND window;
    HDC device_context;
    HGLRC gl_context;
} _gfx_win_backend;

typedef struct {
    u16* str;
    u64 size;
} _string16;

static _string16 _utf16_from_utf8(mg_arena* arena, string8 str);

#define X(ret, name, args) gl_##name##_func name = NULL;
#   include "opengl_funcs.h"
#undef X

static HMODULE w32_opengl_module = NULL;

static void* w32_gl_load(const char* name);

#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002

#define WGL_SWAP_METHOD_ARB                       0x2007
#define WGL_SWAP_EXCHANGE_ARB                     0x2028
#define WGL_DRAW_TO_WINDOW_ARB                    0x2001
#define WGL_ACCELERATION_ARB                      0x2003
#define WGL_SUPPORT_OPENGL_ARB                    0x2010
#define WGL_DOUBLE_BUFFER_ARB                     0x2011
#define WGL_PIXEL_TYPE_ARB                        0x2013
#define WGL_COLOR_BITS_ARB                        0x2014
#define WGL_DEPTH_BITS_ARB                        0x2022
#define WGL_STENCIL_BITS_ARB                      0x2023
#define WGL_FULL_ACCELERATION_ARB                 0x2027
#define WGL_TYPE_RGBA_ARB                         0x202B

typedef BOOL (WINAPI *wglChoosePixelFormatARB_func)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef HGLRC (WINAPI *wglCreateContextAttribsARB_func)(HDC hdc, HGLRC hShareContext, const int *attribList);

static LRESULT CALLBACK w32_window_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static u32 w32_keymap[256] = { 0 };
static void w32_init_keymap(void);

gfx_window* gfx_win_create(mg_arena* arena, u32 width, u32 height, string8 title) {
    gfx_window* win = MGA_PUSH_ZERO_STRUCT(arena, gfx_window);

    *win = (gfx_window){ 
        .title = title,
        .width = width,
        .height = height,
        .backend = MGA_PUSH_ZERO_STRUCT(arena, _gfx_win_backend)
    };

    win->backend->instance = GetModuleHandle(0);

    if (w32_opengl_module == NULL) {
        w32_opengl_module = LoadLibraryW(L"opengl32.dll");
    }

    wglChoosePixelFormatARB_func wglChoosePixelFormatARB = NULL;
    wglCreateContextAttribsARB_func wglCreateContextAttribsARB = NULL;

    // Bootstrap window
    {
        WNDCLASS bootstrap_wnd_class = (WNDCLASSW){
            .hInstance = win->backend->instance,
            .lpfnWndProc = DefWindowProc,
            .lpszClassName = L"Bootstrap Window Class"
        };
        RegisterClassW(&bootstrap_wnd_class);

        HWND bootstrap_window = CreateWindowW(
            bootstrap_wnd_class.lpszClassName,
            NULL, 0,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            0, 0, win->backend->instance, 0
        );

        HDC bootstrap_dc = GetDC(bootstrap_window);

        PIXELFORMATDESCRIPTOR pfd = (PIXELFORMATDESCRIPTOR){
            .nSize = sizeof(PIXELFORMATDESCRIPTOR),
            .nVersion = 1,
            .dwFlags = PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW,
            .iPixelType = PFD_TYPE_RGBA,
            .cColorBits = 24,
            .iLayerType = PFD_MAIN_PLANE
        };

        i32 pixel_format = ChoosePixelFormat(bootstrap_dc, &pfd);
        SetPixelFormat(bootstrap_dc, pixel_format, &pfd);

        HGLRC bootstrap_context = wglCreateContext(bootstrap_dc);
        wglMakeCurrent(bootstrap_dc, bootstrap_context);

        wglChoosePixelFormatARB = (wglChoosePixelFormatARB_func)w32_gl_load("wglChoosePixelFormatARB");
        wglCreateContextAttribsARB = (wglCreateContextAttribsARB_func)w32_gl_load("wglCreateContextAttribsARB");

        wglMakeCurrent(bootstrap_dc, NULL);
        wglDeleteContext(bootstrap_context);
        ReleaseDC(bootstrap_window, bootstrap_dc);
        UnregisterClassW(bootstrap_wnd_class.lpszClassName, win->backend->instance);
        DestroyWindow(bootstrap_window);
    }

    win->backend->win_class = (WNDCLASSW){
        .hInstance = win->backend->instance,
        .lpfnWndProc = w32_window_proc,
        .lpszClassName = L"Magicalbat Window Class",
        .hCursor = LoadCursor(NULL, IDC_ARROW)
    };
    RegisterClassW(&win->backend->win_class);

    RECT win_rect = { 0, 0, width, height };
    AdjustWindowRect(&win_rect, WS_OVERLAPPEDWINDOW, FALSE);
    
    mga_temp scratch = mga_scratch_get(NULL, 0);
    _string16 title16 = _utf16_from_utf8(scratch.arena, title);

    win->backend->window = CreateWindowW(
        win->backend->win_class.lpszClassName,
        title16.str,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        win_rect.right - win_rect.left, win_rect.bottom - win_rect.top,
        NULL, NULL, win->backend->instance, NULL
    );

    mga_scratch_release(scratch);

    SetPropW(win->backend->window, L"gfx_win", win);

    win->backend->device_context = GetDC(win->backend->window);

    i32 pixel_format_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_SWAP_METHOD_ARB,    WGL_SWAP_EXCHANGE_ARB,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     24,
        WGL_DEPTH_BITS_ARB,     0,
        WGL_STENCIL_BITS_ARB,   0,
        0
    };

    i32 pixel_format = 0;
    u32 num_formats = 0;
    wglChoosePixelFormatARB(win->backend->device_context, pixel_format_attribs, NULL, 1, &pixel_format, &num_formats);

    PIXELFORMATDESCRIPTOR pfd = { 0 };
    DescribePixelFormat(win->backend->device_context, pixel_format, sizeof(pfd), &pfd);
    SetPixelFormat(win->backend->device_context, pixel_format, &pfd);

    i32 context_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    win->backend->gl_context = wglCreateContextAttribsARB(win->backend->device_context, NULL, context_attribs);

    wglMakeCurrent(win->backend->device_context, win->backend->gl_context);

    #define X(ret, name, args) name = (gl_##name##_func)w32_gl_load(#name);
    #   include "opengl_funcs.h"
    #undef X

    if (w32_opengl_module != NULL) {
        FreeLibrary(w32_opengl_module);
    }

    ShowWindow(win->backend->window, SW_SHOW);
    glViewport(0, 0, win->width, win->height);

    w32_init_keymap();

    return win;
}
void gfx_win_destroy(gfx_window* win) {
    wglMakeCurrent(win->backend->device_context, NULL);
    wglDeleteContext(win->backend->gl_context);
    ReleaseDC(win->backend->window, win->backend->device_context);
    UnregisterClassW(win->backend->win_class.lpszClassName, win->backend->instance);
    DestroyWindow(win->backend->window);
}

void gfx_win_process_events(gfx_window* win) {
    memcpy(win->prev_mouse_buttons, win->mouse_buttons, GFX_NUM_MOUSE_BUTTONS);
    memcpy(win->prev_keys, win->keys, GFX_NUM_KEYS);
    win->mouse_scroll = 0;

    MSG msg = { 0 };
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void gfx_win_make_current(gfx_window* win) {
    wglMakeCurrent(win->backend->device_context, win->backend->gl_context);
}
void gfx_win_clear(gfx_window* win) {
    UNUSED(win);

    glClear(GL_COLOR_BUFFER_BIT);
}
void gfx_win_swap_buffers(gfx_window* win) {
    SwapBuffers(win->backend->device_context);
}

static LRESULT CALLBACK w32_window_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    gfx_window* win = GetPropW(hWnd, L"gfx_win");
    
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            win->mouse_pos.x = (f32)((lParam) & 0xffff);
            win->mouse_pos.y = (f32)((lParam >> 16) & 0xffff);
        } break;

        case WM_LBUTTONDOWN: {
            win->mouse_buttons[GFX_MB_LEFT] = true;
        } break;
        case WM_LBUTTONUP: {
            win->mouse_buttons[GFX_MB_LEFT] = false;
        } break;
        case WM_MBUTTONDOWN: {
            win->mouse_buttons[GFX_MB_MIDDLE] = true;
        } break;
        case WM_MBUTTONUP: {
            win->mouse_buttons[GFX_MB_MIDDLE] = false;
        } break;
        case WM_RBUTTONDOWN: {
            win->mouse_buttons[GFX_MB_RIGHT] = true;
        } break;
        case WM_RBUTTONUP: {
            win->mouse_buttons[GFX_MB_RIGHT] = false;
        } break;

        case WM_MOUSEWHEEL: {
            win->mouse_scroll = SIGN(GET_WHEEL_DELTA_WPARAM(wParam));
        } break;

        case WM_KEYDOWN: {
            gfx_key down_key = w32_keymap[wParam];
            win->keys[down_key] = true;
        } break;
        case WM_KEYUP: {
            gfx_key up_key = w32_keymap[wParam];
            win->keys[up_key] = false;
        } break;

        case WM_SIZE: {
            u32 width = (u32)LOWORD(lParam);
            u32 height = (u32)HIWORD(lParam);

            win->width = width;
            win->height = height;

            glViewport(0, 0, width, height);
        } break;

        case WM_CLOSE: {
            win->should_close = true;
        } break;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static _string16 _utf16_from_utf8(mg_arena* arena, string8 str) {
    mga_temp scratch = mga_scratch_get(&arena, 1);

    u64 tmp_size = str.size * 2 + 1;
    u16* tmp_out = MGA_PUSH_ZERO_ARRAY(scratch.arena, u16, tmp_size);

    i32 size_written = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, (LPCCH)str.str, str.size, tmp_out, tmp_size);

    if (size_written == 0) {
        fprintf(stderr, "Failed to convert utf8 to utf16 for win32\n");

        mga_scratch_release(scratch);
        return (_string16){ 0 };
    }

    // +1 for null terminator
    u16* out = MGA_PUSH_ZERO_ARRAY(scratch.arena, u16, size_written + 1);
    memcpy(out, tmp_out, sizeof(u16) * size_written);

    mga_scratch_release(scratch);

    return (_string16){ .str = out, .size = size_written };
}

// https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions
static void* w32_gl_load(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == 0 || (p == (void*)0x1) || (p == (void*)0x2) || (p == (void*)0x3) || (p == (void*)-1)) {
        p = (void*)GetProcAddress(w32_opengl_module, name);
    }
    return p;
}

static void w32_init_keymap(void) {
    w32_keymap[VK_BACK] = GFX_KEY_BACKSPACE;
    w32_keymap[VK_TAB] = GFX_KEY_TAB;
    w32_keymap[VK_RETURN] = GFX_KEY_ENTER;
    w32_keymap[VK_CAPITAL] = GFX_KEY_CAPSLOCK;
    w32_keymap[VK_ESCAPE] = GFX_KEY_ESCAPE;
    w32_keymap[VK_SPACE] = GFX_KEY_SPACE;
    w32_keymap[VK_PRIOR] = GFX_KEY_PAGEUP;
    w32_keymap[VK_NEXT] = GFX_KEY_PAGEDOWN;
    w32_keymap[VK_END] = GFX_KEY_END;
    w32_keymap[VK_HOME] = GFX_KEY_HOME;
    w32_keymap[VK_LEFT] = GFX_KEY_LEFT;
    w32_keymap[VK_UP] = GFX_KEY_UP;
    w32_keymap[VK_RIGHT] = GFX_KEY_RIGHT;
    w32_keymap[VK_DOWN] = GFX_KEY_DOWN;
    w32_keymap[VK_INSERT] = GFX_KEY_INSERT;
    w32_keymap[VK_DELETE] = GFX_KEY_DELETE;
    w32_keymap[0x30] = GFX_KEY_0;
    w32_keymap[0x31] = GFX_KEY_1;
    w32_keymap[0x32] = GFX_KEY_2;
    w32_keymap[0x33] = GFX_KEY_3;
    w32_keymap[0x34] = GFX_KEY_4;
    w32_keymap[0x35] = GFX_KEY_5;
    w32_keymap[0x36] = GFX_KEY_6;
    w32_keymap[0x37] = GFX_KEY_7;
    w32_keymap[0x38] = GFX_KEY_8;
    w32_keymap[0x39] = GFX_KEY_9;
    w32_keymap[0x41] = GFX_KEY_A;
    w32_keymap[0x42] = GFX_KEY_B;
    w32_keymap[0x43] = GFX_KEY_C;
    w32_keymap[0x44] = GFX_KEY_D;
    w32_keymap[0x45] = GFX_KEY_E;
    w32_keymap[0x46] = GFX_KEY_F;
    w32_keymap[0x47] = GFX_KEY_G;
    w32_keymap[0x48] = GFX_KEY_H;
    w32_keymap[0x49] = GFX_KEY_I;
    w32_keymap[0x4A] = GFX_KEY_J;
    w32_keymap[0x4B] = GFX_KEY_K;
    w32_keymap[0x4C] = GFX_KEY_L;
    w32_keymap[0x4D] = GFX_KEY_M;
    w32_keymap[0x4E] = GFX_KEY_N;
    w32_keymap[0x4F] = GFX_KEY_O;
    w32_keymap[0x50] = GFX_KEY_P;
    w32_keymap[0x51] = GFX_KEY_Q;
    w32_keymap[0x52] = GFX_KEY_R;
    w32_keymap[0x53] = GFX_KEY_S;
    w32_keymap[0x54] = GFX_KEY_T;
    w32_keymap[0x55] = GFX_KEY_U;
    w32_keymap[0x56] = GFX_KEY_V;
    w32_keymap[0x57] = GFX_KEY_W;
    w32_keymap[0x58] = GFX_KEY_X;
    w32_keymap[0x59] = GFX_KEY_Y;
    w32_keymap[0x5A] = GFX_KEY_Z;
    w32_keymap[VK_NUMPAD0] = GFX_KEY_NUMPAD_0;
    w32_keymap[VK_NUMPAD1] = GFX_KEY_NUMPAD_1;
    w32_keymap[VK_NUMPAD2] = GFX_KEY_NUMPAD_2;
    w32_keymap[VK_NUMPAD3] = GFX_KEY_NUMPAD_3;
    w32_keymap[VK_NUMPAD4] = GFX_KEY_NUMPAD_4;
    w32_keymap[VK_NUMPAD5] = GFX_KEY_NUMPAD_5;
    w32_keymap[VK_NUMPAD6] = GFX_KEY_NUMPAD_6;
    w32_keymap[VK_NUMPAD7] = GFX_KEY_NUMPAD_7;
    w32_keymap[VK_NUMPAD8] = GFX_KEY_NUMPAD_8;
    w32_keymap[VK_NUMPAD9] = GFX_KEY_NUMPAD_9;
    w32_keymap[VK_MULTIPLY] = GFX_KEY_NUMPAD_MULTIPLY;
    w32_keymap[VK_ADD] = GFX_KEY_NUMPAD_ADD;
    w32_keymap[VK_SUBTRACT] = GFX_KEY_NUMPAD_SUBTRACT;
    w32_keymap[VK_DECIMAL] = GFX_KEY_NUMPAD_DECIMAL;
    w32_keymap[VK_DIVIDE] = GFX_KEY_NUMPAD_DIVIDE;
    w32_keymap[VK_F1] = GFX_KEY_F1;
    w32_keymap[VK_F2] = GFX_KEY_F2;
    w32_keymap[VK_F3] = GFX_KEY_F3;
    w32_keymap[VK_F4] = GFX_KEY_F4;
    w32_keymap[VK_F5] = GFX_KEY_F5;
    w32_keymap[VK_F6] = GFX_KEY_F6;
    w32_keymap[VK_F7] = GFX_KEY_F7;
    w32_keymap[VK_F8] = GFX_KEY_F8;
    w32_keymap[VK_F9] = GFX_KEY_F9;
    w32_keymap[VK_F10] = GFX_KEY_F10;
    w32_keymap[VK_F11] = GFX_KEY_F11;
    w32_keymap[VK_F12] = GFX_KEY_F12;
    w32_keymap[VK_NUMLOCK] = GFX_KEY_NUM_LOCK;
    w32_keymap[VK_SCROLL] = GFX_KEY_SCROLL_LOCK;
    w32_keymap[VK_LSHIFT] = GFX_KEY_LSHIFT;
    w32_keymap[VK_RSHIFT] = GFX_KEY_RSHIFT;
    w32_keymap[VK_LCONTROL] = GFX_KEY_LCONTROL;
    w32_keymap[VK_RCONTROL] = GFX_KEY_RCONTROL;
    w32_keymap[VK_LMENU] = GFX_KEY_LALT;
    w32_keymap[VK_RMENU] = GFX_KEY_RALT;
    w32_keymap[VK_OEM_1] = GFX_KEY_SEMICOLON;
    w32_keymap[VK_OEM_PLUS] = GFX_KEY_EQUAL;
    w32_keymap[VK_OEM_COMMA] = GFX_KEY_COMMA;
    w32_keymap[VK_OEM_MINUS] = GFX_KEY_MINUS;
    w32_keymap[VK_OEM_PERIOD] = GFX_KEY_PERIOD;
    w32_keymap[VK_OEM_2] = GFX_KEY_FORWARDSLASH;
    w32_keymap[VK_OEM_3] = GFX_KEY_BACKTICK;
    w32_keymap[VK_OEM_4] = GFX_KEY_LBRACKET;
    w32_keymap[VK_OEM_5] = GFX_KEY_BACKSLASH;
    w32_keymap[VK_OEM_6] = GFX_KEY_RBRACKET;
    w32_keymap[VK_OEM_7] = GFX_KEY_APOSTROPHE;
}

#endif // PLATFORM_WIN32
