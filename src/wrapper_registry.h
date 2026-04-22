#ifndef WRAPPER_REGISTRY_H
#define WRAPPER_REGISTRY_H

#include <unordered_set>
#include <mutex>

// Global Registry for pointers owned by the mod wrapper. 
// Protects against null derefs or C-style casts & lifetime issues.

extern std::unordered_set<void*> g_wrapper_registry;
inline std::mutex g_wrapper_registry_mutex;

inline void register_wrapper(void* ptr) {
    std::lock_guard<std::mutex> lock(g_wrapper_registry_mutex);
    g_wrapper_registry.insert(ptr);
}

inline void unregister_wrapper(void* ptr) {
    std::lock_guard<std::mutex> lock(g_wrapper_registry_mutex);
    g_wrapper_registry.erase(ptr);
}

inline bool is_wrapper(void* ptr) {
    if (!ptr) return false;
    std::lock_guard<std::mutex> lock(g_wrapper_registry_mutex);
    return g_wrapper_registry.count(ptr) != 0;
}

template<typename WrapperT, typename BaseT>
BaseT* unwrap(BaseT* ptr) {
    if (!ptr) return nullptr;
    std::lock_guard<std::mutex> lock(g_wrapper_registry_mutex);
    if (g_wrapper_registry.count((void*)ptr))
        return ((WrapperT*)ptr)->get_inner();
    return ptr;
}

template<typename WrapperT, typename BaseT>
WrapperT* as_wrapper(BaseT* ptr) {
    if (!ptr) return nullptr;
    std::lock_guard<std::mutex> lock(g_wrapper_registry_mutex);
    if (g_wrapper_registry.count((void*)ptr))
        return (WrapperT*)ptr;
    return nullptr;
}

#endif