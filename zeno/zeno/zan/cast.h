#pragma once


#include <zeno/zan/transform.h>
#include <vector>
#include <set>


ZENO_NAMESPACE_BEGIN
namespace zan {
inline namespace ns_cast {


template <class T = void>
static constexpr auto cast = transform([] (auto r) {
    if constexpr (std::is_void_v<T>) {
        return range{r.begin(), r.end()};
    } else {
        return T(r.begin(), r.end());
    }
});


static constexpr auto to_vector = transform([] (auto r) {
    using T = std::remove_cvref_t<decltype(*r.begin())>;
    return cast<std::vector<T>>(r);
});


static constexpr auto to_set = transform([] (auto r) {
    using T = std::remove_cvref_t<decltype(*r.begin())>;
    return cast<std::set<T>>(r);
});


}
}
ZENO_NAMESPACE_END
