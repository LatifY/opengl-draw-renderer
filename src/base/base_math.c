#include "base_math.h"
#include <math.h>
#include <stdio.h>

b32 vec2f_in_rectf(vec2f point, rectf rect) {
    return (
        point.x >= rect.x &&
        point.x <= rect.x + rect.w &&
        point.y >= rect.y &&
        point.y <= rect.y + rect.h
    );
}
b32 rectf_collide_rectf(rectf a, rectf b) {
    return (
        a.x + a.w >= b.x &&
        a.x <= b.x + b.w &&
        a.y + a.h >= b.y &&
        a.y <= b.y + b.h
    );
}
// https://www.jeffreythompson.org/collision-detection/circle-rect.php
b32 rectf_collide_circlef(rectf rect, circlef circle) {
    // temporary variables to set edges for testing
    vec2f test_pos = circle.pos;

    // which edge is closest?
    if (circle.pos.x < rect.x)
        test_pos.x = rect.x; // test left edge
    else if (circle.pos.x > rect.x+rect.w)
        test_pos.x = rect.x + rect.w; // right edge
    if (circle.pos.y < rect.y)
        test_pos.y = rect.y; // top edge
    else if (circle.pos.y > rect.y+rect.h)
        test_pos.y = rect.y+rect.h; // bottom edge

    f32 dist = vec2f_dist(circle.pos, test_pos);

    // if the distance is less than the radius, collision!
    if (dist <= circle.r) {
        return true;
    }
    return false;
}

vec2f vec2f_add(vec2f a, vec2f b) {
    return (vec2f){ a.x + b.x, a.y + b.y };
}
vec2f vec2f_sub(vec2f a, vec2f b) {
    return (vec2f){ a.x - b.x, a.y - b.y };
}
vec2f vec2f_scl(vec2f v, f32 s) {
    return (vec2f){ v.x * s, v.y * s };
}
f32 vec2f_dot(vec2f a, vec2f b) {
    return a.x * b.x + a.y * b.y;
}
f32 vec2f_crs(vec2f a, vec2f b) {
    return a.x * b.y - a.y * b.x;
}
f32 vec2f_sqr_len(vec2f v) {
    return v.x * v.x + v.y * v.y;
}
f32 vec2f_len(vec2f v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}
f32 vec2f_sqr_dist(vec2f a, vec2f b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}
f32 vec2f_dist(vec2f a, vec2f b) {
    return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
vec2f vec2f_prp(vec2f v) {
    return (vec2f){ -v.y, v.x };
}
vec2f vec2f_nrm(vec2f v) {
    f32 r = 1.0f / sqrtf(v.x * v.x + v.y * v.y);
    return (vec2f){ v.x * r, v.y * r };
}
b32 vec2f_eq(vec2f a, vec2f b) {
    return (a.x == b.x && a.y == b.y);
}
vec2f vec2f_ref(vec2f d, vec2f n) {
    return vec2f_sub(d, vec2f_scl(n, 2.0f * vec2f_dot(d, n)));
}

cubic_bezier cbezier_create(vec2f p0, vec2f p1, vec2f p2, vec2f p3) {
    cubic_bezier out;
    out.a = p3;
    out.a = vec2f_sub(out.a, vec2f_scl(p2, 3.0f));
    out.a = vec2f_add(out.a, vec2f_scl(p1, 3.0f));
    out.a = vec2f_sub(out.a, p0);

    out.b = p2;
    out.b = vec2f_sub(out.b, vec2f_scl(p1, 2.0f));
    out.b = vec2f_add(out.b, p0);

    out.c = vec2f_sub(p1, p0);

    out.d = p0;

    return out;
}
vec2f cbezier_calc(const cubic_bezier* bez, f32 t) {
    vec2f out = bez->d;

    out = vec2f_add(out, vec2f_scl(bez->c, 3.0f * t));
    out = vec2f_add(out, vec2f_scl(bez->b, 3.0f * t * t));
    out = vec2f_add(out, vec2f_scl(bez->a, t * t * t));

    return out;
}

void mat3f_transform(mat3f* mat, vec2f scale, vec2f offset, f32 rotation) {
    f32 r_sin = sinf(rotation);
    f32 r_cos = cosf(rotation);

    mat->m[0] = r_cos * scale.x;
    mat->m[1] = -r_sin * scale.y;
    mat->m[2] = 0.0f;
    mat->m[3] = r_sin * scale.x;
    mat->m[4] = r_cos * scale.y;
    mat->m[5] = 0.0f;
    mat->m[6] = offset.x;
    mat->m[7] = offset.y;
    mat->m[8] = 1.0f;
}
void mat3f_from_view(mat3f* mat, viewf v) {
    vec2f size = vec2f_scl((vec2f){ 1.0f, 1.0f / v.aspect_ratio }, v.width);

    vec2f scale = {
         2.0f / size.x,
        -2.0f / size.y,
    };

    f32 r_sin = sinf(v.rotation);
    f32 r_cos = cosf(v.rotation);

    vec2f offset = {
        -(v.center.x * (r_cos * scale.x) + v.center.y * (r_sin * scale.x)),
        -(v.center.x * (-r_sin * scale.y) + v.center.y * (r_cos * scale.y))
    };

    mat3f_transform(mat, scale, offset, v.rotation);
}
void mat3f_inverse(mat3f* out, const mat3f* mat) {
    // Renaming so it is easier to type
    const f32* m = mat->m;
    f32 mat_det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);

    if (fabsf(mat_det) < 1e-8f) {
        fprintf(stderr, "Cannot inverse mat3: determinant of matrix is near zero\n");
        return;
    }

    f32 inv_det = 1.0f / mat_det;

    out->m[0] = (m[4] * m[8] - m[5] * m[7]) * inv_det;
    out->m[1] = (m[2] * m[7] - m[1] * m[8]) * inv_det;
    out->m[2] = (m[1] * m[5] - m[2] * m[4]) * inv_det;
    out->m[3] = (m[5] * m[6] - m[3] * m[8]) * inv_det;
    out->m[4] = (m[0] * m[8] - m[2] * m[6]) * inv_det;
    out->m[5] = (m[2] * m[3] - m[0] * m[5]) * inv_det;
    out->m[6] = (m[3] * m[7] - m[4] * m[6]) * inv_det;
    out->m[7] = (m[1] * m[6] - m[0] * m[7]) * inv_det;
    out->m[8] = (m[0] * m[4] - m[1] * m[3]) * inv_det;
}
vec2f mat3f_mul_vec2f(const mat3f* mat, vec2f vec) {
    vec2f out = { 0 };

    out.x = vec.x * mat->m[0] + vec.y * mat->m[3] + mat->m[6];
    out.y = vec.x * mat->m[1] + vec.y * mat->m[4] + mat->m[7];

    return out;
}

