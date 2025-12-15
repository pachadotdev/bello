#pragma once

#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <uuid/uuid.h>
#endif

static inline std::string gen_uuid() {
#ifdef _WIN32
    GUID guid; CoCreateGuid(&guid);
    char guidStr[39];
    sprintf_s(guidStr, sizeof(guidStr), "%08X-%04X-%04X-%02X%02X-%02X%02X-%02X%02X%02X%02X",
              guid.Data1, guid.Data2, guid.Data3,
              guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    std::string result(guidStr);
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
#else
    uuid_t u; uuid_generate(u);
    char s[37]; uuid_unparse(u, s);
    return std::string(s);
#endif
}
