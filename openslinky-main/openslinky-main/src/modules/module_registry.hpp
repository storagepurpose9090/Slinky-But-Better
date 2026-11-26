#pragma once

#include <cstddef>
#include "events/module.hpp"

namespace slinky::modules {

inline constexpr size_t kModuleRegistryCount = 67;

// Returns the 67-element module registry in exact alphabetical order (matches off_18016BC90)
events::Module* const* get_module_registry() noexcept;

} // namespace slinky::modules
