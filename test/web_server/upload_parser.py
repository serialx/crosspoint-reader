"""Exercise the patched Arduino dispatch sequence with an in-memory client and an event recorder."""
from pathlib import Path
import hashlib, json, os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
framework=Path(sys.argv[1]).resolve()
handler=Path(sys.argv[2]).resolve()
source=framework/'libraries/WebServer/src/Parsing.cpp'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='522a46a1b8bed19b5482b65eb96bb87fe068c937c4516d17111179b2e8b88adc'
s=subprocess.check_output(['patch','-s','-o','-',str(source),str(root/'scripts/webserver-parser.patch')]).decode()
def method(sig, source=s):
    begin=source.index(sig)
    return source[begin:source.index('\n}',begin)+2]
with tempfile.TemporaryDirectory() as directory:
    work=Path(directory)
    w=(root/'test/web_server/stubs/WString.h').read_text().replace(' private:', '''
  char charAt(size_t pos) const { return pos < size() ? at(pos) : 0; }
  bool concat(char c) { return concat(&c, 1); }
  bool concat(const String& value) { return concat(value.c_str(), value.size()); }
  bool equalsIgnoreCase(const char* p) const { return strcasecmp(c_str(), p) == 0; }
  void replace(const char* from, const char*) { erase(std::remove(begin(), end(), *from), end()); }
 private:''')
    (work/'WString.h').write_text(w)
    pre=r'''
#include <cassert>
#include <memory>
#include <iostream>
#include <vector>
#include <strings.h>
#include "UploadIngress.h"
#define log_v(...)
#define log_e(...)
#define F(x) x
#define FPSTR(x) x
#define HTTP_UPLOAD_BUFLEN 8
#define HTTP_RAW_BUFLEN 8
#define HTTP_MAX_POST_WAIT 1
#define WEBSERVER_MAX_POST_ARGS 32
const char* Content_Type = "Content-Type";
const char* filename = "filename";
namespace mime { struct Mime {const char* mimeType;}; Mime mimeTable[]={{"text/plain"}}; constexpr int txt=0; }
enum HTTPMethod { HTTP_ANY, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE };
const char* _http_method_str[]={"ANY","POST","PUT","PATCH","DELETE"};
enum {RAW_START,RAW_WRITE,RAW_END,RAW_ABORTED,UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPRaw { int status; size_t totalSize,currentSize; char buf[8]; };
struct HTTPUpload { int status; size_t totalSize,currentSize; String name,filename,type; uint8_t buf[8]; };
struct NetworkClient {
  std::string input,output; size_t cursor=0;
  int read(){return cursor<input.size() ? (unsigned char)input[cursor++] : -1;}
  bool connected() const {return true;}
  size_t write(const uint8_t* b,size_t n){output.append((const char*)b,n); return n;}
  int readBytes(char*,size_t){abort();}
  String readStringUntil(char delimiter){
    String out;
    int c;
    while((c=read())>=0 && c!=delimiter) out.concat(static_cast<char>(c));
    return out;
  }
  void clear(){}
};
char* readBytesWithTimeout(NetworkClient&,size_t,size_t&,int){abort();}
struct WebServer;
struct RequestHandler {
  std::vector<int> events; std::vector<std::string> committed; std::string pending;
  bool canHandle(WebServer&,HTTPMethod,const String&){return true;}
  bool canRaw(WebServer&,const String&){return false;}
  bool canUpload(WebServer&,const String&){return true;}
  RequestHandler* next(){return nullptr;}
  void raw(WebServer&,const String&,HTTPRaw&){abort();}
  std::string directory, overwrite, filename, plugin;
  void upload(WebServer&,const String&,HTTPUpload&);
};
struct WebServer {
  struct RequestArgument { String key,value; RequestArgument* next=nullptr; };
  bool _collectAllHeaders=false,_chunked=false,failDecode=false;
  RequestArgument *_currentHeaders=nullptr,*_currentArgs=nullptr,*_postArgs=nullptr;
  int _currentVersion=0,_clientContentLength=0,_currentArgCount=0,_postArgsLen=0;
  RequestHandler handler, *_firstHandler=&handler,*_currentHandler=nullptr;
  HTTPMethod _currentMethod=HTTP_ANY;
  std::unique_ptr<HTTPRaw> _currentRaw; std::unique_ptr<HTTPUpload> _currentUpload;
  String _currentUri,_hostHeader;
  ~WebServer(){delete[] _currentArgs;delete[] _postArgs;}
  void collectAllHeaders(){abort();}
  void _collectHeader(const char* k,const char*){if(failDecode && strcmp(k,"X-End")==0)String::failAllocationSize=24;}
  void _parseArguments(const String&);
  String urlDecode(const String&);
  bool hasArg(const String&) const;
  String arg(const String&) const;
  int _uploadReadByte(NetworkClient& c){return c.read();}
  void _uploadWriteByte(uint8_t);
  bool _parseFormUploadAborted(), _parseRequest(NetworkClient&), _parseForm(NetworkClient&,const String&,uint32_t);
};
'''
    post=r'''
void RequestHandler::upload(WebServer& web,const String&,HTTPUpload& p){
    events.push_back(p.status);
    if(p.status==UPLOAD_FILE_START) {
      pending.clear();
      directory = web.arg("path");
      overwrite = web.arg("overwrite");
      plugin = web.arg("plugin");
      filename = p.filename;
    }
    if(p.status==UPLOAD_FILE_WRITE)pending.append((const char*)p.buf,p.currentSize);
    if(p.status==UPLOAD_FILE_END)committed.push_back(pending);
  }
void checkRouting(bool reverse, bool blob, bool replace, bool trailing = false, bool crlf = true) {
  WebServer web;
  NetworkClient client;
  std::string body;
  auto field = [&](const std::string& key, const std::string& value) {
    body += "--b\r\nContent-Disposition: form-data; name=\"" + key + "\"\r\n\r\n" + value + "\r\n";
  };
  field("path", "/other");
  field("overwrite", replace ? "0" : "1");
  field("filename", "other.epub");
  std::string filename = blob ? "blob" : "a.epub";
  std::string params = "filename=\"" + filename + "\"";
  if (reverse) params += "; name=\"file\"";
  else params = "name=\"file\"; " + params;
  body += "--b\r\nContent-Disposition: form-data; " + params + "\r\n\r\ncomplete\r\n";
  if (trailing) field("comment", "after");
  body += crlf ? "--b--\r\n" : "--b--";
  std::string query = "path=/books";
  if (blob) query += "&filename=a.epub";
  if (replace) query += "&overwrite=1";
  client.input = "POST /upload?" + query + " HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=b\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\n\r\n" + body;
  assert(web._parseRequest(client));
  const auto& h = web.handler;
  assert(h.committed.size() == 1 && h.committed.front() == "complete");
  assert(h.directory == "/books" && h.filename == "a.epub" && h.overwrite == (replace ? "1" : ""));
  printf("ROUTE\t%d\t%s\t%s\t%s\t%s\n", replace, h.directory.c_str(), h.filename.c_str(),
         h.overwrite.c_str(), h.committed.front().c_str());
}

int main(int argc, char**) {
  if (argc > 1) {
    WebServer web;
    NetworkClient client;
    client.input.assign(std::istreambuf_iterator<char>(std::cin), {});
    assert(web._parseRequest(client));
    auto& h = web.handler;
    assert(h.committed.size() == 1 && h.committed.front() == "complete");
    assert(std::count(h.events.begin(), h.events.end(), UPLOAD_FILE_START) == 1);
    printf("%s\t%s\t%s\t%s\t%s", web._currentUri.c_str(), h.directory.c_str(),
           h.filename.c_str(), h.plugin.c_str(), h.committed.front().c_str());
    return 0;
  }
  for (bool reverse : {false, true})
    for (bool blob : {false, true})
      for (bool replace : {false, true}) checkRouting(reverse, blob, replace);
  for (bool trailing : {false, true})
    for (bool crlf : {false, true}) checkRouting(false, false, true, trailing, crlf);
  const std::string dir(24,'a');
  const std::string fileStart="Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n\r\n";
  const std::string closed="--b\r\n"+fileStart+"first\r\n--b--\r\n";
  for(const std::string mode: {"valid","two","decode-oom","short-first","short-second","bad-suffix","empty","no-crlf","long-request","long-header"}) {
    WebServer web;
    NetworkClient client;
    web.failDecode=mode=="decode-oom";
    std::string body=closed;
    if(mode=="two")body="--b\r\n"+fileStart+"first\r\n--b\r\n"+fileStart+"second\r\n--b--\r\n";
    if(mode=="short-first")body="--b\r\n"+fileStart+"uncommitted\r\n--b";
    if(mode=="short-second")body="--b\r\n"+fileStart+"first\r\n--b\r\n"+fileStart+"incomplete";
    if(mode=="bad-suffix")body="--b\r\n"+fileStart+"unfinished\r\n--bX";
    if(mode=="empty")body="--b\r\n"+fileStart+"\r\n--b--\r\n";
    if(mode=="no-crlf")body.resize(body.size()-2);
    client.input="POST /upload?overwrite=true&path="+dir+" HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=b\r\nContent-Length: "+std::to_string(body.size())+"\r\nX-End: yes\r\n\r\n"+body;
    if(mode=="long-request")client.input.insert(client.input.find(" HTTP"), 8192, 'x');
    if(mode=="long-header")client.input.insert(client.input.find("X-End: "), "X-Long: "+std::string(8192,'x')+"\r\n");
    bool ok=web._parseRequest(client);
    auto& done=web.handler.committed;
    if(mode=="long-request" || mode=="long-header"){
      assert(!ok && web.handler.events.empty());
    } else if(mode=="decode-oom"){
      assert(String::failedAllocations==1 && !ok && web.handler.events.empty());
    } else if(mode=="short-second"){
      assert(!ok && done==std::vector<std::string>{"first"} && web.handler.events.back()==UPLOAD_FILE_ABORTED);
    } else if(mode=="short-first" || mode=="bad-suffix"){
      assert(!ok && done.empty() && web.handler.events.back()==UPLOAD_FILE_ABORTED);
    } else if(mode=="two"){
      assert(ok && done==(std::vector<std::string>{"first","second"}));
    } else {
      assert(ok && done==std::vector<std::string>{mode=="empty"?"":"first"});
    }
    if(!ok)assert(client.output=="HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    printf("%s passed\n",mode.c_str());
  }
}
'''
    def compile_parser(name, parser):
        signatures = ('void WebServer::_parseArguments(', 'String WebServer::urlDecode(', 'bool WebServer::_parseRequest(',
                      'bool WebServer::_parseForm(', 'void WebServer::_uploadWriteByte(', 'bool WebServer::_parseFormUploadAborted(')
        definitions = [method(sig, parser) for sig in signatures]
        lookup = (source.parent / 'WebServer.cpp').read_text()
        definitions.extend(method(sig, lookup) for sig in ('String WebServer::arg(const String', 'bool WebServer::hasArg(const String'))
        unit = work / (name + '.cpp')
        unit.write_text(pre + '\n' + '\n'.join(definitions) + '\n' + post)
        subprocess.run(['c++', '-std=c++20', '-Wno-vla-cxx-extension', '-O1', '-fsanitize=address,undefined',
                        '-I' + str(work), '-I' + str(root/'src/network'), str(unit), '-o', str(work/name)], check=True)

    compile_parser('oracle', s)
    compile_parser('stock', source.read_text())
    for name, plugin, filename, mime, trailing in [
        ('FilesPage', False, 'new.epub', 'application/octet-stream', False),
        ('EPUB optimizer', False, 'new.epub', 'application/epub+zip', False),
        ('curl -F', False, 'new.epub', 'application/octet-stream', False),
        ('browser FormData', False, 'new.epub', 'application/epub+zip', True),
        ('plugin host', True, 'data', 'application/octet-stream', False),
        ('plugin empty override', True, 'blob', 'application/octet-stream', False),
    ]:
        endpoint = '/api/plugin-fs?plugin=test&path=%2Fbooks%2Fnew.epub' if plugin else '/upload?path=%2Fbooks'
        if plugin and filename == 'blob':
            endpoint += '&filename='
        boundary = ('------------------------' if name == 'curl -F' else '----WebKitFormBoundary') + 'abc123'
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\n'
                f'Content-Type: {mime}\r\n\r\ncomplete\r\n')
        if trailing:
            body += f'--{boundary}\r\nContent-Disposition: form-data; name="comment"\r\n\r\nafter\r\n'
        body += f'--{boundary}--' + ('\r\n' if name == 'curl -F' else '')
        wire = (f'POST {endpoint} HTTP/1.1\r\nContent-Type: multipart/form-data; boundary={boundary}\r\n'
                f'Content-Length: {len(body)}\r\n\r\n{body}').encode()
        def dispatch(binary):
            return subprocess.check_output([work/binary, 'wire'], input=wire).decode().split('\t')
        current = dispatch('oracle')
        assert current == dispatch('stock'), name
        uri, path, part, owner, payload = current
        assert part == ('' if filename == 'blob' else filename)
        env = dict(os.environ, WEB_CLIENT_URI=uri, WEB_CLIENT_PATH=path, WEB_CLIENT_FILENAME=part,
                   WEB_CLIENT_PLUGIN=owner, WEB_CLIENT_PAYLOAD=payload)
        trace = subprocess.check_output([handler], env=env, text=True)
        record = json.loads(trace)
        assert record['case'] == 'client shape', name
        response = ('response:200:application/json:{"ok":true,"bytes":8}' if plugin else
                    'response:200:text/plain:File uploaded successfully: new.epub')
        assert [event for event in record['events'] if event.startswith('response:')] == [response], name
        print(f'{name}: stock dispatch matches, HTTP 200, complete file')
    result=subprocess.check_output([str(work/'oracle')],text=True)
    for line in result.splitlines():
        if not line.startswith('ROUTE\t'):
            print(line)
            continue
        _, replace, directory, filename, overwrite, payload = line.split('\t')
        env = dict(os.environ, WEB_PARSER_REPLACE=replace, WEB_PARSER_PATH=directory, WEB_PARSER_NAME=filename,
                   WEB_PARSER_OVERWRITE=overwrite, WEB_PARSER_PAYLOAD=payload)
        assert json.loads(subprocess.check_output([handler], env=env))['case'] == 'parsed upload'
    print('12 parser-to-handler routing cases passed')
