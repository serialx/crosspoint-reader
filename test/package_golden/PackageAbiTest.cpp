#include <xp_ffi.h>

#include <cassert>
#include <string>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();

using Create = int32_t (*)(const uint8_t*, size_t, XpPkg**);
static int32_t createOpf(const uint8_t* base, size_t size, XpPkg** out) {
  return xp_pkg_opf_create(base, size, 0, 1, out);
}

int main() {
  const std::string label(5000, 'x');
  const std::string inputs[] = {
      "<container><rootfiles><rootfile media-type='application/oebps-package+xml' full-path='" + label +
          "'/></rootfiles></container>",
      "<ncx><navMap><navPoint><navLabel><text>" + label +
          "</text></navLabel><content src='a/../a%20b#x%20y'/></navPoint></navMap></ncx>",
      "<html><body><nav epub:type='toc'><ol><li><a href='a/../a%20b#x%20y'>" + label +
          "</a></li></ol></nav></body></html>",
      "<encryption><EncryptionMethod Algorithm='aes256-cbc'/></encryption>",
      "<package><metadata><title>" + label +
          "</title><creator>A</creator><creator>B</creator><identifier scheme='isbn'>urn:isbn:123</identifier>"
          "<meta property='belongs-to-collection' id='s'>Series</meta>"
          "<meta property='collection-type' refines='#s'>series</meta>"
          "<meta property='group-position' refines='#s'>3.5</meta></metadata>"
          "<manifest><item id='a' href='a/../a%20b' media-type='text/css' properties='nav cover-image'/></manifest>"
          "<spine><itemref idref='a'/></spine><guide><reference type='start' href='a'/></guide></package>"};
  const Create creators[] = {xp_pkg_container_create, xp_pkg_ncx_create, xp_pkg_nav_create, xp_pkg_encryption_create,
                             createOpf};
  for (size_t kind = 0; kind < 5; ++kind) {
    assert(creators[kind](nullptr, 0, nullptr) == -1);
    bool success = false;
    for (size_t limit = 0; limit < 512 && !success; ++limit) {
      xp_test_fail_allocation_after(limit);
      XpPkg* parser = nullptr;
      if (creators[kind](reinterpret_cast<const uint8_t*>("OPS/"), 4, &parser) != 0) {
        assert(parser == nullptr && xp_test_live_allocations() == 0);
        continue;
      }
      size_t consumed = 0;
      assert(xp_pkg_feed(parser, nullptr, 1, 0, &consumed) == -1);
      assert(xp_pkg_feed(parser, nullptr, 0, 2, &consumed) == -1);
      assert(xp_pkg_next(parser, nullptr) == -1);
      size_t offset = 0;
      bool failed = false;
      while (!success && !failed) {
        assert(xp_pkg_feed(parser, reinterpret_cast<const uint8_t*>(inputs[kind].data() + offset),
                           inputs[kind].size() - offset, 1, &consumed) == 0);
        offset += consumed;
        for (;;) {
          XpPkgRecord record{};
          assert(xp_pkg_next(parser, &record) == 0);
          if (record.kind == XP_PKG_ERROR) {
            assert(record.error == 1);
            failed = true;
            break;
          }
          if (record.kind == XP_PKG_DONE) {
            assert(offset == inputs[kind].size());
            success = true;
            break;
          }
          if (record.kind == XP_PKG_NEED_INPUT) {
            assert(consumed != 0);
            break;
          }
        }
      }
      xp_pkg_destroy(parser);
      assert(xp_test_live_allocations() == 0);
    }
    assert(success);
  }
  for (size_t kind = 0; kind < 5; ++kind) {
    bool success = false;
    for (size_t limit = 0; limit < 512 && !success; ++limit) {
      xp_test_fail_allocation_after(limit);
      XpPkgPass* pass = nullptr;
      if (xp_pkg_pass_create(kind + 1, {}, inputs[kind].size(), 0, nullptr, nullptr, nullptr, &pass) == 0) {
        assert(xp_pkg_pass_write(pass, {nullptr, 1}) == 0);
        const auto input = XpPkgSpan{reinterpret_cast<const uint8_t*>(inputs[kind].data()), inputs[kind].size()};
        success = xp_pkg_pass_write(pass, input) == input.len;
        if (kind == 3 && success) success = xp_pkg_pass_finish(pass);
      } else {
        assert(pass == nullptr);
      }
      xp_pkg_pass_destroy(pass);
      assert(xp_test_live_allocations() == 0);
    }
    assert(success);
  }
  xp_pkg_destroy(nullptr);
  xp_test_fail_allocation_after(SIZE_MAX);
}
