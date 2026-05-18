#pragma once

#include <array>
#include <cstddef>
#include <numeric>

template <typename T, std::size_t Capacity>
class RingBuffer {
public:
    void push(const T& value) {
        data[head] = value;
        head = (head + 1) % Capacity;
        if (count < Capacity) ++count;
    }

    const T& back() const { return data[(head + Capacity - 1) % Capacity]; }

    const T& operator[](std::size_t i) const {
        return data[(head + Capacity - count + i) % Capacity];
    }

    std::size_t size() const { return count; }
    bool empty() const { return count == 0; }

    void clear() { head = 0; count = 0; }

    void fill(const T& value) {
        data.fill(value);
        head = 0;
        count = Capacity;
    }

    T sum() const {
        T s{};
        for (std::size_t i = 0; i < count; ++i)
            s = s + (*this)[i];
        return s;
    }

    float average() const {
        if (count == 0) return 0.0f;
        return static_cast<float>(sum()) / static_cast<float>(count);
    }

private:
    std::array<T, Capacity> data{};
    std::size_t head  = 0;
    std::size_t count = 0;
};
