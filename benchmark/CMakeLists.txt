cmake_minimum_required(VERSION 3.18)
project(ThreadPoolBenchmark CXX)

# ==========================================
# 1. Set C++ Standard (-std=c++23)
# ==========================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==========================================
# 2. Default to Release mode (-O3 -DNDEBUG)
# ==========================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# ==========================================
# 3. Find dependencies (-lpthread)
# ==========================================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ==========================================
# 4. Fetch Third-Party Thread Pools dynamically
# ==========================================
include(FetchContent)

# dp::thread_pool
FetchContent_Declare(
    dp_thread_pool
    GIT_REPOSITORY https://github.com/ptsouchlos/thread-pool.git
    GIT_TAG        0.7.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# BS::thread_pool
FetchContent_Declare(
    bs_thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG        v5.1.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# task_thread_pool::task_thread_pool
FetchContent_Declare(
    task_thread_pool
    GIT_REPOSITORY https://github.com/alugowski/task-thread-pool.git
    GIT_TAG        v1.0.10
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# riften::Thiefpool
FetchContent_Declare(
    riften_threadpool
    GIT_REPOSITORY https://github.com/ConorWilliams/Threadpool.git
    GIT_TAG        a757265
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# riften::Deque (Dependency of Thiefpool)
FetchContent_Declare(
    riften_deque
    GIT_REPOSITORY https://github.com/ConorWilliams/ConcurrentDeque.git
    GIT_TAG        v1.0.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# fu2::function (Dependency of Thiefpool)
FetchContent_Declare(
    function2
    GIT_REPOSITORY https://github.com/Naios/function2.git
    GIT_TAG        4.1.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "dummy_dir_to_skip_cmake"
)

# fetch and activate
FetchContent_MakeAvailable(
    dp_thread_pool
    bs_thread_pool
    task_thread_pool
    riften_threadpool
    riften_deque
    function2
)

# ==========================================
# 5. Define executable target
# ==========================================
add_executable(bench bench.cpp)

# ==========================================
# 6. Configure target properties
# ==========================================
target_include_directories(bench PRIVATE 
    # 1. Include our threadpool (supports #include "stp_thread_pool.h")
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    
    # 2. Include task-thread-pool
    ${task_thread_pool_SOURCE_DIR}/include
    
    # 3. Include bs-thread-pool
    ${bs_thread_pool_SOURCE_DIR}/include
    
    # 4. Include Threadpool
    ${riften_threadpool_SOURCE_DIR}/include
    
    # 5. Include ConcurrentDeque
    ${riften_deque_SOURCE_DIR}/include
    
    # 6. Include dp-thread-pool
    ${dp_thread_pool_SOURCE_DIR}/include

    # 7. Include function2
    ${function2_SOURCE_DIR}/include
)

# ==========================================
# 7. Enable compiler warnings (-Wall -Wextra)
# ==========================================
target_compile_options(bench PRIVATE -Wall -Wextra)

# ==========================================
# 8. Link threading library (-lpthread)
# ==========================================
target_link_libraries(bench PRIVATE Threads::Threads)
