// Dishonored (2026-11-08): the natives of dh_hot.cpp compiled a second time with the shadow accessors, for the
// self-check guard (as ME2's me2_src/hot/me_hot_shadow.cpp, limited to the natives Dishonored hooks).
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include <simde/x86/sse4.1.h>
#include <simde/arm/neon.h>

#include <rex/platform.h>
#include <rex/ppc/context.h>

#include <mspack.h>  // n_dh_82D9A3B8.h, outside the wrapper
#include <lzx.h>

#include "../me2_src/hot/me_hot_shadow.h"

#define ME_HOT_SHADOW_ACCESSORS 1
namespace me_hot_shadow_copy {
#include "../me2_src/hot/me_hot_common.h"
#include "../me2_src/hot/n/n_82C47090.h"
#include "../me2_src/hot/n/n_82D5F6D0.h"
#include "../me2_src/hot/n/n_82D65BD0.h"
#include "../me2_src/hot/n/n_82A92C50.h"
#include "hot/n_dh_82D9A3B8.h"
#include "hot/n_dh_82BB5D28.h"
#include "hot/n_dh_82BB4C58.h"
#include "hot/n_dh_8251FE10.h"
#include "hot/n_dh_82773D38.h"
#include "hot/n_dh_827739D0.h"
}  // namespace me_hot_shadow_copy
#undef ME_HOT_SHADOW_ACCESSORS

ME_HOT_SHADOW_NATIVE(n_82C47090)
ME_HOT_SHADOW_NATIVE(n_82D5F6D0)
ME_HOT_SHADOW_NATIVE(n_82D65BD0)
ME_HOT_SHADOW_NATIVE(n_82A92C50)
ME_HOT_SHADOW_NATIVE(n_82A92D28)
ME_HOT_SHADOW_NATIVE(n_dh_82D9A3B8)
ME_HOT_SHADOW_NATIVE(n_dh_822E44F8)
ME_HOT_SHADOW_NATIVE(n_dh_82BB5D28)
ME_HOT_SHADOW_NATIVE(n_dh_82BB4C58)
ME_HOT_SHADOW_NATIVE(n_dh_82BB4D20)
ME_HOT_SHADOW_NATIVE(n_dh_8251FE10)
ME_HOT_SHADOW_NATIVE(n_dh_82773D38)
ME_HOT_SHADOW_NATIVE(n_dh_82773E40)
ME_HOT_SHADOW_NATIVE(n_dh_827739D0)
