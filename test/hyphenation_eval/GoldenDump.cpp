// Capture this tool's output before replacing the C++ engine.
#include "Epub/hyphenation/Hyphenator.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) return 1;
  std::ifstream input(argv[1]);
  if (!input) return 1;
  std::vector<std::string> words;
  std::string hex;
  while (std::getline(input, hex)) {
    if (hex.starts_with('#')) continue;
    std::string word;
    if (hex != "-") {
      for (size_t i = 0; i < hex.size(); i += 2) {
        word.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
      }
    }
    words.push_back(word);
  }
  const char* tags[] = {"en", "fr", "de", "ru", "es", "it", "pl", "sv", "uk", "fi", "pt",
                        "en-US", "pt-BR", "eng", "fra", "fre", "deu", "ger", "rus", "spa", "ita",
                        "ukr", "swe", "fin", "ENG", "FR_ca", "unknown", "", "-en", "por", "pol"};
  for (const char* tag : tags) {
    Hyphenator::setPreferredLanguage(tag);
    for (int fallback = 0; fallback <= 1; ++fallback) {
      for (size_t i = 0; i < words.size(); ++i) {
        std::cout << (*tag ? tag : "-") << ' ' << fallback << ' ' << i;
        for (const auto& br : Hyphenator::breakOffsets(words[i], fallback != 0)) {
          std::cout << ' ' << br.byteOffset << ':' << br.requiresInsertedHyphen;
        }
        std::cout << '\n';
      }
    }
  }
  return std::cout ? 0 : 1;
}
