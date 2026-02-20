/**
 * @file test_url_validator.c
 * @brief Comprehensive test suite for PCRE2-based URL validation
 * @ingroup tests
 *
 * Tests the production-grade URL validation using PCRE2 regex pattern
 * based on Diego Perini's "In search of the perfect URL validation regex"
 *
 * Test categories:
 * - Bynens benchmark suite (should match / should not match)
 * - Bare hostnames (localhost, LAN devices, single-label)
 * - Domains and labels (subdomains, hyphens, underscores)
 * - TLDs (classic, country codes, new gTLDs, unicode/IDN)
 * - Fragments, queries, paths
 * - IPv4 (all valid addresses: public, private, loopback, link-local)
 * - IPv6 (bracketed addresses with zone IDs)
 * - Ports, userinfo, schemes
 * - Unicode/IDN support
 * - Comprehensive rejection cases
 * - Integration tests (real-world URLs)
 */

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>

/* ============================================================================
 * Test Data Structures
 * ============================================================================ */

typedef struct {
  char url[512];
  char note[128];
} url_test_case_t;

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

void cleanup() {
  asciichat_pcre2_cleanup_all();
}

/**
 * Verify that a URL is validated correctly
 */
static void assert_url_valid(const char *url, const char *note) {
  cr_assert(url_is_valid(url), "Expected valid URL: %s %s", url, note ? note : "");
}

/**
 * Verify that a URL is rejected correctly
 */
static void assert_url_invalid(const char *url, const char *note) {
  cr_assert_not(url_is_valid(url), "Expected invalid URL: %s %s", url, note ? note : "");
}

/* ============================================================================
 * Test Suite: Bynens Benchmark - Should Match
 * ============================================================================ */

static url_test_case_t bynens_valid_urls[] = {
    // Basic URLs
    {"http://foo.com/blah_blah", "basic URL"},
    {"http://foo.com/blah_blah/", "with trailing slash"},
    {"http://foo.com/blah_blah_(wikipedia)", "parens in path"},
    {"http://foo.com/blah_blah_(wikipedia)_(again)", "double parens"},

    // Query and userinfo
    {"http://www.example.com/wpstyle/?p=364", "query string"},
    {"https://www.example.com/foo/?bar=baz&inga=42&quux", "complex query"},
    {"http://userid:password@example.com:8080", "userinfo+port"},
    {"http://userid@example.com", "user only"},
    {"http://userid:password@example.com", "user:password"},

    // IPs and fragments
    {"http://142.42.1.1/", "IPv4"},
    {"http://142.42.1.1:8080/", "IPv4+port"},
    {"http://foo.com/blah_(wikipedia)#cite-1", "parens+fragment"},
    {"http://code.google.com/events/#&product=browser", "fragment"},

    // Edge cases
    {"http://j.mp", "short domain"},
    {"http://foo.bar/?q=Test%20URL-encoded%20stuff", "percent-encoded"},
    {"http://1337.net", "numeric subdomain"},
    {"http://a.b-c.de", "hyphenated"},
    {"http://223.255.255.254", "high IPv4"},
    {"https://foo_bar.example.com/", "underscore in host"},
};

