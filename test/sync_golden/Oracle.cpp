#include <ChapterXPathResolver.h>
#include <Epub/Section.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <ProgressComparison.h>
#include <ProgressMapper.h>
#include <SecureHttpClient.h>

#include <iostream>
#include <limits>

static void line(const std::string& s) {
  for (unsigned char c : s) std::cout << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
  std::cout << '\n';
}
static void dump(const std::vector<std::string>& values) {
  for (const auto& s : values) line(s);
}
static void position(const CrossPointPosition& p) {
  std::cout << p.spineIndex << ',' << p.pageNumber << ',' << p.totalPages << ',' << p.visibleTextOffset << ','
            << p.hasVisibleTextOffset << ',' << p.paragraphIndex << ',' << p.hasParagraphIndex << ',' << p.liIndex
            << ',' << p.hasLiIndex << ',' << p.hasResolvedSpineIndex << ',' << p.hasMappedPage << ':';
  line(p.xpathAnchorId);
}
int main() {
  logging_stub::Capture logs;
  auto& store = KOREADER_STORE;
  for (const std::string path : {"", "/", "book.epub", "/folder/é中.epub", "a//book.epub", "\\book.epub"})
    line(KOReaderDocumentId::calculateFromFilename(path));
  for (size_t size : {0u, 1u, 1023u, 1024u, 1025u, 2048u, 4097u, 16385u, 1073741825u}) {
    for (int seek : {-1, 0, 1024})
      for (size_t limit : {0u, 7u, 1024u}) {
        fake::fileSize = size;
        fake::failSeek = seek;
        fake::readLimit = limit;
        fake::fileTrace.clear();
        line(KOReaderDocumentId::calculate("/book.epub"));
        dump(fake::fileTrace);
      }
  }
  fake::openOk = false;
  line(KOReaderDocumentId::calculate("missing"));
  fake::openOk = true;
  for (const char* config :
       {"{}", "null", "[]", "{\"username\":\"user\",\"password\":\"pass\"}",
        "{\"cfgVersion\":2,\"username\":\"u\",\"password\":\"p\",\"serverUrl\":\"local///\"}",
        "{\"cfgVersion\":3,\"serverType\":99,\"matchMethod\":255,\"syncBehavior\":5}",
        "{\"cfgVersion\":3,\"serverType\":\"bad\",\"matchMethod\":true,\"syncBehavior\":false}",
        "{\"cfgVersion\":256,\"headers\":[{\"name\":\"A\",\"value_obf\":\"!!!\"},{\"name\":\"B\"},{\"name\":\"C\"}]}",
        "{\"cfgVersion\":3,\"serverType\":2,\"sendMetadata\":true,\"syncClippings\":true,\"syncBehavior\":1}"}) {
    JsonDocument in;
    deserializeJson(in, config);
    fake::resaves = 0;
    std::cout << store.fromJson(in.as<JsonVariantConst>()) << ',' << fake::resaves << '\n';
    store.saveToFile();
    line(fake::persisted);
    line(store.getBaseUrl());
    line(store.getMd5Password());
    JsonDocument round;
    deserializeJson(round, fake::persisted);
    fake::resaves = 0;
    store.fromJson(round.as<JsonVariantConst>());
    store.saveToFile();
    line(fake::persisted);
    std::cout << fake::resaves << '\n';
  }
  for (const char* url :
       {"", "local/", "http://host///", "https://sync.crosspointreader.com/", "/", "://", "HTTP://HOST"}) {
    store.setServerUrl(url);
    line(store.getBaseUrl());
  }
  store.setCredentials("user:é", "p\"ass");
  store.setServerUrl("host/");
  store.setCustomHeader(0, "x-custom", "value");
  store.setCustomHeader(1, "x-auth-user", "override");
  KOReaderProgress upload{};
  upload.document = "doc";
  upload.progress = "/body/DocFragment[1]/body/p[1]/text().2";
  upload.percentage = 0.1234567f;
  upload.metadata = KOReaderMetadata{"name.epub",
                                     "title",
                                     "author",
                                     "{\"title\":\"bad\",\"id\":42,\"flag\":true,\"empty\":null,\"array\":[],\"obj\":{}"
                                     ",\"string\":\"é\\n\",\"isbn\":\"extra-isbn\"}",
                                     "isbn",
                                     "asin",
                                     "series",
                                     1.25f};
  upload.position = KOReaderRichPosition{123456, 2, 3, 7, 4, "/body"};
  for (int profile = 0; profile < 3; ++profile) {
    store.setServerType(static_cast<KOReaderServerType>(profile));
    for (int code : {-1, 0, 199, 200, 201, 204, 299, 300, 401, 402, 404, 500})
      for (int method = 0; method < 4; ++method) {
        fake::httpTrace.clear();
        fake::httpCode = code;
        fake::response =
            "{\"progress\":\"/"
            "body\",\"percentage\":0.25,\"device\":\"Other\",\"device_id\":\"id\",\"timestamp\":1234567890123,"
            "\"position\":{\"pctQ\":1,\"spine\":2,\"page\":3,\"pages\":0,\"para\":4,\"xpath\":\"/body\"}}";
        KOReaderProgress out{};
        int error = method == 0   ? KOReaderSyncClient::authenticate()
                    : method == 1 ? KOReaderSyncClient::createUser()
                    : method == 2 ? KOReaderSyncClient::getProgress("doc", out)
                                  : KOReaderSyncClient::updateProgress(upload);
        std::cout << error << ',' << KOReaderSyncClient::lastHttpCode << '\n';
        dump(fake::httpTrace);
        line(out.document);
        line(out.progress);
        line(out.device);
        line(out.deviceId);
        std::cout << out.percentage << ',' << out.timestamp << ',' << out.position.has_value() << '\n';
        if (out.position) {
          auto& r = *out.position;
          std::cout << r.pctQ << ',' << r.spineIndex << ',' << r.pageNumber << ',' << r.totalPages << ','
                    << r.paragraphIndex.value_or(0) << '\n';
          line(r.xpath);
        }
      }
  }
  fake::httpCode = 200;
  for (const char* response : {"", "{", "null", "[]", "{}",
                               "{\"progress\":42,\"percentage\":\"0.3\",\"position\":{\"pages\":-1,\"para\":65536}}"}) {
    fake::response = response;
    KOReaderProgress p{};
    std::cout << KOReaderSyncClient::getProgress("doc", p) << '\n';
    line(p.progress);
    std::cout << p.percentage << '\n';
  }
  for (int failure = 0; failure < 4; ++failure) {
    fake::heap = failure == 0 ? 34999 : 35000;
    fake::block = failure == 1 ? 19999 : 20000;
    fake::beginOk = failure != 2;
    if (failure == 3) store.clearCredentials();
    fake::httpTrace.clear();
    KOReaderProgress p{};
    std::cout << KOReaderSyncClient::authenticate() << ',' << KOReaderSyncClient::createUser() << ','
              << KOReaderSyncClient::getProgress("doc", p) << ',' << KOReaderSyncClient::updateProgress(upload) << '\n';
    dump(fake::httpTrace);
  }
  GfxRenderer renderer;
  const std::vector<std::string> chapters = {
      "<html><head><title>skip</title></head><body>  raw<p>one &amp; two&#65; é</p><div><p>other\r\ntext<a "
      "id='anchor'/>end</p></div><ul><li id='anchor'>first</li><li>second</li></ul><img "
      "src='a/b'/><script>skip</script> tail</body></html>",
      "<html><body><section><p>last</p></section></body></html>"};
  auto epub = std::make_shared<Epub>(chapters);
  for (int cache = 0; cache < 5; ++cache) {
    fake::cacheMode = cache;
    for (const char* path :
         {"", "/body/DocFragment[1]", "/body/DocFragment[1]/body", "/body/DocFragment[1].00",
          "/body/DocFragment[1]/body/p[1]/text().3", "/body/DocFragment[1]/body/p[2]/text()[2].0",
          "/body/DocFragment[1]/body/div/p/text().2", "/body/DocFragment[1]/body/ul/li[2]/text().2",
          "/body/DocFragment[1]/body/img.0", "/body/DocFragment[1]/body/text().2",
          "/body/DocFragment[2]/body/section/p[1]/text().1", "/body/DocFragment[99]/body/p[1]/text().1",
          "/p[1]/text().2", "/body/DocFragment[1]/body/p[-1]/text().1"})
      for (float pct : {-1.0f, 0.0f, 0.2f, 0.9f, 1.0f, 2.0f})
        for (int current : {-1, 0, 1})
          position(ProgressMapper::toCrossPoint(epub, {path, pct}, renderer, current, 9, 5));
    for (int spine : {0, 1, 9})
      for (int pages : {0, 1, 7, 11})
        for (bool tried : {false, true}) {
          KOReaderRichPosition rich{
              300000, uint16_t(spine), 3, uint16_t(pages), 2, "/body/DocFragment[1]/body/p[1]/text().2"};
          auto p = ProgressMapper::fromRichPosition(epub, rich, renderer, tried);
          std::cout << p.has_value() << '\n';
          if (p) position(*p);
        }
  }
  for (uint32_t offset = 0; offset < 80; offset += 3) {
    CrossPointPosition p{0, 3, 11};
    p.hasVisibleTextOffset = true;
    p.visibleTextOffset = offset;
    auto s = ProgressMapper::toSavedProgress(epub, p);
    line(s.xpath);
    std::cout << s.percentage << '\n';
  }
  for (unsigned flags = 0; flags < 64; ++flags)
    for (int a : {-1, 0, 1})
      for (int b : {-1, 0, 1}) {
        CrossPointPosition l{a, a, 10}, r{b, b, 10};
        l.hasResolvedSpineIndex = flags & 1;
        r.hasResolvedSpineIndex = flags & 2;
        l.hasMappedPage = flags & 4;
        r.hasMappedPage = flags & 8;
        l.hasVisibleTextOffset = flags & 16;
        r.hasVisibleTextOffset = flags & 32;
        l.visibleTextOffset = a + 1;
        r.visibleTextOffset = b + 1;
        for (float pct : {0.001f, 0.0011f, std::numeric_limits<float>::quiet_NaN()})
          std::cout << int(compareProgress(l, 0, r, pct)) << int(selectRemoteRecord(l, 0, r, pct)) << '\n';
      }
  for (int error = 0; error < 10; ++error)
    line(KOReaderSyncClient::errorString(static_cast<KOReaderSyncClient::Error>(error)));
  dump(logs.lines);
}
