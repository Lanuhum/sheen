#include <cstdio>
#include <cmath>
#include <vector>
#include <cuda_runtime.h>

template <typename T>
__global__ void scale(T *p, int n, T k)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) p[i] = p[i] * k + T(1) / T(3);
}

int main()
{
    std::vector<float> h(4096, 2.0f);
    float *d = 0;
    cudaMalloc((void **)&d, h.size() * sizeof(float));
    cudaMemcpy(d, h.data(), h.size() * sizeof(float), cudaMemcpyHostToDevice);

    scale<float><<<16, 256>>>(d, (int)h.size(), 3.0f);
    cudaDeviceSynchronize();
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("kernel failed: %s\n", cudaGetErrorString(err));
        return 1;
    }

    cudaMemcpy(h.data(), d, h.size() * sizeof(float), cudaMemcpyDeviceToHost);
    printf("h[0] = %.4f (expect %.4f)\n", h[0], 6.3333f);
    printf("std::sqrt(2) = %.4f\n", std::sqrt(2.0));

    cudaFree(d);
    return 0;
}
