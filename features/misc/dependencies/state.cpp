#include "../misc.hpp"

namespace misc_features {
state& settings() {
    static state instance;
    return instance;
}
}

