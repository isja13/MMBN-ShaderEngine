#ifndef UNKNOWN_IMPL_H
#define UNKNOWN_IMPL_H

#include "wrapper_registry.h"

#define IUNKNOWN_PRIV(b) \
    b *inner = NULL; \
    volatile LONG wrapper_ref = 1;

#define IUNKNOWN_INIT(n) \
    inner(n), wrapper_ref(1)

#define IUNKNOWN_IMPL_NO_QI(d, b) \
    b *&d::get_inner() { \
        return impl->inner; \
    } \
 \
    ULONG STDMETHODCALLTYPE d::AddRef() { \
        impl->inner->AddRef(); \
        ULONG ret = (ULONG)InterlockedIncrement(&impl->wrapper_ref); \
        LOG_MFUN(_, ret); \
        return ret; \
    } \
 \
    ULONG STDMETHODCALLTYPE d::Release() { \
        impl->inner->Release(); \
        ULONG ret = (ULONG)InterlockedDecrement(&impl->wrapper_ref); \
        LOG_MFUN(_, ret); \
        if (ret == 0) { \
            delete this; \
        } \
        return ret; \
    }

#define IUNKNOWN_IMPL(d, b) \
    IUNKNOWN_IMPL_NO_QI(d, b) \
 \
    HRESULT STDMETHODCALLTYPE d::QueryInterface( \
        REFIID riid, \
        void   **ppvObject \
    ) { \
        HRESULT ret = impl->inner->QueryInterface(riid, ppvObject); \
        if (ret == S_OK) { \
            if (riid == IID_IUnknown) { \
                impl->inner->Release(); \
                *ppvObject = this; \
                AddRef(); \
            } \
            LOG_MFUN(_, \
                LOG_ARG(riid), \
                LOG_ARG(*ppvObject), \
                ret \
            ); \
        } else { \
            LOG_MFUN(_, \
                LOG_ARG(riid), \
                ret \
            ); \
        } \
        return ret; \
    }

#endif