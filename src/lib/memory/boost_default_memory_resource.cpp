#include <cstddef>
#include <cstdlib>
#include <iostream>

#include <boost/container/pmr/memory_resource.hpp>
#include <boost/core/no_exceptions_support.hpp>

namespace boost::container::pmr {

class default_resource_impl : public memory_resource {  // NOLINT(readability-identifier-naming)
 public:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    return std::malloc(bytes);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
  }

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
    std::free(p);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
  }

  [[nodiscard]] bool do_is_equal(const memory_resource& other) const BOOST_NOEXCEPT override {
    return &other == this;
  }
};

memory_resource* get_default_resource() BOOST_NOEXCEPT {
  // Yes, this leaks. We have had SO many problems with the default memory resource going out of scope
  // before the other things were cleaned up that we decided to live with the leak, rather than
  // running into races over and over again.
  static auto* default_resource_instance = new default_resource_impl();  // NOLINT(cppcoreguidelines-owning-memory)
  return default_resource_instance;
}

memory_resource* new_delete_resource() BOOST_NOEXCEPT {
  return get_default_resource();
}

memory_resource* set_default_resource(memory_resource* r) BOOST_NOEXCEPT {
  // Do nothing
  return get_default_resource();
}

}  // namespace boost::container::pmr