ParameterizedTestParameters(url_validator, bynens_valid) {
  return cr_make_param_array(url_test_case_t, bynens_valid_urls, sizeof(bynens_valid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, bynens_valid, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Bynens Benchmark - Should NOT Match
 * ============================================================================ */

static url_test_case_t bynens_invalid_urls[] = {
    // Malformed
    {"http://", "scheme only"},
    {"http://.", "dot only"},
    {"http://..", "double dot"},
    {"http://?", "question mark only"},
    {"http://#", "hash only"},

    // Invalid schemes
    {"//", "no scheme"},
    {"rdar://1234", "wrong scheme"},
    {"ftps://foo.bar/", "ftps not allowed"},

    // Multicast and broadcast
    {"http://224.0.0.1", "224.0.0.1 base multicast"},
    {"http://224.0.0.251", "224.0.0.251 mDNS"},
    {"http://228.1.1.1", "228.x.x.x mid-range multicast"},
    {"http://239.255.255.255", "239.255.255.255 top multicast"},
    {"http://224.1.1.1", "multicast"},
    {"http://255.255.255.255", "broadcast"},

    // Invalid format
    {"http://foo.bar/foo(bar)baz quux", "space in path"},
    {"http://-error-.invalid/", "leading hyphen"},
    {"http://1.1.1.1.1", "five octets"},
    {"http://123.123.123", "three octets"},
};

ParameterizedTestParameters(url_validator, bynens_invalid) {
  return cr_make_param_array(url_test_case_t, bynens_invalid_urls,
                             sizeof(bynens_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, bynens_invalid, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Bare Hostnames (localhost, LAN devices)
 * ============================================================================ */

static url_test_case_t bare_hostname_urls[] = {
    // Localhost
    {"http://localhost", "localhost"},
    {"http://localhost/", "localhost+slash"},
    {"http://localhost:3000", "localhost+port"},
    {"http://localhost:8080/api/v1", "localhost+port+path"},
    {"https://localhost:443/path#frag", "localhost+https+frag"},

    // LAN devices
    {"http://myserver", "bare hostname"},
    {"http://raspberrypi", "LAN host"},
    {"http://nas/files/movie.mkv", "NAS path"},
    {"http://printer:631", "CUPS port"},
    {"http://db:5432", "postgres port"},
    {"http://redis:6379/0", "redis db0"},

    // K8s
    {"http://k8s-service:80/healthz", "k8s service name"},
    {"http://a", "single-char hostname"},
    {"http://z", "single-char z"},
};

ParameterizedTestParameters(url_validator, bare_hostnames) {
  return cr_make_param_array(url_test_case_t, bare_hostname_urls, sizeof(bare_hostname_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, bare_hostnames) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Domains and Labels
 * ============================================================================ */

static url_test_case_t domain_urls[] = {
    // Basic
    {"http://example.com", "basic domain"},
    {"https://example.com/", "basic+slash"},
    {"http://www.example.com", "www prefix"},
    {"http://a.b", "minimal two-label"},
    {"http://x.co", "two-char TLD"},

    // Subdomains
    {"http://a.b.c.d.e.com", "5 subdomains"},
    {"http://a.b.c.d.e.f.g.h.example.com", "8 subdomains"},
    {"http://sub1.sub2.sub3.example.co.uk", "deep+country"},

    // Hyphens and underscores
    {"http://my-server.com", "hyphen in label"},
    {"http://a-b.c-d.com", "hyphens everywhere"},
    {"http://my_server.com", "underscore in label"},
    {"http://foo_bar.baz_qux.com", "multiple underscores"},
    {"http://_dmarc.example.com", "leading underscore"},

    // Numeric labels
    {"http://123abc.com", "digits then alpha"},
    {"http://abc123.com", "alpha then digits"},
    {"http://1337.net", "all-numeric label"},
    {"http://1.2.3.example.com", "numeric subdomains"},
    {"http://007.bond.com", "leading zeros in label"},

    // FQDN trailing dot
    {"http://example.com.", "FQDN trailing dot"},
    {"http://www.example.com.", "FQDN www"},
};

ParameterizedTestParameters(url_validator, domains) {
  return cr_make_param_array(url_test_case_t, domain_urls, sizeof(domain_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, domains, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: TLDs (gTLDs, ccTLDs, IDN)
 * ============================================================================ */

static url_test_case_t tld_urls[] = {
    // Classic gTLDs
    {"http://example.com", ".com"},
    {"http://example.net", ".net"},
    {"http://example.org", ".org"},
    {"http://example.edu", ".edu"},
    {"http://example.gov", ".gov"},

    // Country codes
    {"http://example.uk", ".uk"},
    {"http://example.de", ".de"},
    {"http://example.jp", ".jp"},
    {"http://example.io", ".io"},
    {"http://example.co", ".co"},
    {"http://example.ru", ".ru"},

    // Compound country
    {"http://example.co.uk", ".co.uk"},
    {"http://example.co.jp", ".co.jp"},
    {"http://example.com.au", ".com.au"},
    {"http://example.org.uk", ".org.uk"},

    // New gTLDs
    {"http://example.app", ".app"},
    {"http://example.dev", ".dev"},
    {"http://example.blog", ".blog"},
    {"http://example.cloud", ".cloud"},
    {"http://example.shop", ".shop"},
    {"http://example.xyz", ".xyz"},

    // Unicode/IDN
    {"https://例子.测试", "Chinese .测试"},
    {"https://пример.рф", "Russian .рф"},
    {"https://münchen.de", "German umlaut"},
    {"https://café.com", "French accent"},
    {"https://日本語.jp", "Japanese IDN"},
};

ParameterizedTestParameters(url_validator, tlds) {
  return cr_make_param_array(url_test_case_t, tld_urls, sizeof(tld_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, tlds) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Fragments
 * ============================================================================ */

static url_test_case_t fragment_urls[] = {
    {"http://example.com#", "empty fragment"},
    {"http://example.com#top", "simple fragment"},
    {"http://example.com#section", "section"},
    {"http://example.com#section-1", "fragment with hyphen"},
    {"http://example.com#section_two", "fragment with underscore"},
    {"http://example.com#section/sub", "fragment with slash"},
    {"http://example.com#L42", "GitHub line fragment"},
    {"http://example.com#:~:text=hello", "Chrome text fragment"},
};

ParameterizedTestParameters(url_validator, fragments) {
  return cr_make_param_array(url_test_case_t, fragment_urls, sizeof(fragment_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, fragments, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Queries
 * ============================================================================ */

static url_test_case_t query_urls[] = {
    {"http://example.com?q=1", "simple query"},
    {"http://example.com?", "empty query"},
    {"http://example.com?a=1&b=2", "multi-param"},
    {"http://example.com?key=", "empty value"},
    {"http://example.com?q=hello%20world", "percent-encoded space"},
    {"http://example.com?a[0]=1&a[1]=2", "array params"},
    {"http://example.com?redirect=http://other.com", "URL in query"},
};

ParameterizedTestParameters(url_validator, queries) {
  return cr_make_param_array(url_test_case_t, query_urls, sizeof(query_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, queries) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Paths
 * ============================================================================ */

static url_test_case_t path_urls[] = {
    {"http://example.com/", "root path"},
    {"http://example.com/path", "simple path"},
    {"http://example.com/a/b/c/d", "deep path"},
    {"http://example.com/page.html", "file extension"},
    {"http://example.com/%E4%B8%AD%E6%96%87", "percent-encoded path"},
    {"http://example.com/(parens)/in/path", "parens in path"},
    {"http://example.com/~user", "tilde in path"},
    {"http://example.com/@user", "at in path"},
    {"http://example.com/path,with,commas", "commas in path"},
};

ParameterizedTestParameters(url_validator, paths) {
  return cr_make_param_array(url_test_case_t, path_urls, sizeof(path_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, paths, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: IPv4 - Valid Addresses
 * ============================================================================ */

static url_test_case_t ipv4_valid_urls[] = {
    // Public valid
    {"http://1.1.1.1", "Cloudflare DNS"},
    {"http://8.8.8.8", "Google DNS"},
    {"http://142.42.1.1", "mid-range"},
    {"http://223.255.255.254", "high end"},
    {"http://100.64.0.1", "CGNAT range"},

    // With port
    {"http://1.1.1.1:80", "IPv4+port 80"},
    {"http://8.8.8.8:443", "IPv4+port 443"},
    {"http://1.1.1.1:8080", "IPv4+port 8080"},
    {"http://142.42.1.1:65535", "IPv4+port max"},

    // With path
    {"http://1.1.1.1/", "IPv4+root"},
    {"http://1.1.1.1/path", "IPv4+path"},
    {"http://8.8.8.8/dns-query", "IPv4+api path"},

    // Private 10.0.0.0/8
    {"http://10.0.0.0", "10.0.0.0 network"},
    {"http://10.0.0.1", "10.0.0.1"},
    {"http://10.1.1.1", "10.1.1.1"},
    {"http://10.255.255.254", "10.255.255.254"},
    {"http://10.255.255.255", "10.255.255.255"},

    // Private 172.16.0.0/12
    {"http://172.16.0.0", "172.16.0.0 network"},
    {"http://172.16.0.1", "172.16.0.1"},
    {"http://172.20.0.1", "172.20 mid-range"},
    {"http://172.31.255.254", "172.31 top"},

    // Private 192.168.0.0/16
    {"http://192.168.0.0", "192.168.0.0 network"},
    {"http://192.168.0.1", "192.168.0.1"},
    {"http://192.168.1.1", "192.168.1.1"},
    {"http://192.168.255.254", "192.168.255.254"},

    // Loopback 127.0.0.0/8
    {"http://127.0.0.0", "127.0.0.0"},
    {"http://127.0.0.1", "127.0.0.1 loopback"},
    {"http://127.0.0.2", "127.0.0.2"},
    {"http://127.1.1.1", "127.1.1.1"},
    {"http://127.255.255.254", "127.255.255.254"},

    // Link-local 169.254.0.0/16
    {"http://169.254.0.0", "169.254.0.0"},
    {"http://169.254.0.1", "169.254.0.1"},
    {"http://169.254.1.1", "169.254.1.1"},
    {"http://169.254.255.254", "169.254.255.254"},
};

ParameterizedTestParameters(url_validator, ipv4_valid) {
  return cr_make_param_array(url_test_case_t, ipv4_valid_urls, sizeof(ipv4_valid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ipv4_valid) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: IPv6 - Valid Addresses
 * ============================================================================ */

static url_test_case_t ipv6_urls[] = {
    // Standard forms
    {"http://[2001:0db8:85a3:0000:0000:8a2e:0370:7334]", "full IPv6"},
    {"http://[2001:db8:85a3::8a2e:370:7334]", "compressed"},
    {"http://[2001:db8::1]", "short compressed"},
    {"http://[::1]", "loopback"},
    {"http://[fe80::1]", "link-local"},

    // IPv4-mapped
    {"http://[::ffff:192.168.1.1]", "IPv4-mapped"},
    {"http://[::ffff:10.0.0.1]", "IPv4-mapped private"},
    {"http://[::ffff:127.0.0.1]", "IPv4-mapped loopback"},

    // Zone ID
    {"https://[fe80::1%25eth0]/", "zone ID eth0"},
    {"https://[fe80::1%25en0]/", "zone ID en0"},
    {"https://[fe80::1%25wlan0]/", "zone ID wlan0"},

    // With port and path
    {"http://[::1]:80", "IPv6+port 80"},
    {"http://[::1]:8080", "IPv6+port 8080"},
    {"http://[::1]/", "IPv6+root"},
    {"http://[::1]/path", "IPv6+path"},
    {"http://[::1]:8080/path?q=1#frag", "IPv6+all"},
};

ParameterizedTestParameters(url_validator, ipv6) {
  return cr_make_param_array(url_test_case_t, ipv6_urls, sizeof(ipv6_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ipv6, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: IPv6 - Invalid (Unbracketed)
 * ============================================================================ */

static url_test_case_t ipv6_invalid_urls[] = {
    {"http://2001:db8::1", "unbracketed IPv6"},
    {"http://::1", "unbracketed loopback"},
    {"http://fe80::1", "unbracketed link-local"},
    {"http://ff02::1", "unbracketed multicast"},
    {"http://[2001:db8::1", "missing closing bracket"},
    {"http://2001:db8::1]", "missing opening bracket"},
    {"http://[2001:db8::/32]", "CIDR notation not allowed"},
    {"http://[2001:db8::xyz]", "invalid hex characters"},
};

ParameterizedTestParameters(url_validator, ipv6_invalid) {
  return cr_make_param_array(url_test_case_t, ipv6_invalid_urls, sizeof(ipv6_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ipv6_invalid, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Ports
 * ============================================================================ */

static url_test_case_t port_urls[] = {
    {"http://example.com:1", "port 1"},
    {"http://example.com:22", "SSH"},
    {"http://example.com:80", "HTTP"},
    {"http://example.com:443", "HTTPS"},
    {"http://example.com:8080", "alt HTTP"},
    {"http://example.com:65535", "max port"},
    {"http://example.com:8080/", "port+root"},
    {"http://example.com:8080/path", "port+path"},
    {"http://example.com:8080?q=1", "port+query"},
    {"http://example.com:8080/p?q=1#s", "port+all"},
};

ParameterizedTestParameters(url_validator, ports_valid) {
  return cr_make_param_array(url_test_case_t, port_urls, sizeof(port_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ports_valid, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Ports - Invalid
 * ============================================================================ */

static url_test_case_t port_invalid_urls[] = {
    {"http://example.com:", "colon no port"},      {"http://example.com:abc", "non-numeric port"},
    {"http://example.com:-8080", "negative port"}, {"http://example.com:8080a", "alphanumeric port"},
    {"http://example.com:-1", "negative port"},    {"http://example.com:8a", "hex port"},
    {"http://example.com:8.0", "float port"},      {"http://example.com:80 ", "space after port"},
};

ParameterizedTestParameters(url_validator, ports_invalid) {
  return cr_make_param_array(url_test_case_t, port_invalid_urls, sizeof(port_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ports_invalid) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Userinfo
 * ============================================================================ */

static url_test_case_t userinfo_urls[] = {
    {"http://user@example.com", "user only"},
    {"http://user:pass@example.com", "user:pass"},
    {"http://user:p%40ss@example.com", "encoded @ in pass"},
    {"http://user:@example.com", "empty password"},
    {"http://user@localhost", "user@bare"},
    {"http://user@localhost:3000", "user@bare+port"},
    {"http://user@1.1.1.1", "user@IPv4"},
    {"http://user@[::1]", "user@IPv6"},
    {"http://user:pass@myserver:8080/", "user:pass@LAN+port"},
};

ParameterizedTestParameters(url_validator, userinfo) {
  return cr_make_param_array(url_test_case_t, userinfo_urls, sizeof(userinfo_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, userinfo) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Schemes
 * ============================================================================ */

static url_test_case_t scheme_valid_urls[] = {
    {"http://example.com", "http"},           {"https://example.com", "https"},
    {"HTTP://EXAMPLE.COM", "HTTP uppercase"}, {"HTTPS://EXAMPLE.COM", "HTTPS uppercase"},
    {"Http://Example.com", "mixed case"},
};

ParameterizedTestParameters(url_validator, schemes_valid) {
  return cr_make_param_array(url_test_case_t, scheme_valid_urls, sizeof(scheme_valid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, schemes_valid, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

static url_test_case_t scheme_invalid_urls[] = {
    {"ftp://example.com", "ftp"},
    {"ftps://example.com", "ftps"},
    {"sftp://example.com", "sftp"},
    {"ws://example.com", "websocket"},
    {"wss://example.com", "websocket secure"},
    {"file:///path/to/file", "file"},
    {"ssh://example.com", "ssh"},
    {"git://example.com/repo", "git"},
    {"scp://example.com", "scp"},
    {"svn://example.com/repo", "svn"},
    {"data:text/html", "data URI"},
    {"blob:http://example.com/uuid", "blob scheme"},
    {"mailto:user@example.com", "mailto"},
    {"tel:+1234567890", "tel"},
    {"magnet:?xt=urn:btih", "magnet"},
    {"javascript:alert(1)", "javascript"},
    {"about:blank", "about"},
    {"chrome://settings", "chrome internal"},
    {"file://localhost/path", "file local"},
    {"news:comp.lang.c", "news"},
    {"htp://example.com", "typo: htp"},
    {"http2://example.com", "http2"},
    {"http/1.1://example.com", "versioned scheme"},
    {"http-s://example.com", "hyphenated scheme"},
    {"http+secure://example.com", "http+secure"},
};

ParameterizedTestParameters(url_validator, schemes_invalid) {
  return cr_make_param_array(url_test_case_t, scheme_invalid_urls,
                             sizeof(scheme_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, schemes_invalid, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Malformed URLs
 * ============================================================================ */

static url_test_case_t malformed_urls[] = {
    {"", "empty string"},
    {" ", "space"},
    {"   ", "multiple spaces"},
    {"\t", "tab"},
    {"\n", "newline"},
    {"not a url", "plain text"},
    {"just some words here", "sentence"},
    {"http", "bare scheme word"},
    {"http://", "scheme+two slashes only"},
    {"http://.", "dot only"},
    {"http://..", "double dot only"},
    {"http://...", "triple dot"},
    {"http://?", "question mark only"},
    {"http://#", "hash only"},
    {"http/example.com", "slash instead of colon"},
    {"http:example.com", "missing slashes"},
    {"http:/example.com", "single slash"},
    {"http:///example.com", "triple slash"},
    {"ttp://example.com", "missing h"},
    {"http://.com", "dot-start TLD"},
    {"http://-example.com", "leading hyphen"},
    {"http://example-.com", "trailing hyphen in label"},
    {"http://exam ple.com", "space in host"},
    {"http://exam\tple.com", "tab in host"},
    {"http://exam\nple.com", "newline in host"},
    {"http://-example.example.com", "leading hyphen label"},
    {"http://example-.example.com", "trailing hyphen label"},
    {"http://-.com", "hyphen only"},
    {".example.com", "leading dot - rejected by default scheme addition"},
    {"http://.example.com", "leading dot"},
    {"http://example..com", "double dot"},
    {"http://example..co.uk", "double dot before TLD"},
    {"http://.com", "bare dot TLD"},
};

ParameterizedTestParameters(url_validator, malformed) {
  return cr_make_param_array(url_test_case_t, malformed_urls, sizeof(malformed_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, malformed, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Schemeless URLs (Bare Domains)
 * ============================================================================ */

static url_test_case_t schemeless_valid_urls[] = {
    {"example.com", "bare domain (defaults to http)"},
    {"www.example.com", "www prefix (defaults to http)"},
    {"sub.example.com", "subdomain (defaults to http)"},
    {"a.b.c.example.com", "deep subdomain (defaults to http)"},
    {"example.com/path", "bare domain+path (defaults to http)"},
    {"example.com:8080", "bare domain+port (defaults to http)"},
    {"example.com?query=1", "bare domain+query (defaults to http)"},
    {"example.com#fragment", "bare domain+fragment (defaults to http)"},
    {"foo.com", "bare hostname (defaults to http)"},
    {"foo.com/path", "bare+path (defaults to http)"},
    {"localhost", "bare localhost (defaults to http)"},
    {"localhost:8080", "bare localhost+port (defaults to http)"},
    {"192.168.1.1", "bare IP (defaults to http)"},
};

ParameterizedTestParameters(url_validator, schemeless_valid) {
  return cr_make_param_array(url_test_case_t, schemeless_valid_urls,
                             sizeof(schemeless_valid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, schemeless_valid, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

static url_test_case_t schemeless_invalid_urls[] = {
    {"//example.com", "protocol-relative"},
    {"//www.example.com", "protocol-relative www"},
    {"///example.com", "triple slash"},
    {"////example.com", "quad slash"},
    {"user@example.com", "ambiguous email address"},
    {"user:pass@example.com", "userinfo requires scheme"},
};

ParameterizedTestParameters(url_validator, schemeless_invalid) {
  return cr_make_param_array(url_test_case_t, schemeless_invalid_urls,
                             sizeof(schemeless_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, schemeless_invalid, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Invalid IPv4
 * ============================================================================ */

static url_test_case_t ipv4_invalid_urls[] = {
    {"http://1.1.1", "three octets"},
    {"http://1.1.1.1.1", "five octets"},
    {"http://1.1.1.256", "octet > 255"},
    {"http://1.1.256.1", "middle octet > 255"},
    {"http://256.1.1.1", "first octet > 255"},
    {"http://1.1.1.-1", "negative octet"},
    {"http://1.1..1", "double dot"},
    {"http://1.1.1.1.1.1", "many octets"},
    {"http://1", "single octet"},
    {"http://3628126748", "decimal IP"},
    {"http://1.1.1.+1", "plus sign in octet"},
    {"http://1.1.1.~1", "tilde in octet"},
    {"http://1.1.1.!1", "exclamation in octet"},
    {"http://1..1.1", "empty octet"},
    {"http://1.2.3.256", "invalid IP (octet > 255)"},
};

ParameterizedTestParameters(url_validator, ipv4_invalid) {
  return cr_make_param_array(url_test_case_t, ipv4_invalid_urls, sizeof(ipv4_invalid_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, ipv4_invalid, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Spaces and Whitespace
 * ============================================================================ */

static url_test_case_t whitespace_urls[] = {
    {"http://exam ple.com", "space in host"},
    {"http://example .com", "space before TLD"},
    {"http://example. com", "space after dot"},
    {"http:// example.com", "space after scheme"},
    {"http ://example.com", "space before scheme"},
    {"http://example.com /path", "space before path"},
    {"http://example.com/ path", "space in path"},
    {"http://example.com/path /file", "space in path 2"},
    {"http://example.com?q=hello world", "space in query"},
    {"http://example.com#hello world", "space in fragment"},
    {"http://exam\tple.com", "tab in host"},
    {"http://example.com\tpath", "tab in path"},
    {"http://example.com?q=\t", "tab in query"},
    {"http://example.com\npath", "newline in path"},
    {"http://example.com\r\npath", "CRLF in path"},
    {"http://foo.bar/foo(bar)baz quux", "space in path with parens"},
};

ParameterizedTestParameters(url_validator, whitespace) {
  return cr_make_param_array(url_test_case_t, whitespace_urls, sizeof(whitespace_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, whitespace, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Invalid Characters
 * ============================================================================ */

static url_test_case_t invalid_char_urls[] = {
    {"http://exam\x01ple.com", "control character in host"},
    {"http://exam\x1fple.com", "control character in host 2"},
    {"http://exam[ple.com", "bracket in host"},
    {"http://exam]ple.com", "closing bracket in host"},
    {"http://exam{ple.com", "curly bracket in host"},
    {"http://exam}ple.com", "closing curly in host"},
    {"http://exam<ple.com", "angle bracket"},
    {"http://exam|ple.com", "pipe in host"},
    {"http://exam\\ple.com", "backslash in host"},
    {"http://exam\"ple.com", "quote in host"},
    {"http://exam'ple.com", "apostrophe in host"},
    {"http://exam`ple.com", "backtick in host"},
};

ParameterizedTestParameters(url_validator, invalid_characters) {
  return cr_make_param_array(url_test_case_t, invalid_char_urls, sizeof(invalid_char_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, invalid_characters, .fini = cleanup) {
  assert_url_invalid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Real-World Integration Tests
 * ============================================================================ */

static url_test_case_t integration_urls[] = {
    // GitHub
    {"https://github.com/user/repo", "GitHub repo"},
    {"https://github.com/user/repo/issues/123", "GitHub issue"},
    {"https://raw.githubusercontent.com/user/repo/master/README.md", "GitHub raw"},

    // YouTube
    {"https://www.youtube.com/watch?v=dQw4w9WgXcQ", "YouTube video"},
    {"https://youtu.be/dQw4w9WgXcQ", "YouTube short link"},

    // APIs
    {"https://api.github.com/repos/user/repo", "GitHub API"},
    {"https://api.openai.com/v1/chat/completions", "OpenAI API"},
    {"https://httpbin.org/post", "httpbin service"},

    // Media streams
    {"https://example.com/stream/video.mp4", "MP4 stream"},
    {"https://example.com/media/audio.m3u8", "HLS stream"},
    {"https://cdn.example.com/video/1080p/file.mkv", "CDN video"},

    // Local services
    {"http://localhost:3000/api", "local dev server"},
    {"http://myserver:8080/admin", "LAN admin panel"},
    {"http://nas/files/backup.tar.gz", "NAS backup"},
    {"http://raspberrypi:8000/camera", "Raspberry Pi camera"},
    {"http://localhost/", "localhost root"},
    {"http://localhost:5000/api/health", "localhost API"},
    {"https://[::1]:8443/admin", "IPv6 localhost HTTPS"},
};

ParameterizedTestParameters(url_validator, integration) {
  return cr_make_param_array(url_test_case_t, integration_urls, sizeof(integration_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, integration, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}

/* ============================================================================
 * Test Suite: Edge Cases
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(url_validator, LOG_DEBUG, LOG_DEBUG, false, false);

Test(url_validator, edge_cases_very_long_url) {
  // Construct a very long but valid URL
  const char *long_url = "https://example.com/path/to/resource?param1=value1&param2=value2"
                         "&param3=value3&param4=value4&param5=value5&param6=value6"
                         "&param7=value7&param8=value8&param9=value9&param10=value10"
                         "#section-with-long-fragment-name";
  assert_url_valid(long_url, "very long URL");
}

static url_test_case_t edge_case_urls[] = {
    {"http://a", "single char host"},
    {"https://x.y", "minimal domain"},
    {"http://example.com:65535", "max port 65535"},
    {"https://example.com:1", "min port 1"},
    {"http://example.com?", "query marker only"},
    {"http://example.com#", "fragment marker only"},
    {"http://example.com?q=&p=", "empty query params"},
    {"http://example.com?q=[1,2,3]", "brackets"},
    {"http://example.com?q={1,2,3}", "curly brackets"},
};

ParameterizedTestParameters(url_validator, edge_cases) {
  return cr_make_param_array(url_test_case_t, edge_case_urls, sizeof(edge_case_urls) / sizeof(url_test_case_t));
}

ParameterizedTest(url_test_case_t *test, url_validator, edge_cases, .fini = cleanup) {
  assert_url_valid(test->url, test->note);
}
