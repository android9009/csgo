#pragma once
// Совместимость имён Fatality/Havoc: Vector/QAngle/Color/Matrix лежат в math/.
#include "../../math/vector.hpp"
#include "../../math/qangle.hpp"
#include "../../math/color.hpp"
#include "../../math/matrix.hpp"
#include <vector>
#include <string>

template <typename T>
class CUtlVector {
public:
    int m_Size; T* m_pMemory; int m_nAllocationCount; int m_nGrowSize;
    CUtlVector() : m_Size(0), m_pMemory(nullptr), m_nAllocationCount(0), m_nGrowSize(0) {}
    T& operator[](int i) { return m_pMemory[i]; }
    const T& operator[](int i) const { return m_pMemory[i]; }
    int Size() const { return m_Size; }
    T* Base() { return m_pMemory; }
    const T* Base() const { return m_pMemory; }
};
class CUtlString {
public:
    char* m_pString;
    CUtlString() : m_pString(nullptr) {}
    const char* Get() const { return m_pString; }
};
