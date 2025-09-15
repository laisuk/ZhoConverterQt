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
        // Try to resolve as much as possible; works even if final path doesn't exist yet.
        fs::path abs = fs::weakly_canonical(p, ec);
        if (ec)
        {
            ec.clear();
            abs = fs::absolute(p, ec);
            if (!ec) abs = abs.lexically_normal();
        }
        return abs;
    }

    /// Produce a stable ZIP entry name from an absolute file path and absolute base dir:
    /// - never throws (uses error_code overloads)
    /// - tries relative() first, then lexically_relative(), then filename()
    /// - returns forward slashes, no leading "./", and no leading '/'
    [[nodiscard]] inline std::string make_zip_entry(const fs::path& full,
                                                    const fs::path& baseAbs) noexcept
    {
        std::error_code ec;

        // First attempt: relative as-is (fast path, works for same-root, same-symlink spelling)
        fs::path rel = fs::relative(full, baseAbs, ec);

        // If that fails (different spellings or symlinks), try canonical forms (both exist at write time)
        if (ec || rel.empty() || rel.is_absolute())
        {
            ec.clear();
            std::error_code ec2;
            const fs::path full_can = fs::weakly_canonical(full, ec2);
            const fs::path base_can = fs::weakly_canonical(baseAbs, ec2);
            if (!ec2)
            {
                rel = fs::relative(full_can, base_can, ec);
            }
        }

        // Fallbacks if still not good
        if (ec || rel.empty() || rel.is_absolute())
        {
            ec.clear();
            if (fs::path lex = full.lexically_relative(baseAbs); !lex.empty() && !lex.is_absolute())
            {
                rel = std::move(lex);
            }
            else
            {
                rel = full.filename(); // last resort
            }
        }

        // Normalize to forward slashes and sanitize leading markers
        std::string out = rel.generic_string(); // always '/' separators in a std::string

        // Strip a leading "./"
        if (out.size() >= 2 && out[0] == '.' && out[1] == '/')
        {
            out.erase(0, 2);
        }
        // Ensure no leading slash (ZIP entries must be relative)
        while (!out.empty() && out.front() == '/')
        {
            out.erase(0, 1);
        }
        // Avoid empty name
        if (out.empty())
        {
            out = full.filename().generic_string();
        }
        return out;
    }
} // namespace office::zip
