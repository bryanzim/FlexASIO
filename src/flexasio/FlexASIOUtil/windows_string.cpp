#include "windows_string.h"

#include <Windows.h>

#include <stdexcept>
#include <system_error>

namespace flexasio
{

    std::string ConvertToUTF8(std::wstring_view input)
    {
        if (input.size() == 0U)
        {
            return {};
        }

        const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            throw std::system_error(::GetLastError(), std::system_category(), "Unable to get size for string conversion to UTF-8");
        }

        std::string result(size, 0);
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), result.data(), static_cast<int>(result.size()), nullptr, nullptr) != static_cast<int>(result.size()))
        {
            throw std::system_error(::GetLastError(), std::system_category(), "Unable to convert string to UTF-8");
        }
        return result;
    }

    std::wstring ConvertFromUTF8(std::string_view input)
    {
        if (input.size() == 0U)
        {
            return {};
        }

        const auto size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if (size <= 0)
        {
            throw std::system_error(::GetLastError(), std::system_category(), "Unable to get size for string conversion from UTF-8");
        }

        std::wstring result(size, 0);
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), result.data(), static_cast<int>(result.size())) != static_cast<int>(result.size()))
        {
            throw std::system_error(::GetLastError(), std::system_category(), "Unable to convert string from UTF-8");
        }
        return result;
    }

}
