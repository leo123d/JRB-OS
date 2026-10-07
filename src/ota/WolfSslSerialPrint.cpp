// PaperRead: wolfSSL calls this to route its internal trace to the console.
// Upstream defined it in src/network/HttpDownloader.cpp, which was removed with
// the network stack (decision 6). wolfSSL itself is still required for local
// EPUB content-protection decryption (lib/Epub/BookKey.cpp), so the symbol must
// still exist at link time.
#include <Logging.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
