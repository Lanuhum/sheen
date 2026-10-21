#pragma once
#include "types.h"

// ---------------------------------------------------------
// Обход CSG-дерева.
// Каждый узел возвращает список интервалов вдоль луча (входы/выходы).
// Лист — интервал примитива. Внутренний — комбинация интервалов детей
// по операции (union/intersection/difference). В конце берём ближайший
// интервал и превращаем в HitRecord (точка, нормаль).
//
// Интервалы хранятся в локальном массиве фиксированной длины:
// CSG с парой примитивов (комната!) даёт <= 8 интервалов. Для больших
// деревьев этого хватит в подавляющем большинстве сценариев.
// ---------------------------------------------------------

struct CSGStack {
    CSGInterval iv[8];
    int count = 0;
};

// Один интервал примитива (лист CSG).
GPU_DEVICE inline bool csg_leaf_intervals(
    const Ray& ray, int kind, int index,
    const Object* objects, const Superellipsoid* ses, const HeightField* hfs,
    float t0, float t1, CSGInterval& out
) {
    out.t_enter = -1e30f;
    out.t_exit  = -1e30f;
    if (kind == 0) {  // сфера
        const Object& o = objects[index];
        return sphere_intervals(ray, o.param1, o.param2, t0, t1, out.t_enter, out.t_exit);
    } else if (kind == 1) {  // суперэллипсоид
        const Superellipsoid& se = ses[index];
        return se_intervals(ray, se, t0, t1, out.t_enter, out.t_exit);
    } else if (kind == 2) {  // HeightField (пока не реализовано как интервал)
        return false;
    }
    return false;
}

// Объединение двух наборов интервалов по операции.
// op: 0=union, 1=intersection, 2=difference (a - b)
GPU_DEVICE inline void csg_merge(const CSGInterval* A, int nA,
                                 const CSGInterval* B, int nB,
                                 int op, CSGInterval* out, int& nOut) {
    nOut = 0;
    if (op == 0) {  // union: собрать всё и слить перекрывающиеся интервалы
        CSGInterval tmp[16];
        int n = 0;
        for (int i = 0; i < nA && n < 16; i++) tmp[n++] = A[i];
        for (int i = 0; i < nB && n < 16; i++) tmp[n++] = B[i];

        for (int i = 1; i < n; i++) {          // сортировка вставками
            CSGInterval key = tmp[i];
            int j = i - 1;
            while (j >= 0 && tmp[j].t_enter > key.t_enter) {
                tmp[j + 1] = tmp[j];
                j--;
            }
            tmp[j + 1] = key;
        }

        for (int i = 0; i < n && nOut < 8; i++) {
            if (nOut == 0 || tmp[i].t_enter > out[nOut - 1].t_exit) {
                out[nOut++] = tmp[i];
            } else if (tmp[i].t_exit > out[nOut - 1].t_exit) {
                out[nOut - 1].t_exit = tmp[i].t_exit;
            }
        }
    } else if (op == 1) {  // intersection: пересечения отрезков AиB попарно
        for (int i = 0; i < nA; i++) {
            for (int j = 0; j < nB; j++) {
                float t0 = fmaxf(A[i].t_enter, B[j].t_enter);
                float t1 = fminf(A[i].t_exit, B[j].t_exit);
                if (t0 < t1 && nOut < 8) {
                    out[nOut] = A[i];
                    out[nOut].t_enter = t0;
                    out[nOut].t_exit  = t1;
                    nOut++;
                }
            }
        }
    } else if (op == 2) {  // difference: A минус (пересечения с B)
        for (int i = 0; i < nA; i++) {
            // вырезаем из A[i] все отрезки B, что перекрывают его
            float cur = A[i].t_enter;
            for (int j = 0; j < nB; j++) {
                float b0 = B[j].t_enter, b1 = B[j].t_exit;
                if (b1 <= cur) continue;          // B[j] раньше текущего
                if (b0 >= A[i].t_exit) break;     // B[j] после A[i]
                if (b0 > cur) {  // отрезок [cur, b0] остаётся
                    if (nOut < 8) {
                        out[nOut] = A[i];
                        out[nOut].t_enter = cur;
                        out[nOut].t_exit  = b0;
                        nOut++;
                    }
                }
                cur = fmaxf(cur, b1);
                if (cur >= A[i].t_exit) break;
            }
            if (cur < A[i].t_exit && nOut < 8) {
                out[nOut] = A[i];
                out[nOut].t_enter = cur;
                out[nOut].t_exit  = A[i].t_exit;
                nOut++;
            }
        }
    }
}

