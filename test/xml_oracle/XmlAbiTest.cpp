#include <XmlPullParser.h>
#include <xp_ffi.h>

#include <cassert>
#include <cstring>
#include <string>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();

int main() {
  assert(xp_xml_create(nullptr) == -1);
  XpXml* parser = nullptr;
  xp_test_fail_allocation_after(0);
  assert(xp_xml_create(&parser) == -2 && parser == nullptr);
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(xp_xml_create(&parser) == 0);
  size_t consumed = 99;
  assert(xp_xml_feed(parser, nullptr, 1, 0, &consumed) == -1);
  assert(xp_xml_feed(parser, nullptr, 0, 2, &consumed) == -1);
  assert(xp_xml_next(parser, nullptr) == -1);
  xp_xml_stop(parser);
  XpXmlEvent event{};
  assert(xp_xml_next(parser, &event) == 0 && event.kind == XP_XML_STOPPED);
  assert(xp_xml_feed(parser, nullptr, 0, 1, &consumed) == 0 && consumed == 0);
  xp_xml_destroy(parser);
  xp_xml_destroy(nullptr);

  const std::string input = "<!DOCTYPE r [<!ENTITY e 'value'><!ATTLIST r a CDATA 'default'>]><r><long b='" +
                            std::string(4000, 'x') + "'>&e;</long></r>";
  bool reachedSuccess = false;
  for (size_t limit = 0; limit < 128 && !reachedSuccess; ++limit) {
    xp_test_fail_allocation_after(limit);
    parser = nullptr;
    if (xp_xml_create(&parser) != 0) continue;
    size_t offset = 0;
    bool failed = false;
    while (!failed && !reachedSuccess) {
      const size_t remaining = input.size() - offset;
      assert(xp_xml_feed(parser, reinterpret_cast<const uint8_t*>(input.data() + offset), remaining, 1, &consumed) ==
             0);
      offset += consumed;
      for (;;) {
        const auto status = xp_xml_next(parser, &event);
        if (status == -2) {
          failed = true;
          break;
        }
        assert(status == 0);
        if (event.kind == XP_XML_ERROR) {
          assert(event.error == 1);
          failed = true;
          break;
        }
        if (event.kind == XP_XML_START) {
          assert(event.name.data[event.name.len] == 0);
          assert(event.attributes[event.attribute_count * 2] == nullptr);
          if (std::strcmp(event.name.data, "long") == 0) {
            assert(event.attribute_count == 1 && std::strlen(event.attributes[1]) == 4000);
          }
        }
        if (event.kind == XP_XML_DONE) {
          assert(offset == input.size());
          reachedSuccess = true;
          break;
        }
        if (event.kind == XP_XML_NEED_INPUT) {
          assert(consumed != 0);
          break;
        }
      }
    }
    xp_xml_destroy(parser);
    assert(xp_test_live_allocations() == 0);
  }
  assert(reachedSuccess);
  xp_test_fail_allocation_after(SIZE_MAX);
  XmlPullParser facade;
  assert(facade.setup(nullptr, nullptr));
  const auto* document = reinterpret_cast<const uint8_t*>("<r/>");
  assert(facade.write(document, 4, true));
  assert(!facade.write(nullptr, 0, true));
  assert(!facade.write(document, 4, true));
}
