#ifndef FOUR_WINDS_WIRE_JSON_H
#define FOUR_WINDS_WIRE_JSON_H

#include <string>
#include "swe/swe_json.h"

namespace Multiplayer
{
    // Strict JSON object, <=1 MiB, depth <=32, <=65536 values and object keys.
    // Rejects invalid UTF-8, duplicate (decoded) keys and out-of-range numbers.
    // The output is assigned only after the entire document passes validation.
    // Callers must additionally validate protocol fields with exact JsonTypes.
    bool parseWireObject(const std::string &, SWE::JsonObject &, std::string & error);
}

#endif
