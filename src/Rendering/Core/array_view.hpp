#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace Rendering {

// Non-owning view over a contiguous run of T. Lets barrier calls accept a braced list, a std::array or a
// std::vector without copying. A view built from a braced list is only valid until the end of the full
// expression, which is exactly how the barrier functions below use it.
template <typename T> class ArrayView {
public:
    ArrayView() = default;
    // GCC's -Winit-list-lifetime flags keeping a braced list's pointer. Here the view is only a function
    // parameter, so the list outlives it (the same contract C++26 gives std::span).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
    ArrayView(std::initializer_list<T> list) : first(list.begin()), count(list.size()) {}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    ArrayView(const std::vector<T>& v) : first(v.data()), count(v.size()) {}
    template <std::size_t N> ArrayView(const std::array<T, N>& a) : first(a.data()), count(N) {}
    ArrayView(const T& single) : first(&single), count(1) {}

    const T* begin() const { return first; }
    const T* end() const { return first + count; }
    std::size_t size() const { return count; }

private:
    const T* first = nullptr;
    std::size_t count = 0;
};

} // namespace Rendering
