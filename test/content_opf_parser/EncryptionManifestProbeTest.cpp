#include <PackageParser.h>
#include <gtest/gtest.h>

#include <string>

TEST(EncryptionManifestProbe, FontOnlyAndEmptyManifestsDoNotNeedProtection) {
  // Same algorithms and references as Foundryside's retained font manifest.
  for (
      const std::string xml : {
          R"(<encryption xmlns="urn:oasis:names:tc:opendocument:xmlns:container" xmlns:enc="http://www.w3.org/2001/04/xmlenc#">
             <enc:EncryptedData><enc:EncryptionMethod Algorithm="http://ns.adobe.com/pdf/enc#RC"/>
             <enc:CipherData><enc:CipherReference URI="OEBPS/Fonts/font00372.otf"/></enc:CipherData></enc:EncryptedData>
             <enc:EncryptedData><enc:EncryptionMethod Algorithm="http://ns.adobe.com/pdf/enc#RC"/>
             <enc:CipherData><enc:CipherReference URI="OEBPS/Fonts/font00373.otf"/></enc:CipherData></enc:EncryptedData>
           </encryption>)",
          R"(<encryption><EncryptedData><EncryptionMethod Algorithm="http://www.idpf.org/2008/embedding"/>
             <CipherData><CipherReference URI="font.otf"/></CipherData></EncryptedData></encryption>)",
          "<encryption/>", R"(<encryption><!-- <EncryptionMethod Algorithm="aes128-cbc"/> --></encryption>)"}) {
    PackageParser probe(XP_PKG_ENCRYPTION, "", 0, "");
    ASSERT_TRUE(probe.setup());
    for (const unsigned char byte : xml) ASSERT_EQ(probe.write(byte), 1u);
    ASSERT_TRUE(probe.finish());
    EXPECT_FALSE(probe.needsProtectionCheck());
  }
}

TEST(EncryptionManifestProbe, AesStillRequiresSdkValidation) {
  PackageParser probe(XP_PKG_ENCRYPTION, "", 0, "");
  ASSERT_TRUE(probe.setup());
  const std::string xml = R"(<encryption xmlns:e="http://www.w3.org/2001/04/xmlenc#"><e:EncryptedData>
    <e:EncryptionMethod Algorithm = 'http://www.w3.org/2001/04/xmlenc#aes128-cbc'/>
    <e:CipherData><e:CipherReference URI="chapter.xhtml"/></e:CipherData></e:EncryptedData></encryption>)";
  for (const unsigned char byte : xml) ASSERT_EQ(probe.write(byte), 1u);
  ASSERT_TRUE(probe.finish());
  EXPECT_TRUE(probe.needsProtectionCheck());
}

TEST(EncryptionManifestProbe, Aes256StillRequiresSdkValidation) {
  PackageParser probe(XP_PKG_ENCRYPTION, "", 0, "");
  ASSERT_TRUE(probe.setup());
  const std::string xml = R"(<encryption xmlns:e="http://www.w3.org/2001/04/xmlenc#"><e:EncryptedData>
    <e:EncryptionMethod Algorithm = 'http://www.w3.org/2001/04/xmlenc#aes256-cbc'/>
    <e:CipherData><e:CipherReference URI="chapter.xhtml"/></e:CipherData></e:EncryptedData></encryption>)";
  for (const unsigned char byte : xml) ASSERT_EQ(probe.write(byte), 1u);
  ASSERT_TRUE(probe.finish());
  EXPECT_TRUE(probe.needsProtectionCheck());
}

TEST(EncryptionManifestProbe, TruncatedManifestFailsValidation) {
  PackageParser probe(XP_PKG_ENCRYPTION, "", 0, "");
  ASSERT_TRUE(probe.setup());
  const std::string xml = "<encryption><EncryptedData>";
  ASSERT_EQ(probe.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_FALSE(probe.finish());
}
