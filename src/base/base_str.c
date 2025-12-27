#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "base_defs.h"
#include "base_str.h"

string8 str8_from_range(u8* start, u8* end) {
    if (start == NULL || end == NULL) {
        fprintf(stderr, "Cannot creates string8 from NULL ptr\n");
    }

    return (string8){ (u64)(end - start), start };
}
string8 str8_from_cstr(u8* cstr) {
    if (cstr == NULL) {
        fprintf(stderr, "Cannot create string8 from NULL ptr\n");
    }

    u8* ptr = cstr;
    for(; *ptr != 0; ptr += 1);
    return str8_from_range(cstr, ptr);
}

string8 str8_copy(mg_arena* arena, string8 str) {
    string8 out = { 
        .str = MGA_PUSH_ARRAY(arena, u8, str.size),
        .size = str.size
    };

    memcpy(out.str, str.str, str.size);
    
    return out;
}
u8* str8_to_cstr(mg_arena* arena, string8 str) {
    u8* out = MGA_PUSH_ARRAY(arena, u8, str.size + 1);
    
    memcpy(out, str.str, str.size);
    out[str.size] = '\0';

    return out;
}

b32 str8_equals(string8 a, string8 b) {
    if (a.size != b.size)
        return false;

    for (u64 i = 0; i < a.size; i++)  {
        if (a.str[i] != b.str[i])
            return false;
    }
    
    return true;
}
b32 str8_contains(string8 str, string8 sub) {
    for (u64 i = 0; i < str.size - sub.size + 1; i++) {
        b32 contains = true;
        for (u64 j = 0; j < sub.size; j++) {
            if (str.str[i + j] != sub.str[j]) {
                contains = false;
                break;
            }
        }

        if (contains) {
            return true;
        }
    }

    return false;
}
b32 str8_contains_char(string8 str, u8 c) {
    for (u64 i = 0; i < str.size; i++) {
        if (str.str[i] == c)
            return true;
    }

    return false;
}

b32 str8_index_of(string8 str, string8 sub, u64* index) {
    if (index == NULL) {
        fprintf(stderr, "Cannot put index of string8 into NULL ptr\n");
    }

    for (u64 i = 0; i < str.size; i++) {
        if (str8_equals(str8_substr(str, i, i + sub.size), sub)) {
            *index = i;

            return true;
        }
    }

    return false;
}

b32 str8_index_of_char(string8 str, u8 c, u64* index) {
    if (index == NULL) {
        fprintf(stderr, "Cannot put index of string8 into NULL ptr\n");
    }

    for (u64 i = 0; i < str.size; i++) {
        if (str.str[i] == c) {
            *index = i;
            return true;
        }
    }

    return false;
}

string8 str8_substr(string8 str, u64 start, u64 end) {
    u64 end_clamped = MIN(str.size, end);
    u64 start_clamped = MIN(start, end_clamped);
    return (string8){ end_clamped - start_clamped, str.str + start_clamped };
}
string8 str8_substr_size(string8 str, u64 start, u64 size) {
    return str8_substr(str, start, start + size);
}

string8 str8_remove_space(mg_arena* arena, string8 str) {
    mga_temp scratch = mga_scratch_get(&arena, 1);

    string8 stripped = {
        .size = str.size,
        .str = MGA_PUSH_ZERO_ARRAY(scratch.arena, u8, str.size)
    };

    u64 s_i = 0;
    for (u64 i = 0; i < str.size; i++) {
        u8 c = str.str[i];
        
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            stripped.str[s_i++] = str.str[i];
        } else {
            stripped.size--;
        }
    }

    string8 out = str8_copy(arena, stripped);

    mga_scratch_release(scratch);

    return out;
}

void str8_list_push_existing(string8_list* list, string8 str, string8_node* node) {
    if (list == NULL || node == NULL) {
        fprintf(stderr, "Cannot push node to string list: list or node is NULL\n");

        return;
    }

    node->str = str;
    SLL_PUSH_BACK(list->first, list->last, node);
    list->node_count++;
    list->total_size += str.size;
}
void str8_list_push(mg_arena* arena, string8_list* list, string8 str) {
    if (list == NULL) {
        fprintf(stderr, "Cannot push string to list: list is NULL\n");

        return;
    }

    string8_node* node = MGA_PUSH_ZERO_STRUCT(arena, string8_node);
    str8_list_push_existing(list, str, node);
}

string8 str8_concat(mg_arena* arena, string8_list list) {
    string8 out = {
        .str = MGA_PUSH_ZERO_ARRAY(arena, u8, list.total_size),
        .size = list.total_size
    };

    u8* ptr = out.str;

    for (string8_node* node = list.first; node != NULL; node = node->next) {
        memcpy(ptr, node->str.str, node->str.size);
        ptr += node->str.size;
    }

    return out;
}

string8 str8_pushfv(mg_arena* arena, const char* fmt, va_list args) {
    va_list args2;
    va_copy(args2, args);

    u64 init_size = 1024;
    u8* buffer = MGA_PUSH_ARRAY(arena, u8, init_size);
    u64 size = vsnprintf((char*)buffer, init_size, fmt, args);

    string8 out = { 0 };
    if (size < init_size) {
        mga_pop(arena, init_size - size - 1);
        out = (string8){ size, buffer };
    } else {
        // NOTE: This path may not work
        mga_pop(arena, init_size);
        u8* fixed_buff = MGA_PUSH_ARRAY(arena, u8, size + 1);
        u64 final_size = vsnprintf((char*)fixed_buff, size + 1, fmt, args);
        out = (string8){ final_size, fixed_buff };
    }

    va_end(args2);

    return out;
}

string8 str8_pushf(mg_arena* arena, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    string8 out = str8_pushfv(arena, fmt, args);

    va_end(args);

    return out;
}

