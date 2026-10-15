#pragma once

// Чистая абстракция без костылей
#if defined(__HIP__)
    #include <hip/hip_runtime.h>
#else
    #include <cuda_runtime.h>
    #include <device_launch_parameters.h> // Для blockIdx, threadIdx и т.д.
#endif

// Унифицированные макросы для декорации функций, если понадобятся
#define GPU_HOST __host__
#define GPU_DEVICE __device__
#define GPU_HOST_DEVICE __host__ __device__