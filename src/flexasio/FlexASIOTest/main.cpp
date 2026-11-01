#include "..\FlexASIO\cflexasio.h"

#include <ASIOTest/test.h>

#include <cstdlib>

int main(int argc, char** argv)
{
    auto* const asioDriver = CreateFlexASIO();
    if (asioDriver == nullptr)
    {
        std::abort();
    }

    const auto result = ::ASIOTest_RunTest(asioDriver, argc, argv);

    ReleaseFlexASIO(asioDriver);
    return result;
}