// Рекурсивный обход: массив узлов + корень. Возвращает интервалы корня.
GPU_DEVICE inline void csg_collect(
    const CSGNode* nodes, int root,
    const Ray& ray,
    const Object* objects, const Superellipsoid* ses, const HeightField* hfs,
    float t0, float t1, CSGInterval* out, int& nOut
) {
    const CSGNode& n = nodes[root];
    if (n.leaf_kind >= 0) {  // лист
        nOut = 0;
        CSGInterval iv;
        if (csg_leaf_intervals(ray, n.leaf_kind, n.leaf_index, objects, ses, hfs, t0, t1, iv)) {
            iv.material_id = (n.leaf_kind == 0) ? objects[n.leaf_index].material_id
                            : (n.leaf_kind == 1) ? ses[n.leaf_index].material_id
                            : 0;
            iv.leaf_id = n.leaf_index;
            iv.kind = n.leaf_kind;
            out[nOut++] = iv;
        }
        return;
    }

    CSGInterval A[8], B[8];
    int nA = 0, nB = 0;
    if (n.left >= 0) csg_collect(nodes, n.left, ray, objects, ses, hfs, t0, t1, A, nA);
    if (n.right >= 0) csg_collect(nodes, n.right, ray, objects, ses, hfs, t0, t1, B, nB);
    csg_merge(A, nA, B, nB, n.op, out, nOut);
}

// Ближайший интервал CSG-дерева -> HitRecord.
GPU_DEVICE inline bool intersect_csg(
    const Ray& ray,
    const CSGNode* nodes, int root,
    const Object* objects, const Superellipsoid* ses, const HeightField* hfs,
    float t0, float t1, HitRecord& rec
) {
    CSGInterval iv[8];
    int count = 0;
    csg_collect(nodes, root, ray, objects, ses, hfs, t0, t1, iv, count);

    // ближайший (минимальный t_enter в пределах луча)
    float best = 1e30f;
    int best_i = -1;
    for (int i = 0; i < count; i++) {
        if (iv[i].t_enter < best && iv[i].t_exit > t0) {
            best = iv[i].t_enter;
            best_i = i;
        }
    }
    if (best_i < 0) return false;

    const CSGInterval& hit = iv[best_i];
    float t = fmaxf(hit.t_enter, t0);
    if (t < t0 || t > t1) return false;

    rec.t = t;
    rec.hit = 1;
    rec.material_id = hit.material_id;
    rec.point = ray.origin + t * ray.direction;

    // нормаль: от примитива, давшего интервал
    if (hit.kind == 0) {
        const Object& o = objects[hit.leaf_id];
        rec.normal = normalize(rec.point - o.param1);
        rec.tangent = {0.0f, 0.0f, 0.0f};
    } else if (hit.kind == 1) {
        rec.normal = normalize(se_grad(ses[hit.leaf_id], rec.point));
        rec.tangent = {1.0f, 0.0f, 0.0f};
    } else {
        rec.normal = {0.0f, 1.0f, 0.0f};
        rec.tangent = {1.0f, 0.0f, 0.0f};
    }
    rec.uv = {rec.point.x, rec.point.y};
    if (dot(rec.normal, ray.direction) > 0.0f) rec.normal = -rec.normal;
    return true;
}