#include "singleton/singleton.h"

#include <stdexcept>
#include <string>

namespace slg::singleton::detail {

[[noreturn]] void ThrowUninitializedAccess(const char* type_name) {
    std::string message = "Singleton instance not initialized for type: ";
    message += type_name ? type_name : "<unknown>";
    throw std::logic_error(message);
}

}  // namespace slg::singleton::detail
