#pragma once

#include <windows.h>

namespace flexasio
{

    class COMInitializer final
    {
    public:
        explicit COMInitializer(DWORD coInit);
        ~COMInitializer();

        COMInitializer(const COMInitializer&) = delete;
        COMInitializer& operator=(const COMInitializer&) = delete;
    };

}
