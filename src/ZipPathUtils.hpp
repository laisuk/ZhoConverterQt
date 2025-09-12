#pragma once
#include <filesystem>
#include <system_error>
#include <string>

namespace office::zip
{
    namespace fs = std::filesystem;

    /// Return an absolute, stable version of a path without throwing.
    [[nodiscard]] inline fs::path stable_abs(const fs::path& p) noexcept
    {
        std::error_code ec;
        fs::path abs = fs::weakly_canonical(p, ec);
        if (ec)
        {
            ec.clear();
            abs = fs::absolute(p, ec);
        }
        return abs;
    }

    /// Produce a stable ZIP entry name from an absolute file path and absolute base dir:
    /// - never throws (uses error_code overloads)
    /// - falls back to lexical_relative when roots differ (e.g., R:\temp vs C:\...)
    /// - guarantees forward slashes + UTF-8
    [[nodiscard]] inline std::string make_zip_entry(const fs::path& full,
                                                    const fs::path& baseAbs) noexcept
    {
        std::error_code ec;
        fs::path rel = fs::relative(full, baseAbs, ec);

        if (ec || rel.empty() || rel.is_absolute())
        {
            ec.clear();
            fs::path lex = full.lexically_relative(baseAbs);
            if (!lex.empty()) rel = std::move(lex);
            else rel = full.filename(); // last resort: keep file name
        }
        return rel.generic_u8string(); // UTF-8 + '/'
    }
} // namespace office::zip
