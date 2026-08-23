#pragma once

#include "../../../TR-Shared/SimpleUIV2/TRSimpleUIV2.h"

#include <string>
#include <vector>

namespace TR::FreqUIV2
{
static_assert(SimpleUIV2::TR_SHARED_UI_V2_API_VERSION == 0x00020024u,
              "FREQ-TR requires TR SimpleUIV2 API 2.1");
inline constexpr std::string_view FREQ_REQUIRED_SHARED_UI_V2_REVISION =
    "simple-ui-v2-family-equivalence-20260816";
static_assert(FREQ_REQUIRED_SHARED_UI_V2_REVISION == SimpleUIV2::TR_SHARED_UI_V2_REVISION,
              "FREQ-TR and TR SimpleUIV2 revisions are not coordinated");

const SimpleUIV2::SimplePluginDefinition& definition();
const std::vector<std::string>& retiredUiParameterIds();
}
