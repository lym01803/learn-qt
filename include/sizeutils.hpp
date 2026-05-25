#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <format>

namespace utils {

// unit: Byte
struct fsize {
  using value_t = std::int64_t;

  value_t size{0};
  constexpr fsize() = default;
  constexpr explicit fsize(value_t size) : size{size} {}

  bool operator==(const fsize &other) const {
    return size == other.size;
  }

  auto operator<=>(const fsize &other) const {
    return size <=> other.size;
  }

  value_t value() const {
    return size;
  }
};

inline fsize operator+(const fsize &l, const fsize &r) {
  return fsize{l.size + r.size};
}

inline fsize operator-(const fsize &l, const fsize &r) {
  return fsize{l.size - r.size};
}

inline fsize operator*(const fsize &l, fsize::value_t r) {
  return fsize{l.size * r};
}

inline fsize operator*(const fsize &l, double r) {
  return fsize{static_cast<fsize::value_t>(static_cast<double>(l.size) * r)};
}

inline fsize operator/(const fsize &l, fsize::value_t r) {
  return fsize{l.size / r};
}

inline fsize operator/(const fsize &l, double r) {
  return fsize{static_cast<fsize::value_t>(static_cast<double>(l.size) / r)};
}

} // namespace utils;

template <>
struct std::formatter<utils::fsize> {
  enum class mode_t : std::uint8_t { B, KB, MB, GB, TB, Auto };
  mode_t mode = mode_t::B;
  int digits = 0;

  constexpr auto parse(std::format_parse_context &ctx) {
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') {
      if (*it == 'k' || *it == 'K') {
        mode = mode_t::KB;
      } else if (*it == 'm' || *it == 'M') {
        mode = mode_t::MB;
      } else if (*it == 'g' || *it == 'G') {
        mode = mode_t::GB;
      } else if (*it == 't' || *it == 'T') {
        mode = mode_t::TB;
      } else if (*it == 'a' || *it == 'A') {
        mode = mode_t::Auto;
      } else if ('0' <= *it && *it <= '9') {
        digits = digits * 10 + (*it - '0');
      } else if (*it == '{') {
        throw std::format_error{"not support nested {}"};
      }
      ++it; // NOLINT
    }
    return it;
  }

  auto format(const utils::fsize &obj, std::format_context &ctx) const {
    switch (mode) {
      case mode_t::B: { return std::format_to(ctx.out(), "{} B", obj.size); }
      case mode_t::KB: {
        if (digits == 0) {
          return std::format_to(ctx.out(), "{} KB", obj.size / 1024);
        }
        return std::format_to(ctx.out(), "{:.{}f} KB", 
          static_cast<double>(obj.size) / 1024., digits);
      }
      case mode_t::MB: {
        if (digits == 0) {
          return std::format_to(ctx.out(), "{} MB", obj.size / 1024 / 1024);
        }
        return std::format_to(ctx.out(), "{:.{}f} MB", 
          static_cast<double>(obj.size) / 1024. / 1024., digits);
      }
      case mode_t::GB: {
        if (digits == 0) {
          return std::format_to(ctx.out(), "{} GB", 
            obj.size / 1024 / 1024 / 1024);
        }
        return std::format_to(ctx.out(), "{:.{}f} GB", 
          static_cast<double>(obj.size) / 1024. / 1024. / 1024., digits);
      }
      case mode_t::TB: {
        if (digits == 0) {
          return std::format_to(ctx.out(), "{} TB", 
            obj.size / 1024 / 1024 / 1024 / 1024);
        }
        return std::format_to(ctx.out(), "{:.{}f} TB", 
          static_cast<double>(obj.size) / 1024. / 1024. / 1024. / 1024., digits);
      }
      case mode_t::Auto: {
        int use_digits = digits > 0 ? digits : 2;
        auto size = obj.size;
        if (size < 1024) {
          return std::format_to(ctx.out(), "{} B", size);
        }
        size /= 1024;
        if (size < 1024) {
          return std::format_to(ctx.out(), "{} KB", size);
        }
        auto f_size = static_cast<double>(size) / 1024.;
        if (f_size < 1024.) {
          return std::format_to(ctx.out(), "{:.{}f} MB", f_size, use_digits);
        }
        f_size /= 1024.;
        if (f_size < 1024.) {
          return std::format_to(ctx.out(), "{:.{}f} GB", f_size, use_digits);
        }
        f_size /= 1024.;
        return std::format_to(ctx.out(), "{:.{}f} TB", f_size, use_digits);
      }
    }
  }
};
