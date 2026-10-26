#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/css/CssParser.h>
#include <GfxRenderer.h>
#include <sys/resource.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ChapterTrace.h"
#include "LayoutTrace.h"

namespace {
bool readManifest(Epub& epub, std::vector<std::string>& cssPaths) {
  std::ifstream manifest("manifest.tsv");
  if (!manifest) return false;
  std::string line;
  while (std::getline(manifest, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const auto value = line.substr(tab + 1);
    if (line.starts_with("spine\t"))
      epub.spine.push_back({value});
    else if (line.starts_with("css\t"))
      cssPaths.push_back(value);
    else if (line.starts_with("language\t"))
      epub.language = value;
    else if (line.starts_with("toc\t")) {
      const auto split = value.find('\t');
      if (split == std::string::npos) return false;
      epub.toc.push_back({std::stoi(value.substr(0, split)), value.substr(split + 1)});
    } else
      return false;
  }
  return !epub.spine.empty();
}

ReaderRenderSpec setting(int index) {
  ReaderRenderSpec spec;
  spec.fontId = index == 1 ? 20 : 16;
  spec.viewportWidth = index == 0 ? 480 : index == 1 ? 240 : 640;
  spec.viewportHeight = index == 0 ? 720 : index == 1 ? 320 : 400;
  spec.hyphenationEnabled = index != 0;
  spec.focusReadingEnabled = index == 1;
  spec.extraParagraphSpacing = index == 1;
  spec.paragraphIndentSpaces = index == 2 ? 0 : 2;
  spec.lineCompression = index == 2 ? 1.25f : 1.0f;
  spec.characterSpacing = index == 2 ? 1 : 0;
  spec.wordSpacingPercent = index == 2 ? 125 : 100;
  spec.embeddedStyle = true;
  spec.imageRendering = 0;
  return spec;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 6 && (std::string(argv[1]) == "--read" || std::string(argv[1]) == "--probe")) {
    const bool probe = std::string(argv[1]) == "--probe";
    const rlimit limit{128 * 1024 * 1024, 128 * 1024 * 1024};
    setrlimit(RLIMIT_DATA, &limit);
    const auto source = std::filesystem::absolute(argv[2]);
    std::filesystem::current_path(argv[3]);
    std::filesystem::create_directories("cache/sections");
    std::filesystem::copy_file(source, "cache/sections/0.bin", std::filesystem::copy_options::overwrite_existing);
    auto epub = std::make_shared<Epub>();
    GfxRenderer renderer;
    Section section(epub, 0, renderer);
    const bool accepted = section.loadSectionFile(setting(std::stoi(argv[4])));
    std::cout << "section\t" << accepted << '\t' << section.pageCount << std::endl;
    if (!accepted) return 0;
    const auto hex = [](const std::string& bytes) {
      static constexpr char digits[] = "0123456789abcdef";
      for (const unsigned char byte : bytes) std::cout << digits[byte >> 4] << digits[byte & 15];
    };
    const int pages = std::stoi(argv[5]);
    for (int index = 0; index < pages; ++index) {
      auto page = section.loadPage(index);
      std::cout << "page\t" << index << '\t' << static_cast<bool>(page) << std::endl;
      if (!page) continue;
      if (probe) continue;
      std::cout << "visible\t" << page->visibleTextOffset << '\n';
      HalFile output("page.bin");
      if (!page->serialize(output)) return 10;
      output.close();
      std::ifstream input("page.bin", std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(input)), {});
      std::cout << "wire\t";
      hex(bytes);
      std::cout << '\n';
      section.currentPage = index;
      std::cout << "text\t";
      hex(section.getTextFromSectionFile());
      std::cout << std::endl;
    }
    return 0;
  }
  if (argc != 2) {
    std::cerr << "Usage: SectionGoldenDump <extracted-fixture-directory>\n";
    return 2;
  }
  static_assert(sizeof(int) == 4 && sizeof(float) == 4 && sizeof(bool) == 1);
  std::filesystem::current_path(argv[1]);
  auto epub = std::make_shared<Epub>();
  std::vector<std::string> cssPaths;
  if (!readManifest(*epub, cssPaths)) return 3;
  GfxRenderer renderer;
  for (int config = 0; config < 3; ++config) {
    std::filesystem::remove_all("cache");
    std::filesystem::create_directories("cache");
    CssParser css("cache");
    bool complete = true;
    for (const auto& path : cssPaths) {
      HalFile source;
      if (!source.open(("book/" + path).c_str(), "rb")) return 4;
      const auto result = css.loadFromStream(source);
      if (result == CssParser::ParseResult::Error) return 5;
      complete &= result == CssParser::ParseResult::Complete;
    }
    if (!css.saveToCache(complete)) return 6;
    epub->css = &css;
    const auto spec = setting(config);
    const auto output = "output/" + std::to_string(config);
    std::filesystem::create_directories(output);
    for (size_t spine = 0; spine < epub->spine.size(); ++spine) {
      Section section(epub, static_cast<int>(spine), renderer);
      ChapterTrace::open(output + "/" + std::to_string(spine) + ".ops");
      LayoutTrace::open(output + "/" + std::to_string(spine) + ".layout");
      if (!section.createSectionFile(spec)) {
        std::cerr << "Build failed: setting " << config << ", spine " << spine << ' ' << epub->spine[spine].href
                  << '\n';
        return 7;
      }
      LayoutTrace::close();
      ChapterTrace::close();
      const auto pageCount = section.pageCount;
      if (!section.loadSectionFile(spec) || section.pageCount != pageCount) return 8;
      for (int page = 0; page < pageCount; ++page) {
        if (!section.loadPage(page) || !section.getVisibleTextOffsetForPage(page) ||
            !section.getParagraphIndexForPage(page))
          return 9;
      }
      const auto filename = std::to_string(spine) + ".bin";
      std::filesystem::copy_file("cache/sections/" + filename, output + "/" + filename,
                                 std::filesystem::copy_options::overwrite_existing);
      std::cout << config << '\t' << spine << '\t' << pageCount << '\t'
                << std::filesystem::file_size(output + "/" + filename) << '\n';
    }
  }
}
