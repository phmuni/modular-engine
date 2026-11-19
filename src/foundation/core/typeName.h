#pragma once
// Readable C++ type names for messages: "GameState" rather than the compiler's mangled "9GameState".

#include <string>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
#endif

inline std::string typeName(const std::type_info &type) {
#if defined(__GNUC__) || defined(__clang__)
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
