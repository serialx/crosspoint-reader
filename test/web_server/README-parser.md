## Updating the Arduino WebServer parser

The firmware builds a local copy of the framework's `libraries/WebServer/src/Parsing.cpp`.
The current source SHA-256 is `522a46a1b8bed19b5482b65eb96bb87fe068c937c4516d17111179b2e8b88adc`.

1. Compare the new framework source with the pinned source before changing the digest in `scripts/patch_webserver.py` and `upload_parser.py`. Replay `scripts/webserver-parser.patch` against the new file and check the multipart callback order.
2. Keep the request and header limits on the exact `POST /upload` route. Its multipart parser checks the query, disposition, Content-Length and delimiter before dispatching file completion. Other multipart routes use the framework header reader, query parser, multipart callback order and form argument merge.
3. Verify the C++ symbol for `WebServer::_parseRequest(NetworkClient&)` if the framework changes its declaration. The link requires an alias defined in the patched translation unit. The ESP compiler CTest configuration includes `web_parser_link_guard`, which links a patched archive and verifies that the stock archive fails.
4. Run the web host oracle and compile its unmutated handler using `test/web_server/extract.py --catalog-off`. Run `python3 test/web_server/upload_parser.py <framework-directory> <oracle-executable>` to compare the checked and stock parser and exercise the firmware handler. Build each shipping board profile affected by the framework update.
