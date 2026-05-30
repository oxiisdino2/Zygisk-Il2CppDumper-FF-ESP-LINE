#pragma once
#include <cstdint>

// Method RVAs in libil2cpp.so (from script.json)
// Runtime address = libil2cpp_base + RVA
namespace MethodRVAs {
    constexpr uint32_t GameFacade_CurrentLocalPlayer = 0x5F52F04;  // -> Player*
    constexpr uint32_t Player_get_CurHP             = 0x59E5A48;  // -> int
    constexpr uint32_t Player_get_MaxHP             = 0x59E5B5C;  // -> int
    constexpr uint32_t Player_get_TeamIndex         = 0x596D408;  // -> int
    constexpr uint32_t Camera_get_main              = 0x9806824;  // -> Camera* (in libunity.so)
}

// Il2CppObject header - every managed object starts with this
struct Il2CppObject {
    uintptr_t klass;       // Il2CppClass* (0x0)
    uintptr_t monitor;     // MonitorData* (0x4 on x86)
};

// Il2CppClass important fields (x86, offset varies by version)
struct Il2CppClass {
    // ... many fields, key ones:
    // uintptr_t *static_fields;     // pointer to static field data
    // const char *name;             // class name string
};

// String object layout (System.String)
struct Il2CppString {
    Il2CppObject obj;
    int32_t length;          // 0x8 - string length
    uint16_t chars[1];       // 0xC - UTF-16 chars (variable length)
};

// Array layout (System.Collections.Generic.Dictionary<K,V> etc)
struct Il2CppArray {
    Il2CppObject obj;
    uintptr_t bounds;        // 0x8 - nullptr for single-dim, bounds for multi
    int32_t max_length;      // 0xC - total array length
    // uint8_t data[1];      // 0x10 - actual elements start here
};

// Dictionary layout (simplified - actual layout is complex)
// Dictionary actually contains buckets, entries, count, etc.
struct UnityDictionary {
    Il2CppObject obj;
    int32_t count;           // number of entries
    // ... internal buckets/entries arrays
};

// List<T> layout
struct UnityList {
    Il2CppObject obj;
    uintptr_t _items;        // 0x8 - T[] array
    int32_t _size;           // 0xC - number of elements
    int32_t _version;        // 0x10
};
