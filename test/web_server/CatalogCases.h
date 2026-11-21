#pragma once
#include "CatalogPhysical.h"

static JsonDocument response(const char* prefix = "response:200:application/json:") {
  for (auto it = host::trace.rbegin(); it != host::trace.rend(); ++it) {
    if (!it->starts_with(prefix)) continue;
    JsonDocument result;
    assert(!deserializeJson(result, it->substr(std::strlen(prefix))));
    return result;
  }
  for (const auto& line : host::trace) std::cerr << line << '\n';
  std::abort();
}
static void upload(CrossPointWebServer& app, const char* family, const char* filename, bool valid) {
  host::trace.clear();
  app.server->args["family"] = family;
  app.server->part.filename = filename;
  app.server->part.status = UPLOAD_FILE_START;
  app.handleFontUploadData();
  const std::string bytes(valid ? "CPFONT\0\0" : "invalid!", 8);
  app.server->part.buf = reinterpret_cast<const uint8_t*>(bytes.data());
  app.server->part.currentSize = bytes.size();
  app.server->part.status = UPLOAD_FILE_WRITE;
  app.handleFontUploadData();
  app.server->part.status = UPLOAD_FILE_END;
  app.handleFontUploadData();
  app.handleFontUpload();
}
int main() {
  CrossPointWebServer app;
  app.server = std::make_unique<CrossPointHttpServer>(80);
  app.wsServer = std::make_unique<WebSocketsServer>();
  app.running = true;
  app.handleStatus();
  const auto status = response();
  const std::string capabilities = "," + (status["pluginCapabilities"] | std::string{}) + ",";
  assert(capabilities.find(",catalog.encoded_item_fields,") != std::string::npos);
  app.handleFontList();
  auto empty = response();
  assert(empty["families"].size() == 0 && empty["maxFamilies"] == 128 && !empty["vectorFonts"].as<bool>());

  host::add("/fonts", "", true);
  host::add("/fonts/Test", "", true);
  host::add("/fonts/Test/Test_12.cpfont", "twelve");
  host::add("/fonts/Test/Test_16.cpfont", "sixteen");
  host::add("/fonts/Test/invalid.txt", "ignored");
  xp_sd_catalog_dirty(sdFontSystem.registry().raw(), XP_SD_MARK_DIRTY, false);
  app.handleFontList();
  auto listed = response();
  assert(listed["families"].size() == 1);
  assert(listed["families"][0]["name"] == "Test");
  assert(listed["families"][0]["sizes"][0] == 12 && listed["families"][0]["sizes"][1] == 16);
  assert(listed["families"][0]["files"].size() == 2);
  assert(listed["families"][0]["files"][0]["size"] == 6);

  host::files.erase("/fonts/Test/Test_16.cpfont");
  xp_sd_catalog_dirty(sdFontSystem.registry().raw(), XP_SD_MARK_DIRTY, false);
  app.handleFontList();
  auto refreshed = response();
  assert(refreshed["families"][0]["sizes"].size() == 1);
  assert(refreshed["families"][0]["files"].size() == 1);

  upload(app, "Test", "Test_18.cpfont", true);
  assert(response()["ok"] == true);
  assert(host::files.at("/fonts/Test/Test_18.cpfont")->bytes == std::string("CPFONT\0\0", 8));
  upload(app, "Test", "Test_20.cpfont", false);
  assert(response("response:400:application/json:")["error"] == "Invalid font file");
  assert(!host::files.contains("/fonts/Test/Test_20.cpfont"));

  host::files.clear();
  upload(app, "New", "New_12.cpfont", true);
  assert(response()["ok"] == true);
  assert(host::files.contains("/.fonts/New/New_12.cpfont"));
  host::trace.clear();
  app.server->args["plain"] = "{\"family\":\"New\"}";
  app.handleFontDelete();
  assert(response()["ok"] == true);
  host::failOperation = "font-delete";
  host::failAfter = 0;
  app.handleFontDelete();
  assert(response("response:500:application/json:")["error"] == "Delete failed");
  std::cout << "shipping catalog web routes: discovery, dirty refresh, upload roots, invalid magic and delete results "
               "passed\n";
}
