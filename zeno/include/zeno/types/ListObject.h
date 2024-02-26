#pragma once

#include <zeno/core/IObject.h>
#include <zeno/funcs/LiterialConverter.h>
#include <vector>
#include <memory>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace zeno {

struct ListObject : IObjectClone<ListObject> {
  std::vector<zany> arr;
  std::map<std::string, std::string> itemidxs;
  glm::mat4 transformMat = glm::mat4(1);

  ListObject() = default;

  explicit ListObject(std::vector<zany> arrin) : arr(std::move(arrin)) {
  }

  template <class T = IObject>
  std::vector<std::shared_ptr<T>> get() const {
      std::vector<std::shared_ptr<T>> res;
      for (auto const &val: arr) {
          res.push_back(safe_dynamic_cast<T>(val));
      }
      return res;
  }

  template <class T = IObject>
  std::vector<T *> getRaw() const {
      std::vector<T *> res;
      for (auto const &val: arr) {
          res.push_back(safe_dynamic_cast<T>(val.get()));
      }
      return res;
  }

  template <class T>
  std::vector<T> get2() const {
      std::vector<T> res;
      for (auto const &val: arr) {
          res.push_back(objectToLiterial<T>(val));
      }
      return res;
  }

  template <class T>
  [[deprecated("use get2<T>() instead")]]
  std::vector<T> getLiterial() const {
      return get2<T>();
  }
};

}
