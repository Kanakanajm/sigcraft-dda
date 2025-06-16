#include "enklume/nbt.h"
#include "support_private.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdalign.h>

#pragma GCC diagnostic error "-Wswitch"

static_assert(sizeof(float) == sizeof(int32_t),  "what kind of platform is this");
static_assert(sizeof(double) == sizeof(int64_t), "what kind of platform is this");

static const char* validate_in_bounds(const char* buf, const char* bound, size_t off) {
    assert(buf + off <= bound);
    return buf + off;
}

#define read(T) (T) enkl_swap_endianness(sizeof(T), *(T*) ((*buffer = validate_in_bounds(*buffer, buffer_end, sizeof(T))) - sizeof(T)) )
#define advance_bytes(b) ((*buffer = validate_in_bounds(*buffer, buffer_end, (b))) - (b))

static NBT_Object* cunk_decode_nbt_impl(const char*, const char*, const char**, Enkl_Allocator*);

static bool cunk_decode_nbt_body(NBT_Tag tag, NBT_Body* out_body, const char* buffer_start, const char* const buffer_end, const char** const buffer, Enkl_Allocator* allocator) {
    NBT_Body body = { 0 };
    switch (tag) {
        case NBT_Tag_Byte:   body.p_byte   = read(int8_t);  break;
        case NBT_Tag_Short:  body.p_short  = read(int16_t); break;
        case NBT_Tag_Int:    body.p_int    = read(int32_t); break;
        case NBT_Tag_Long:   body.p_long   = read(int64_t); break;
        case NBT_Tag_Float:  body.p_float  = read(float);   break;
        case NBT_Tag_Double: body.p_double = read(double);  break;
        case NBT_Tag_ByteArray: {
            int32_t size = body.p_byte_array.count = read(int32_t);
            assert(size >= 0);
            int8_t* data = body.p_byte_array.arr = allocator->allocate_bytes(allocator, sizeof(int8_t) * size, 0);
            memcpy(data, *buffer, sizeof(int8_t) * size);
            advance_bytes(sizeof(int8_t) * size);
            break;
        } case NBT_Tag_String: {
            uint16_t size = read(uint16_t);
            char* str;
            body.p_string = str = allocator->allocate_bytes(allocator, size + 1, 0);
            memcpy(str, *buffer, size * sizeof(int8_t));
            advance_bytes(size * sizeof(int8_t));
            str[size] = '\0';
            break;
        }
        case NBT_Tag_List: {
            NBT_Tag elements_tag = body.p_list.tag = read(uint8_t);
            int32_t elements_count = body.p_list.count = read(int32_t);
            assert(elements_count >= 0);
            NBT_Body* arr = body.p_list.bodies = allocator->allocate_bytes(allocator, sizeof(NBT_Body) * elements_count, 0);
            for (int32_t i = 0; i < elements_count; i++) {
                bool list_item_decoded_ok = cunk_decode_nbt_body(elements_tag, &arr[i], buffer_start, buffer_end, buffer, allocator);
                assert(list_item_decoded_ok);
            }
            break;
        }
        case NBT_Tag_Compound: {
            size_t space = 8;
            body.p_compound.objects = allocator->allocate_bytes(allocator, sizeof(NBT_Object*) * space, alignof(NBT_Object*));
            int32_t size = 0;
            while (true) {
                NBT_Object* o = cunk_decode_nbt_impl(buffer_start, buffer_end, buffer, allocator);
                if (!o)
                    break;
                size++;
                if (size >= space) {
                    body.p_compound.objects = allocator->grow_allocation(allocator, body.p_compound.objects, alignof(NBT_Object*), sizeof(NBT_Object*) * space, sizeof(NBT_Object*) * space * 2);
                    space *= 2;
                }
                body.p_compound.objects[size - 1] = o;
                assert(size - 1 < space);
            }
            body.p_compound.count = size;
            break;
        }
        case NBT_Tag_IntArray: {
            int32_t size = body.p_int_array.count = read(int32_t);
            assert(size >= 0);
            int32_t* data = body.p_int_array.arr = allocator->allocate_bytes(allocator, sizeof(int32_t) * size, 0);
            memcpy(data, *buffer, sizeof(int32_t) * size);
            advance_bytes(sizeof(int32_t) * size);
            break;
        }
        case NBT_Tag_LongArray: {
            int32_t size = body.p_long_array.count = read(int32_t);
            assert(size >= 0);
            int64_t* data = body.p_long_array.arr = allocator->allocate_bytes(allocator, sizeof(int64_t) * size, 0);
            memcpy(data, *buffer, sizeof(int64_t) * size);
            advance_bytes(sizeof(int64_t) * size);
            for (size_t i = 0; i < size; i++) {
                // data[i] = swap_endianness(8, data[i]);
            }
            break;
        }
        default:
            return false;
    }
    *out_body = body;
    return true;
}

