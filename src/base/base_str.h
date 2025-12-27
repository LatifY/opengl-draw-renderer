/**
 * @file str.h
 * @brief Length based strings and string handling functions (based on Mr 4th's length based strings)
 *
 * This is heavily based on the string header in the [Mr 4th programming series](https://www.youtube.com/@Mr4thProgramming)
 */

#ifndef STR_H
#define STR_H

#include <stdarg.h>

#include "base_defs.h"
#include "mg/mg_arena.h"

typedef struct {
    u64 size;
    u8* str;
} string8;

typedef struct string8_node {
    struct string8_node* next;
    string8 str;
} string8_node;

typedef struct {
    string8_node* first;
    string8_node* last;

    u64 node_count;
    u64 total_size;
} string8_list; 

#define STR8(s) ((string8){ sizeof(s)-1, (u8*)s })

string8 str8_from_range(u8* start, u8* end);
string8 str8_from_cstr(u8* cstr);

string8 str8_copy(mg_arena* arena, string8 str);
u8* str8_to_cstr(mg_arena* arena, string8 str);

b32 str8_equals(string8 a, string8 b);
b32 str8_contains(string8 str, string8 sub);
b32 str8_contains_char(string8 str, u8 c);

b32 str8_index_of(string8 str, string8 sub, u64* index);
b32 str8_index_of_char(string8 str, u8 c, u64* index);

string8 str8_substr(string8 str, u64 start, u64 end);
string8 str8_substr_size(string8 str, u64 start, u64 size);

string8 str8_remove_space(mg_arena* arena, string8 str);

void str8_list_push_existing(string8_list* list, string8 str, string8_node* node);
void str8_list_push(mg_arena* arena, string8_list* list, string8 str);

string8 str8_concat(mg_arena* arena, string8_list list);

string8 str8_pushfv(mg_arena* arena, const char* fmt, va_list args);
string8 str8_pushf(mg_arena* arena, const char* fmt, ...);

#endif // BASE_STR_H

