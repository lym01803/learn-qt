#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <string_view>
namespace utils {

struct utf8_iterator {
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::string_view;
  using pointer = value_type *;
  using reference = value_type &;

  utf8_iterator() = default;

  utf8_iterator(std::string_view str) { // NOLINT
    if (str.empty() || str.data() == nullptr) {
      return;
    }
    bound = str.size();
    const auto byte = str[0];
    if ((byte & 0b1000'0000) == 0) {
      sv = std::string_view {str.data(), 1};
      return;
    }
    if ((byte & 0b1110'0000) == 0b1100'0000) {
      sv = std::string_view {str.data(), std::min((size_t)2, bound)};
      return;
    }
    if ((byte & 0b1111'0000) == 0b1110'0000) {
      sv = std::string_view {str.data(), std::min((size_t)3, bound)};
      return;
    }
    sv = std::string_view {str.data(), std::min((size_t)4, bound)};
  }

  bool operator==(const utf8_iterator &other) const {
    return sv.data() == other.sv.data() && bound == other.bound;
  }

  utf8_iterator &operator++() {
    *this = utf8_iterator(std::string_view {sv.data() + sv.size(), bound - sv.size()});
    return *this;
  }

  utf8_iterator operator++(int) {
    utf8_iterator temp = *this;
    ++(*this);
    return temp;
  }

  pointer operator->() noexcept {
    return std::addressof(sv);
  }

  reference operator*() noexcept {
    return sv;
  }

private:
  std::string_view sv;
  size_t bound = 0;
};

struct utf8_view {
  std::string_view sv;

  utf8_iterator begin() const {
    return utf8_iterator{sv};
  }

  utf8_iterator end() const {
    return utf8_iterator{};
  }
};

} // namespace utils
