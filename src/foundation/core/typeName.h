#pragma once
// Readable C++ type names for messages: "GameState" rather than the compiler's mangled "9GameState".

#include <string>
#include <typeinfo>
// Asked of the header, not the compiler: clang-cl (Clang on Windows with Microsoft's library) says it is Clang but has
// no cxxabi.h, and Microsoft's compiler gives readable names anyway
#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#define MODULAR_ENGINE_HAS_CXXABI 1
#include <cstdlib>
#include <cxxabi.h>
#endif
#endif

inline std::string typeName(const std::type_info &type) {
#ifdef MODULAR_ENGINE_HAS_CXXABI
  int status = 0;
  char *demangled = abi::__cxa_demangle(type.name(), nullptr, nullptr, &status);
  if (status == 0 && demangled) {
    std::string name(demangled);
    std::free(demangled);
    return name;
  }
#endif
  return type.name(); // MSVC names are readable already
}
