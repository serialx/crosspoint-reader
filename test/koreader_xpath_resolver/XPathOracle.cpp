#include <ChapterXPathResolver.h>

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 2;
  const int spineIndex = std::stoi(argv[2]);
  std::vector<std::string> contents(spineIndex + 1);
  contents.back().assign(std::istreambuf_iterator<char>(input), {});
  auto epub = std::make_shared<Epub>(std::move(contents));
  epub->testChunkSize = std::stoul(argv[3]);
  char kind;
  std::string argument;
  while (std::cin >> kind >> argument) {
    std::string result;
    switch (kind) {
      case 'p':
        result = ChapterXPathResolver::findXPathForParagraph(epub, spineIndex, std::stoul(argument));
        break;
      case 'o':
        result = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, spineIndex, std::stoul(argument));
        break;
      case 'g':
        result = ChapterXPathResolver::findXPathForProgress(epub, spineIndex, std::stof(argument));
        break;
      default:
        return 2;
    }
    std::cout << result << '\n';
  }
}