static void free_nbt_body(NBT_Tag tag, NBT_Body* body, Enkl_Allocator* allocator) {
    switch (tag) {
        case NBT_Tag_End:
        case NBT_Tag_Byte:
        case NBT_Tag_Short:
        case NBT_Tag_Int:
        case NBT_Tag_Long:
        case NBT_Tag_Float:
        case NBT_Tag_Double:
            break;
        case NBT_Tag_ByteArray:
            allocator->free_bytes(allocator, body->p_byte_array.arr);
            break;
        case NBT_Tag_String:
            allocator->free_bytes(allocator, (void*) body->p_string);
            break;
        case NBT_Tag_List: {
            for (size_t i = 0; i < body->p_list.count; i++) {
                free_nbt_body(body->p_list.tag, &body->p_list.bodies[i], allocator);
            }
            allocator->free_bytes(allocator, body->p_list.bodies);
            break;
        }
        case NBT_Tag_Compound: {
            for (size_t i = 0; i < body->p_compound.count; i++) {
                enkl_free_nbt(body->p_compound.objects[i], allocator);
            }
            // free the array itself
            allocator->free_bytes(allocator, body->p_compound.objects);
            break;
        }
        case NBT_Tag_IntArray:
            allocator->free_bytes(allocator, body->p_int_array.arr);
            break;
        case NBT_Tag_LongArray:
            allocator->free_bytes(allocator, body->p_long_array.arr);
            break;
    }
}

void enkl_free_nbt(NBT_Object* o, Enkl_Allocator* allocator) {
    free_nbt_body(o->tag, &o->body, allocator);
    allocator->free_bytes(allocator, (void*) o->name);
    allocator->free_bytes(allocator, o);
}

static NBT_Object* cunk_decode_nbt_impl(const char* buffer_start, const char* const buffer_end, const char** const buffer, Enkl_Allocator* allocator) {
    NBT_Tag tag = read(uint8_t);
    if (tag == NBT_Tag_End)
        return NULL;

    NBT_Object* o = allocator->allocate_bytes(allocator, sizeof(NBT_Object), 0);
    o->tag = tag;

    uint16_t name_size = read(uint16_t);
    char* name = allocator->allocate_bytes(allocator, name_size + 1, 0);
    memcpy(name, *buffer, name_size * sizeof(int8_t));
    advance_bytes(name_size * sizeof(int8_t));
    name[name_size] = '\0';
    o->name = name;

    bool body_ok = cunk_decode_nbt_body(tag, &o->body, buffer_start, buffer_end, buffer, allocator);
    assert(body_ok);
    return o;
}

NBT_Object* cunk_decode_nbt(size_t buffer_size, const char* buffer, Enkl_Allocator* allocator) {
    return cunk_decode_nbt_impl(buffer, buffer + buffer_size, &buffer, allocator);
}

const NBT_Object* cunk_nbt_compound_direct_access(const NBT_Compound* c, const char* name) {
    assert(name);
    for (size_t i = 0; i < c->count; i++) {
        const NBT_Object* child = c->objects[i];
        if (strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

const NBT_Object* cunk_nbt_compound_access(const NBT_Object* o, const char* name) {
    return cunk_nbt_compound_direct_access(cunk_nbt_extract_compound(o), name);
}

#define X(N, s) const NBT_##N* cunk_nbt_extract_##s(const NBT_Object* o) { \
    if (o->tag != NBT_Tag_##N)                                             \
        return NULL;                                                       \
    return &o->body.p_##s;                                                 \
}
NBT_TAG_TYPES(X)
#undef X
