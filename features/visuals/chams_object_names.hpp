#pragma once
// Network-class names of the throwable projectiles, kept free of any SDK include so
// the object classification and its tests stay portable.
#include <string_view>

namespace ChamsObjectNames {
inline bool Projectile(std::string_view name) {
    return name == "CBaseCSGrenadeProjectile" || name == "CSmokeGrenadeProjectile" ||
           name == "CDecoyProjectile" || name == "CMolotovProjectile" || name == "CInferno";
}
}
