#pike __REAL_VERSION__
#require constant(SSL.Cipher)

//! Dummy HTTPS server/client

#include "tls.h"

#ifndef PORT
#define PORT 25678
#endif

#ifndef CIPHER_BITS
#define CIPHER_BITS	112
#endif

#ifndef RSA_BITS
#define RSA_BITS	4096
#endif

#ifndef DSA_BITS
#define DSA_BITS	2048
#endif

#ifndef KE_MODE
#define KE_MODE		1
#endif

#ifndef HOST
#define HOST	"127.0.0.1"
#endif

class MyContext
{
  inherit SSL.Context;

  SSL.Alert alert_factory(.Connection con,
			  int level, int description,
			  SSL.Constants.ProtocolVersion version,
			  string|void message)
  {
    if (message && description) {
      werror("ALERT [%s: %d:%d]: %s",
	     SSL.Constants.fmt_version(version),
	     level, description, message);
    }
    return ::alert_factory(con, level, description, version, message);
  }
}

#ifndef HTTPS_CLIENT
SSL.Port port;

void my_accept_callback(SSL.File f)
{
  Conn(port->accept());
}
#endif

class Conn (SSL.File sslfile)
{
  string message =
    "HTTP/1.0 200 Ok\r\n"
    "Connection: close\r\n"
    "Content-Length: 132\r\n"
    "Content-Type: text/html; charset=ISO-8859-1\r\n"
    "Date: Thu, 01 Jan 1970 00:00:01 GMT\r\n"
    "Server: Bare-Bones\r\n"
    "\r\n"
    "<html><head><title>SSL-3 server</title></head>\n"
    "<body><h1>This is a minimal SSL-3 http server</h1>\n"
    "<hr><it>/nisse</it></body></html>\n";
  int index = 0;

  void write_callback()
  {
    if (index < sizeof(message))
    {
      int written = sslfile->write(message[index..]);
      if (written > 0)
	index += written;
      else
	sslfile->close();
    }
    if (index == sizeof(message))
      sslfile->close();
  }

  void read_callback(mixed id, string data)
  {
    SSL3_DEBUG_MSG("Received: '" + data + "'\n");
    sslfile->set_write_callback(write_callback);
  }

  protected void create()
  {
    sslfile->set_nonblocking(read_callback, 0, 0);
  }
}

class Client
{
  Stdio.Buffer request = Stdio.Buffer(
    "HEAD / HTTP/1.0\r\n"
    "Host: " HOST ":" + PORT + "\r\n"
    "\r\n");

  void write_cb(SSL.File fd)
  {
    if( request->output_to(fd) < 0 )
      exit(1, "Failed to write data: %s.\n", strerror(fd->errno()));

    if( sizeof(request) ) return;
    fd->set_write_callback(UNDEFINED);
  }

  void got_data(mixed ignored, string data)
  {
    werror("Data: %O\n", data);
  }

  void con_closed()
  {
    exit(0, "Connection closed.\n");
  }

  protected void create(Stdio.File con)
  {
    SSL.Context ctx = MyContext();
    // Make sure all cipher suites are available.
    ctx->preferred_suites = ctx->get_suites(-1, 2);
    werror("Starting\n");
    SSL.File ssl = SSL.File(con, ctx);
    ssl->connect();
    ssl->set_nonblocking(got_data, write_cb, con_closed);
  }
}

string common_name;
void make_certificate(SSL.Context ctx, Crypto.Sign key, void|Crypto.Hash hash)
{
  mapping attrs = ([
    "organizationName" : "Test",
    "commonName" : common_name,
  ]);
  string cert = Standards.X509.make_selfsigned_certificate(key, 3600*24, attrs, 0, hash);
  ctx->add_cert(key, ({ cert }), ({ "*" }));
}

int main()
{
#ifdef HTTPS_CLIENT
  Stdio.File con = Stdio.File();
  if (!con->connect(HOST, PORT))
    exit(1, "Failed to connect to server: %s.\n", strerror(con->errno()));

  Client(con);
  return -1;
#else
  SSL.Context ctx = MyContext();

  Crypto.Sign key;

  common_name = gethostname();
  common_name = (gethostbyname(common_name) || ({ common_name }))[0];
  werror("Common name: %O\n", common_name);

  werror("Generating RSA certificate (%d bits)...\n", RSA_BITS);
  key = Crypto.RSA()->generate_key(RSA_BITS);
  make_certificate(ctx, key);

  // Compat with OLD clients.
  make_certificate(ctx, key, Crypto.SHA1);


  werror("Generating DSA certificate (%d bits)...\n", DSA_BITS);

  catch {
    // NB: Not all versions of Nettle support q sizes other than 160.
    key = Crypto.DSA()->generate_key(DSA_BITS, 256);
    make_certificate(ctx, key);
  };

  // Compat with OLD clients.
  //
  // The old FIPS standard maxed out at 1024 & 160 bits with SHA-1.
  key = Crypto.DSA()->generate_key(1024, 160);
  make_certificate(ctx, key, Crypto.SHA1);

#if constant(Crypto.ECC.Curve)
  werror("Generating ECDSA certificate (%d bits)...\n", 521);

  key = Crypto.ECC.SECP_521R1.ECDSA()->generate_key();
  make_certificate(ctx, key, Crypto.SHA512);
  make_certificate(ctx, key, Crypto.SHA256);

  // Compat with OLD clients.
  //
  // Unlikely to be needed, but the cost is minimal.
  make_certificate(ctx, key, Crypto.SHA1);
#endif

  // Make sure all cipher suites are available.
  ctx->preferred_suites = ctx->get_suites(CIPHER_BITS, KE_MODE);
  ctx->min_version = SSL.Constants.PROTOCOL_SSL_3_0;
  SSL3_DEBUG_MSG("Cipher suites:\n%s",
                 .Constants.fmt_cipher_suites(ctx->preferred_suites));

  SSL3_DEBUG_MSG("Certs:\n%O\n", ctx->get_certificates());

  port = SSL.Port(ctx);

  werror("Starting\n");
  if (!port->bind(PORT, my_accept_callback, NetUtils.ANY))
    exit(1, "Failed to bind port %d.\n", PORT);

  werror("Listening on port %d.\n", PORT);
  return -1;
#endif
}
