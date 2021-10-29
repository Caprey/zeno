#pragma once

#include <zeno/common.h>

#if defined(ZENO_WITH_SYCL)
#include <CL/sycl.hpp>

ZENO_NAMESPACE_BEGIN
namespace zycl {
    using namespace cl::sycl;
}
ZENO_NAMESPACE_END
#else
#pragma message("<zeno/zycl/zycl.h> is using host emulated sycl, which is CPU-only and slow.")
#define ZENO_SYCL_IS_EMULATED 1

#include <array>
#include <vector>

ZENO_NAMESPACE_BEGIN
namespace zycl {

using cl_int = int;
using cl_float = float;

template <size_t N>
struct id : std::array<size_t, N> {
    using std::array<size_t, N>::array;

    constexpr explicit(N != 1) id(size_t i)
        : std::array<size_t, N>({i}) {
        if constexpr (N != 1) {
            for (int j = 1; j < N; j++) {
                (*this)[j] = i;
            }
        }
    }

    constexpr explicit(N != 1) operator size_t() const {
        return std::get<0>(*this);
    }
};

template <size_t N>
struct range : id<N> {
    using id<N>::id;
};

template <size_t N>
struct item : range<N> {
    using range<N>::range;
};


template <size_t N>
struct nd_range {
    id<N> global_size{};
    id<N> local_size{};

    nd_range() = default;

    explicit nd_range(id<N> global_size, id<N> local_size)
        : global_size(global_size), local_size(local_size)
    {}

    constexpr size_t get_global_size(size_t i) const {
        return global_size[i];
    }

    constexpr size_t get_local_size(size_t i) const {
        return local_size[i];
    }
};

template <size_t N>
struct nd_item : nd_range<N> {
    id<N> global_id{};
    id<N> local_id{};

    constexpr size_t get_global_id(size_t i) const {
        return global_id[i];
    }

    constexpr size_t get_local_id(size_t i) const {
        return local_id[i];
    }
};

template <size_t I, size_t N>
void _M_nd_range_for(range<N> const &size, id<N> &index, auto &&f) {
    if constexpr (I == N) {
        f(index);
    } else {
        for (index[I] = 0; index[I] < size[I]; index[I]++) {
            _M_nd_range_for<I + 1, N>(size, index, f);
        }
    }
}

template <size_t N>
void _M_nd_range_for(range<N> const &size, auto &&f) {
    id<N> index;
    _M_nd_range_for<0>(size, index, f);
}

struct handler {
    template <class = void>
    void single_task(auto &&f) {
        f();
    }

    template <class = void, size_t N>
    void parallel_for(range<N> dim, auto &&f) {
        _M_nd_range_for(dim, [&] (id<N> idx) {
            item<N> itm(idx);
            f(itm);
        });
    }

    template <class = void, size_t N>
    void parallel_for(nd_range<N> dim, auto &&f) {
        _M_nd_range_for(dim.global_size, [&] (id<N> global_id) {
            nd_item<N> item;
            item.global_id = global_id;
            f(item);
        });
    }
};

struct selector {
};

struct default_selector : selector {
};

struct cpu_selector : selector {
};

struct gpu_selector : selector {
};

struct queue {
    explicit queue(selector = default_selector{}) {}

    void submit(auto &&f) {
        handler h;
        f(h);
    }
};

namespace access {

enum class mode {
    read,
    write,
    read_write,
    discard_write,
    discard_read_write,
    atomic,
};

};

template <access::mode mode, class Buf, class T, size_t N>
struct accessor {
    Buf const &buf;

    explicit accessor(Buf const &buf) : buf(buf) {
    }

    inline T &operator[](id<N> idx) const {
        return const_cast<Buf &>(buf)._M_at(idx);
    }
};

template <size_t N>
inline size_t _M_calc_product(range<N> const &size) {
    size_t ret = 1;
    for (int i = 0; i < N; i++) {
        ret *= size[i];
    }
    return ret;
}

template <size_t N>
inline size_t _M_linearize_id(range<N> const &size, id<N> const &idx) {
    size_t ret = 0;
    size_t term = 1;
    for (size_t i = 0; i < N; i++) {
        ret += term * idx[i];
        term *= size[i];
    }
    return ret;
}

template <class T, size_t N>
struct buffer {
    std::vector<T> _M_data;
    range<N> _M_shape;

    buffer() = default;
    buffer(buffer const &) = default;
    buffer &operator=(buffer const &) = default;
    buffer(buffer &&) = default;
    buffer &operator=(buffer &&) = default;

    explicit buffer(range<N> shape)
        : _M_shape(shape), _M_data(_M_calc_product(shape)) {
    }

    template <access::mode mode>
    auto get_access() const {
        return accessor<mode, buffer, T, N>(*this);
    }

    template <access::mode mode>
    auto get_access(handler &) const {
        return accessor<mode, buffer, T, N>(*this);
    }

    range<N> shape() const {
        return _M_shape;
    }

    size_t size() const {
        return _M_calc_product(_M_shape);
    }

    T &_M_at(id<N> idx) {
        return _M_data.at(_M_linearize_id(_M_shape, idx));
    }
};

}
ZENO_NAMESPACE_END

#endif
