#include <cstdio>
#include <cuda_runtime.h>

__global__ void fill(float *out, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = sqrtf((float)i) * 2.0f;
}

int main()
{
    int devCount = 0;
    cudaError_t err = cudaGetDeviceCount(&devCount);
    if (err != cudaSuccess) {
        printf("cudaGetDeviceCount failed: %s\n", cudaGetErrorString(err));
        return 1;
    }

    printf("devices: %d\n", devCount);
    for (int d = 0; d < devCount; ++d) {
        cudaDeviceProp p;
        cudaGetDeviceProperties(&p, d);
        printf("device %d: %s | sm_%d%d | %d MB | ECC %s\n",
               d, p.name, p.major, p.minor,
               (int)(p.totalGlobalMem / (1024u * 1024u)),
               p.ECCEnabled ? "on" : "off");
    }
    cudaSetDevice(0);

    const int n = 1 << 20;
    float *dev = 0;
    if (cudaMalloc((void **)&dev, n * sizeof(float)) != cudaSuccess) {
        printf("cudaMalloc failed: %s\n", cudaGetErrorString(cudaGetLastError()));
        return 1;
    }

    cudaEvent_t t0, t1;
    cudaEventCreate(&t0);
    cudaEventCreate(&t1);

    fill<<<(n + 255) / 256, 256>>>(dev, n);
    cudaDeviceSynchronize();
    err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("kernel failed: %s\n", cudaGetErrorString(err));
        return 1;
    }

    cudaEventRecord(t0);
    for (int i = 0; i < 100; ++i) fill<<<(n + 255) / 256, 256>>>(dev, n);
    cudaEventRecord(t1);
    cudaEventSynchronize(t1);

    float ms = 0.0f;
    cudaEventElapsedTime(&ms, t0, t1);
    printf("100 x 1M kernel: %.3f ms total (%.4f ms per kernel)\n", ms, ms / 100.0f);

    float *host = new float[n];
    cudaMemcpy(host, dev, n * sizeof(float), cudaMemcpyDeviceToHost);
    printf("host[1024] = %.4f (expect %.4f)\n", host[1024], sqrtf(1024.0f) * 2.0f);
    delete[] host;

    size_t freeMem = 0, totalMem = 0;
    cudaMemGetInfo(&freeMem, &totalMem);
    printf("VRAM: %.2f GB free of %.2f GB\n",
           freeMem / 1073741824.0, totalMem / 1073741824.0);

    cudaFree(dev);
    return 0;
}
