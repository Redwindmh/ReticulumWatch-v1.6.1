#pragma once

#include <microStore/Adapters/NoopFileSystem.h>
#include <microStore/FileSystem.h>
#include <type_traits>

// microStore 0.1.7 only exposes Adapters::NoopFileSystem when
// USTORE_USE_NOOPFS is defined. Keep this as an explicit compile-time contract
// so a missing build flag fails here with a useful error instead of looking
// like a missing namespace/API.
#ifndef USTORE_USE_NOOPFS
#error "USTORE_USE_NOOPFS must be defined for microStore 0.1.7 NoopFileSystem"
#endif

namespace rw::reticulum {

using RnsNoopFileSystem = microStore::Adapters::NoopFileSystem;

static_assert(std::is_base_of<microStore::FileSystem, RnsNoopFileSystem>::value,
              "microStore 0.1.7 NoopFileSystem must derive from FileSystem");

inline RnsNoopFileSystem makeRnsNoopFileSystem() {
    return RnsNoopFileSystem{};
}

} // namespace rw::reticulum
