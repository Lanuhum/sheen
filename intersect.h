#pragma once
#include "types.h"

// ---------------------------------------------------------
// Пересечение луча со сферой
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_sphere(const Ray& ray, float3 center, float radius, HitRecord& rec) {
    float3 oc = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    
    if (discriminant < 0.0f) return false;
    
    float sqrtDisc = sqrtf(discriminant);
    float t = (-b - sqrtDisc) / (2.0f * a);
    
    if (t < ray.t_min || t > ray.t_max) {
        t = (-b + sqrtDisc) / (2.0f * a);
        if (t < ray.t_min || t > ray.t_max) return false;
    }
    
    rec.t = t;
    rec.hit = 1;
    rec.point = ray.origin + t * ray.direction;
    rec.normal = normalize(rec.point - center);
    return true;
}

// ---------------------------------------------------------
// Пересечение луча с плоскостью
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_plane(const Ray& ray, float3 normal, float d, HitRecord& rec) {
    float denom = dot(normal, ray.direction);
    if (fabsf(denom) > 1e-6f) {
        float t = -(dot(ray.origin, normal) + d) / denom;
        if (t >= ray.t_min && t <= ray.t_max) {
            rec.t = t;
            rec.hit = 1;
            rec.point = ray.origin + t * ray.direction;
            rec.normal = normal;
            if (dot(rec.normal, ray.direction) > 0.0f) {
                rec.normal = -rec.normal;
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------
// Пересечение луча с коническим сегментом (между двумя SweepPoint)
// ИСПРАВЛЕНО: правильные коэффициенты квадратного уравнения
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_cone_segment(
    const Ray& ray,
    const SweepPoint& p0,
    const SweepPoint& p1,
    HitRecord& rec
) {
    float3 axis = p1.position - p0.position;
    float L2 = dot(axis, axis);
    if (L2 < 1e-12f) return false;  // вырожденный сегмент
    
    float L = sqrtf(L2);
    float3 d_axis = axis / L;  // единичная ось конуса
    
    float r0 = p0.radius;
    float r1 = p1.radius;
    float dr = r1 - r0;
    float k = dr / L;  // скорость изменения радиуса вдоль оси
    
    // Переходим в систему координат: начало в p0, ось X вдоль d_axis
    float3 Q = ray.origin - p0.position;
    float u = dot(ray.direction, d_axis);  // проекция направления на ось
    float v = dot(Q, d_axis);              // проекция начала на ось
    
    // Перпендикулярные компоненты
    float3 D_perp = ray.direction - u * d_axis;
    float3 Q_perp = Q - v * d_axis;
    
    // Квадратное уравнение A*s^2 + B*s + C = 0
    // Условие попадания: |Q_perp + s*D_perp|^2 = (r0 + k*(v + s*u))^2
    float A = dot(D_perp, D_perp) - k * k * u * u;
    float B = 2.0f * (dot(D_perp, Q_perp) - k * r0 * u - k * k * v * u);
    float C = dot(Q_perp, Q_perp) - k * k * v * v - 2.0f * k * r0 * v - r0 * r0;
    
    float discriminant = B * B - 4.0f * A * C;
    if (discriminant < 0.0f) return false;
    
    float sqrtDisc = sqrtf(discriminant);
    float inv2A = 1.0f / (2.0f * A);
    float s1 = (-B - sqrtDisc) * inv2A;
    float s2 = (-B + sqrtDisc) * inv2A;
    
    // Проверяем оба корня, выбираем ближайший валидный
    for (int i = 0; i < 2; i++) {
        float s = (i == 0) ? s1 : s2;
        if (s < ray.t_min || s > ray.t_max) continue;
        
        // Проверяем, что точка пересечения внутри сегмента (0 <= t_param <= L)
        float t_param = v + s * u;  // проекция точки на ось
        if (t_param < 0.0f || t_param > L) continue;
        
        // Нашли валидное пересечение
        rec.t = s;
        rec.hit = 1;
        rec.point = ray.origin + s * ray.direction;
        
        // Нормаль конуса: перпендикуляр к поверхности
        float r_at_t = r0 + k * t_param;
        if (r_at_t < 1e-7f) r_at_t = 1e-7f;
        
        float3 radial = Q_perp + s * D_perp;
        float3 n = radial / r_at_t - k * d_axis;
        rec.normal = normalize(n);
        
        // Убедимся, что нормаль смотрит против луча
        if (dot(rec.normal, ray.direction) > 0.0f) {
            rec.normal = -rec.normal;
        }
        
        return true;
    }
    
    return false;
}

// ---------------------------------------------------------
// Пересечение луча со всей прядью (sphere_sweep)
// Проверяет все конические сегменты + граничные сферы
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_sphere_sweep(
    const Ray& ray,
    const SweepStrand& strand,
    const SweepPoint* points,
    HitRecord& rec
) {
    if (strand.point_count < 2) return false;

    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;

    int base = strand.point_offset;
    int count = strand.point_count;

    // Только конические сегменты — без граничных сфер
    for (int i = 0; i < count - 1; i++) {
        const SweepPoint& p0 = points[base + i];
        const SweepPoint& p1 = points[base + i + 1];

        if (intersect_cone_segment(ray, p0, p1, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = strand.material_id;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }

    return hit_anything;
}

// ---------------------------------------------------------
// Проверка пересечения ТОЛЬКО с массивом плоскостей
// ---------------------------------------------------------
GPU_DEVICE inline bool intersect_planes(
    const Ray& ray, 
    const Plane* planes, 
    int num_planes, 
    HitRecord& rec
) {
    bool hit_anything = false;
    float closest_t = ray.t_max;
    HitRecord temp_rec;
    
    for (int i = 0; i < num_planes; i++) {
        const Plane& p = planes[i];
        if (intersect_plane(ray, p.normal, p.d, temp_rec)) {
            if (temp_rec.t < closest_t) {
                rec = temp_rec;
                rec.material_id = p.material_id;
                rec.object_id = -1 - i;
                closest_t = temp_rec.t;
                hit_anything = true;
            }
        }
    }
    return hit_anything;
}