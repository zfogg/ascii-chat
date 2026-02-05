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
#include <ascii-chat/util/url.h>
#include <string.h>

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

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
 * Test Suite: Bynens Benchmark (should match)
 * ============================================================================ */

Test(url_validator, bynens_should_match_basic_urls) {
  assert_url_valid("http://foo.com/blah_blah", "basic URL");
  assert_url_valid("http://foo.com/blah_blah/", "with trailing slash");
  assert_url_valid("http://foo.com/blah_blah_(wikipedia)", "parens in path");
  assert_url_valid("http://foo.com/blah_blah_(wikipedia)_(again)", "double parens");
}

Test(url_validator, bynens_should_match_query_userinfo) {
  assert_url_valid("http://www.example.com/wpstyle/?p=364", "query string");
  assert_url_valid("https://www.example.com/foo/?bar=baz&inga=42&quux", "complex query");
  assert_url_valid("http://userid:password@example.com:8080", "userinfo+port");
  assert_url_valid("http://userid@example.com", "user only");
  assert_url_valid("http://userid:password@example.com", "user:password");
}

Test(url_validator, bynens_should_match_ips_fragments) {
  assert_url_valid("http://142.42.1.1/", "IPv4");
  assert_url_valid("http://142.42.1.1:8080/", "IPv4+port");
  assert_url_valid("http://foo.com/blah_(wikipedia)#cite-1", "parens+fragment");
  assert_url_valid("http://code.google.com/events/#&product=browser", "fragment");
}

Test(url_validator, bynens_should_match_edge_cases) {
  assert_url_valid("http://j.mp", "short domain");
  assert_url_valid("http://foo.bar/?q=Test%20URL-encoded%20stuff", "percent-encoded");
  assert_url_valid("http://1337.net", "numeric subdomain");
  assert_url_valid("http://a.b-c.de", "hyphenated");
  assert_url_valid("http://223.255.255.254", "high IPv4");
  assert_url_valid("https://foo_bar.example.com/", "underscore in host");
}

/* ============================================================================
 * Test Suite: Bynens Benchmark (should NOT match)
 * ============================================================================ */

Test(url_validator, bynens_should_not_match_malformed) {
  assert_url_invalid("http://", "scheme only");
  assert_url_invalid("http://.", "dot only");
  assert_url_invalid("http://..", "double dot");
  assert_url_invalid("http://?", "question mark only");
  assert_url_invalid("http://#", "hash only");
}

Test(url_validator, bynens_should_not_match_invalid_schemes) {
  assert_url_invalid("//", "no scheme");
  assert_url_valid("foo.com", "bare hostname (defaults to http)");
  assert_url_invalid("rdar://1234", "wrong scheme");
  assert_url_invalid("ftps://foo.bar/", "ftps not allowed");
}

Test(url_validator, bynens_should_not_match_multicast_and_broadcast) {
  assert_url_invalid("http://224.1.1.1", "multicast");
  assert_url_invalid("http://255.255.255.255", "broadcast");
}

Test(url_validator, ipv4_multicast_rejected) {
  assert_url_invalid("http://224.0.0.1", "224.0.0.1 base multicast");
  assert_url_invalid("http://224.0.0.251", "224.0.0.251 mDNS");
  assert_url_invalid("http://228.1.1.1", "228.x.x.x mid-range multicast");
  assert_url_invalid("http://239.255.255.255", "239.255.255.255 top multicast");
}

Test(url_validator, bynens_should_not_match_invalid_format) {
  assert_url_invalid("http://foo.bar/foo(bar)baz quux", "space in path");
  assert_url_invalid("http://-error-.invalid/", "leading hyphen");
  assert_url_invalid("http://1.1.1.1.1", "five octets");
  assert_url_invalid("http://123.123.123", "three octets");
}

/* ============================================================================
 * Test Suite: Bare Hostnames
 * ============================================================================ */

Test(url_validator, bare_hostnames_localhost) {
  assert_url_valid("http://localhost", "localhost");
  assert_url_valid("http://localhost/", "localhost+slash");
  assert_url_valid("http://localhost:3000", "localhost+port");
  assert_url_valid("http://localhost:8080/api/v1", "localhost+port+path");
  assert_url_valid("https://localhost:443/path#frag", "localhost+https+frag");
}

Test(url_validator, bare_hostnames_lan_devices) {
  assert_url_valid("http://myserver", "bare hostname");
  assert_url_valid("http://raspberrypi", "LAN host");
  assert_url_valid("http://nas/files/movie.mkv", "NAS path");
  assert_url_valid("http://printer:631", "CUPS port");
  assert_url_valid("http://db:5432", "postgres port");
  assert_url_valid("http://redis:6379/0", "redis db0");
}

Test(url_validator, bare_hostnames_k8s) {
  assert_url_valid("http://k8s-service:80/healthz", "k8s service name");
  assert_url_valid("http://a", "single-char hostname");
  assert_url_valid("http://z", "single-char z");
}

/* ============================================================================
 * Test Suite: Domains and Labels
 * ============================================================================ */

Test(url_validator, domains_basic) {
  assert_url_valid("http://example.com", "basic domain");
  assert_url_valid("https://example.com/", "basic+slash");
  assert_url_valid("http://www.example.com", "www prefix");
  assert_url_valid("http://a.b", "minimal two-label");
  assert_url_valid("http://x.co", "two-char TLD");
}

Test(url_validator, domains_subdomains) {
  assert_url_valid("http://a.b.c.d.e.com", "5 subdomains");
  assert_url_valid("http://a.b.c.d.e.f.g.h.example.com", "8 subdomains");
  assert_url_valid("http://sub1.sub2.sub3.example.co.uk", "deep+country");
}

Test(url_validator, domains_hyphens_underscores) {
  assert_url_valid("http://my-server.com", "hyphen in label");
  assert_url_valid("http://a-b.c-d.com", "hyphens everywhere");
  assert_url_valid("http://my_server.com", "underscore in label");
  assert_url_valid("http://foo_bar.baz_qux.com", "multiple underscores");
  assert_url_valid("http://_dmarc.example.com", "leading underscore");
}

Test(url_validator, domains_numeric_labels) {
  assert_url_valid("http://123abc.com", "digits then alpha");
  assert_url_valid("http://abc123.com", "alpha then digits");
  assert_url_valid("http://1337.net", "all-numeric label");
  assert_url_valid("http://1.2.3.example.com", "numeric subdomains");
  assert_url_valid("http://007.bond.com", "leading zeros in label");
}

Test(url_validator, domains_trailing_dot_fqdn) {
  assert_url_valid("http://example.com.", "FQDN trailing dot");
  assert_url_valid("http://www.example.com.", "FQDN www");
}

/* ============================================================================
 * Test Suite: TLDs (gTLDs, ccTLDs, IDN)
 * ============================================================================ */

Test(url_validator, gtlds_classic) {
  assert_url_valid("http://example.com", ".com");
  assert_url_valid("http://example.net", ".net");
  assert_url_valid("http://example.org", ".org");
  assert_url_valid("http://example.edu", ".edu");
  assert_url_valid("http://example.gov", ".gov");
}

Test(url_validator, gtlds_country_codes) {
  assert_url_valid("http://example.uk", ".uk");
  assert_url_valid("http://example.de", ".de");
  assert_url_valid("http://example.jp", ".jp");
  assert_url_valid("http://example.io", ".io");
  assert_url_valid("http://example.co", ".co");
  assert_url_valid("http://example.ru", ".ru");
}

Test(url_validator, gtlds_compound_country) {
  assert_url_valid("http://example.co.uk", ".co.uk");
  assert_url_valid("http://example.co.jp", ".co.jp");
  assert_url_valid("http://example.com.au", ".com.au");
  assert_url_valid("http://example.org.uk", ".org.uk");
}

Test(url_validator, gtlds_new) {
  assert_url_valid("http://example.app", ".app");
  assert_url_valid("http://example.dev", ".dev");
  assert_url_valid("http://example.blog", ".blog");
  assert_url_valid("http://example.cloud", ".cloud");
  assert_url_valid("http://example.shop", ".shop");
  assert_url_valid("http://example.xyz", ".xyz");
}

Test(url_validator, gtlds_unicode_idn) {
  assert_url_valid("https://例子.测试", "Chinese .测试");
  assert_url_valid("https://пример.рф", "Russian .рф");
  assert_url_valid("https://münchen.de", "German umlaut");
  assert_url_valid("https://café.com", "French accent");
  assert_url_valid("https://日本語.jp", "Japanese IDN");
}

/* ============================================================================
 * Test Suite: Fragments
 * ============================================================================ */

Test(url_validator, fragments_basic) {
  assert_url_valid("http://example.com#", "empty fragment");
  assert_url_valid("http://example.com#top", "simple fragment");
  assert_url_valid("http://example.com#section", "section");
  assert_url_valid("http://example.com#section-1", "fragment with hyphen");
}

Test(url_validator, fragments_special_chars) {
  assert_url_valid("http://example.com#section_two", "fragment with underscore");
  assert_url_valid("http://example.com#section/sub", "fragment with slash");
  assert_url_valid("http://example.com#L42", "GitHub line fragment");
  assert_url_valid("http://example.com#:~:text=hello", "Chrome text fragment");
}

/* ============================================================================
 * Test Suite: Queries
 * ============================================================================ */

Test(url_validator, queries_simple) {
  assert_url_valid("http://example.com?q=1", "simple query");
  assert_url_valid("http://example.com?", "empty query");
  assert_url_valid("http://example.com?a=1&b=2", "multi-param");
  assert_url_valid("http://example.com?key=", "empty value");
}

Test(url_validator, queries_encoded) {
  assert_url_valid("http://example.com?q=hello%20world", "percent-encoded space");
  assert_url_valid("http://example.com?a[0]=1&a[1]=2", "array params");
  assert_url_valid("http://example.com?redirect=http://other.com", "URL in query");
}

/* ============================================================================
 * Test Suite: Paths
 * ============================================================================ */

Test(url_validator, paths_basic) {
  assert_url_valid("http://example.com/", "root path");
  assert_url_valid("http://example.com/path", "simple path");
  assert_url_valid("http://example.com/a/b/c/d", "deep path");
  assert_url_valid("http://example.com/page.html", "file extension");
}

Test(url_validator, paths_special_chars) {
  assert_url_valid("http://example.com/%E4%B8%AD%E6%96%87", "percent-encoded path");
  assert_url_valid("http://example.com/(parens)/in/path", "parens in path");
  assert_url_valid("http://example.com/~user", "tilde in path");
  assert_url_valid("http://example.com/@user", "at in path");
  assert_url_valid("http://example.com/path,with,commas", "commas in path");
}

/* ============================================================================
 * Test Suite: IPv4 (Valid Public IPs)
 * ============================================================================ */

Test(url_validator, ipv4_public_valid) {
  assert_url_valid("http://1.1.1.1", "Cloudflare DNS");
  assert_url_valid("http://8.8.8.8", "Google DNS");
  assert_url_valid("http://142.42.1.1", "mid-range");
  assert_url_valid("http://223.255.255.254", "high end");
  assert_url_valid("http://100.64.0.1", "CGNAT range");
}

Test(url_validator, ipv4_public_with_port) {
  assert_url_valid("http://1.1.1.1:80", "IPv4+port 80");
  assert_url_valid("http://8.8.8.8:443", "IPv4+port 443");
  assert_url_valid("http://1.1.1.1:8080", "IPv4+port 8080");
  assert_url_valid("http://142.42.1.1:65535", "IPv4+port max");
}

Test(url_validator, ipv4_public_with_path) {
  assert_url_valid("http://1.1.1.1/", "IPv4+root");
  assert_url_valid("http://1.1.1.1/path", "IPv4+path");
  assert_url_valid("http://8.8.8.8/dns-query", "IPv4+api path");
}

/* ============================================================================
 * Test Suite: IPv6 (Valid)
 * ============================================================================ */

Test(url_validator, ipv6_standard_forms) {
  assert_url_valid("http://[2001:0db8:85a3:0000:0000:8a2e:0370:7334]", "full IPv6");
  assert_url_valid("http://[2001:db8:85a3::8a2e:370:7334]", "compressed");
  assert_url_valid("http://[2001:db8::1]", "short compressed");
  assert_url_valid("http://[::1]", "loopback");
  assert_url_valid("http://[fe80::1]", "link-local");
}

Test(url_validator, ipv6_ipv4_mapped) {
  assert_url_valid("http://[::ffff:192.168.1.1]", "IPv4-mapped");
  assert_url_valid("http://[::ffff:10.0.0.1]", "IPv4-mapped private");
  assert_url_valid("http://[::ffff:127.0.0.1]", "IPv4-mapped loopback");
}

Test(url_validator, ipv6_zone_id) {
  assert_url_valid("https://[fe80::1%25eth0]/", "zone ID eth0");
  assert_url_valid("https://[fe80::1%25en0]/", "zone ID en0");
  assert_url_valid("https://[fe80::1%25wlan0]/", "zone ID wlan0");
}

Test(url_validator, ipv6_with_port_path) {
  assert_url_valid("http://[::1]:80", "IPv6+port 80");
  assert_url_valid("http://[::1]:8080", "IPv6+port 8080");
  assert_url_valid("http://[::1]/", "IPv6+root");
  assert_url_valid("http://[::1]/path", "IPv6+path");
  assert_url_valid("http://[::1]:8080/path?q=1#frag", "IPv6+all");
}

Test(url_validator, ipv6_unbracketed_rejected) {
  assert_url_invalid("http://2001:db8::1", "unbracketed IPv6");
  assert_url_invalid("http://::1", "unbracketed loopback");
  assert_url_invalid("http://fe80::1", "unbracketed link-local");
}

/* ============================================================================
 * Test Suite: Ports
 * ============================================================================ */

Test(url_validator, ports_valid) {
  assert_url_valid("http://example.com:1", "port 1");
  assert_url_valid("http://example.com:22", "SSH");
  assert_url_valid("http://example.com:80", "HTTP");
  assert_url_valid("http://example.com:443", "HTTPS");
  assert_url_valid("http://example.com:8080", "alt HTTP");
  assert_url_valid("http://example.com:65535", "max port");
}

Test(url_validator, ports_with_path_query) {
  assert_url_valid("http://example.com:8080/", "port+root");
  assert_url_valid("http://example.com:8080/path", "port+path");
  assert_url_valid("http://example.com:8080?q=1", "port+query");
  assert_url_valid("http://example.com:8080/p?q=1#s", "port+all");
}

/* ============================================================================
 * Test Suite: Userinfo
 * ============================================================================ */

Test(url_validator, userinfo_basic) {
  assert_url_valid("http://user@example.com", "user only");
  assert_url_valid("http://user:pass@example.com", "user:pass");
  assert_url_valid("http://user:p%40ss@example.com", "encoded @ in pass");
  assert_url_valid("http://user:@example.com", "empty password");
}

Test(url_validator, userinfo_with_hosts) {
  assert_url_valid("http://user@localhost", "user@bare");
  assert_url_valid("http://user@localhost:3000", "user@bare+port");
  assert_url_valid("http://user@1.1.1.1", "user@IPv4");
  assert_url_valid("http://user@[::1]", "user@IPv6");
  assert_url_valid("http://user:pass@myserver:8080/", "user:pass@LAN+port");
}

/* ============================================================================
 * Test Suite: Schemes
 * ============================================================================ */

Test(url_validator, schemes_valid) {
  assert_url_valid("http://example.com", "http");
  assert_url_valid("https://example.com", "https");
  assert_url_valid("HTTP://EXAMPLE.COM", "HTTP uppercase");
  assert_url_valid("HTTPS://EXAMPLE.COM", "HTTPS uppercase");
  assert_url_valid("Http://Example.com", "mixed case");
}

Test(url_validator, schemes_rejected) {
  assert_url_invalid("ftp://example.com", "ftp");
  assert_url_invalid("ftps://example.com", "ftps");
  assert_url_invalid("ws://example.com", "websocket");
  assert_url_invalid("wss://example.com", "websocket secure");
  assert_url_invalid("file:///path/to/file", "file");
  assert_url_invalid("ssh://example.com", "ssh");
  assert_url_invalid("git://example.com/repo", "git");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejections
 * ============================================================================ */

Test(url_validator, reject_malformed) {
  assert_url_invalid("", "empty string");
  assert_url_invalid(" ", "space");
  assert_url_invalid("not a url", "plain text");
  assert_url_invalid("http", "bare scheme word");
  assert_url_invalid("http://", "scheme+two slashes only");
}

Test(url_validator, reject_schemeless) {
  assert_url_valid("example.com", "bare domain (defaults to http)");
  assert_url_valid("www.example.com", "bare www (defaults to http)");
  assert_url_valid("foo.com/path", "bare+path (defaults to http)");
  assert_url_invalid("//example.com", "protocol-relative");
}

Test(url_validator, reject_spaces) {
  assert_url_invalid("http:// example.com", "space after scheme");
  assert_url_invalid("http://example .com", "space in host");
  assert_url_invalid("http://example.com/path name", "space in path");
}

/* ============================================================================
 * Test Suite: Real-World Integration Tests
 * ============================================================================ */

Test(url_validator, integration_github) {
  assert_url_valid("https://github.com/user/repo", "GitHub repo");
  assert_url_valid("https://github.com/user/repo/issues/123", "GitHub issue");
  assert_url_valid("https://raw.githubusercontent.com/user/repo/master/README.md", "GitHub raw");
}

Test(url_validator, integration_youtube) {
  assert_url_valid("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "YouTube video");
  assert_url_valid("https://youtu.be/dQw4w9WgXcQ", "YouTube short link");
}

Test(url_validator, integration_apis) {
  assert_url_valid("https://api.github.com/repos/user/repo", "GitHub API");
  assert_url_valid("https://api.openai.com/v1/chat/completions", "OpenAI API");
  assert_url_valid("https://httpbin.org/post", "httpbin service");
}

Test(url_validator, integration_media_streams) {
  assert_url_valid("https://example.com/stream/video.mp4", "MP4 stream");
  assert_url_valid("https://example.com/media/audio.m3u8", "HLS stream");
  assert_url_valid("https://cdn.example.com/video/1080p/file.mkv", "CDN video");
}

Test(url_validator, integration_local_services) {
  assert_url_valid("http://localhost:3000/api", "local dev server");
  assert_url_valid("http://myserver:8080/admin", "LAN admin panel");
  assert_url_valid("http://nas/files/backup.tar.gz", "NAS backup");
  assert_url_valid("http://raspberrypi:8000/camera", "Raspberry Pi camera");
}

Test(url_validator, integration_localhost_variants) {
  assert_url_valid("http://localhost/", "localhost root");
  assert_url_valid("http://localhost:5000/api/health", "localhost API");
  assert_url_valid("https://[::1]:8443/admin", "IPv6 localhost HTTPS");
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

Test(url_validator, edge_cases_very_long_url) {
  // Construct a very long but valid URL
  const char *long_url = "https://example.com/path/to/resource?param1=value1&param2=value2"
                         "&param3=value3&param4=value4&param5=value5&param6=value6"
                         "&param7=value7&param8=value8&param9=value9&param10=value10"
                         "#section-with-long-fragment-name";
  assert_url_valid(long_url, "very long URL");
}

Test(url_validator, edge_cases_minimal_valid) {
  assert_url_valid("http://a", "single char host");
  assert_url_valid("https://x.y", "minimal domain");
}

Test(url_validator, edge_cases_maximum_port) {
  assert_url_valid("http://example.com:65535", "max port 65535");
  assert_url_valid("https://example.com:1", "min port 1");
}

Test(url_validator, edge_cases_query_fragment_only) {
  assert_url_valid("http://example.com?", "query marker only");
  assert_url_valid("http://example.com#", "fragment marker only");
  assert_url_valid("http://example.com?q=&p=", "empty query params");
  assert_url_valid("http://example.com?q=[1,2,3]", "brackets");
  assert_url_valid("http://example.com?q={1,2,3}", "curly brackets");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid Schemes
 * ============================================================================ */

Test(url_validator, reject_wrong_schemes_1) {
  assert_url_invalid("ftp://example.com", "ftp scheme");
  assert_url_invalid("ftps://example.com", "ftps scheme");
  assert_url_invalid("sftp://example.com", "sftp scheme");
  assert_url_invalid("ssh://example.com", "ssh scheme");
  assert_url_invalid("scp://example.com", "scp scheme");
}

Test(url_validator, reject_wrong_schemes_2) {
  assert_url_invalid("ws://example.com", "websocket");
  assert_url_invalid("wss://example.com", "websocket secure");
  assert_url_invalid("file:///path", "file scheme");
  assert_url_invalid("data:text/html", "data URI");
  assert_url_invalid("blob:http://example.com/uuid", "blob scheme");
}

Test(url_validator, reject_wrong_schemes_3) {
  assert_url_invalid("mailto:user@example.com", "mailto");
  assert_url_invalid("tel:+1234567890", "tel");
  assert_url_invalid("git://example.com/repo", "git");
  assert_url_invalid("svn://example.com/repo", "svn");
  assert_url_invalid("magnet:?xt=urn:btih", "magnet");
}

Test(url_validator, reject_wrong_schemes_4) {
  assert_url_invalid("javascript:alert(1)", "javascript");
  assert_url_invalid("about:blank", "about");
  assert_url_invalid("chrome://settings", "chrome internal");
  assert_url_invalid("file://localhost/path", "file local");
  assert_url_invalid("news:comp.lang.c", "news");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Malformed URLs
 * ============================================================================ */

Test(url_validator, reject_malformed_scheme) {
  assert_url_invalid("http/example.com", "slash instead of colon");
  assert_url_invalid("http:example.com", "missing slashes");
  assert_url_invalid("http:/example.com", "single slash");
  assert_url_invalid("http:///example.com", "triple slash");
  assert_url_invalid("ttp://example.com", "missing h");
}

Test(url_validator, reject_malformed_host) {
  assert_url_invalid("http://", "empty host");
  assert_url_invalid("http://.", "dot only");
  assert_url_invalid("http://..", "double dot only");
  assert_url_invalid("http://...", "triple dot");
  assert_url_invalid("http://.com", "dot-start TLD");
}

Test(url_validator, reject_malformed_host_2) {
  assert_url_invalid("http://-example.com", "leading hyphen");
  assert_url_invalid("http://example-.com", "trailing hyphen in label");
  assert_url_invalid("http://exam ple.com", "space in host");
  assert_url_invalid("http://exam\tple.com", "tab in host");
  assert_url_invalid("http://exam\nple.com", "newline in host");
}

Test(url_validator, reject_empty_components) {
  assert_url_invalid("", "empty string");
  assert_url_invalid(" ", "single space");
  assert_url_invalid("   ", "multiple spaces");
  assert_url_invalid("\t", "tab");
  assert_url_invalid("\n", "newline");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Missing Scheme
 * ============================================================================ */

Test(url_validator, reject_schemeless_1) {
  assert_url_valid("example.com", "bare domain (defaults to http)");
  assert_url_valid("www.example.com", "www prefix (defaults to http)");
  assert_url_valid("sub.example.com", "subdomain (defaults to http)");
  assert_url_valid("a.b.c.example.com", "deep subdomain (defaults to http)");
}

Test(url_validator, reject_schemeless_2) {
  assert_url_valid("example.com/path", "bare domain+path (defaults to http)");
  assert_url_valid("example.com:8080", "bare domain+port (defaults to http)");
  assert_url_valid("example.com?query=1", "bare domain+query (defaults to http)");
  assert_url_valid("example.com#fragment", "bare domain+fragment (defaults to http)");
}

Test(url_validator, reject_schemeless_3) {
  assert_url_invalid("//example.com", "protocol-relative");
  assert_url_invalid("//www.example.com", "protocol-relative www");
  assert_url_invalid("///example.com", "triple slash");
  assert_url_invalid("////example.com", "quad slash");
}

Test(url_validator, reject_schemeless_4) {
  assert_url_invalid("user@example.com", "ambiguous email address");
  assert_url_invalid("user:pass@example.com", "userinfo requires scheme");
  assert_url_valid("localhost", "bare localhost (defaults to http)");
  assert_url_valid("localhost:8080", "bare localhost+port (defaults to http)");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid IPv4
 * ============================================================================ */

Test(url_validator, valid_private_ipv4_10) {
  assert_url_valid("http://10.0.0.0", "10.0.0.0 network");
  assert_url_valid("http://10.0.0.1", "10.0.0.1");
  assert_url_valid("http://10.1.1.1", "10.1.1.1");
  assert_url_valid("http://10.255.255.254", "10.255.255.254");
  assert_url_valid("http://10.255.255.255", "10.255.255.255");
}

Test(url_validator, valid_private_ipv4_172) {
  assert_url_valid("http://172.16.0.0", "172.16.0.0 network");
  assert_url_valid("http://172.16.0.1", "172.16.0.1");
  assert_url_valid("http://172.20.0.1", "172.20 mid-range");
  assert_url_valid("http://172.31.255.254", "172.31 top");
}

Test(url_validator, valid_private_ipv4_192) {
  assert_url_valid("http://192.168.0.0", "192.168.0.0 network");
  assert_url_valid("http://192.168.0.1", "192.168.0.1");
  assert_url_valid("http://192.168.1.1", "192.168.1.1");
  assert_url_valid("http://192.168.255.254", "192.168.255.254");
}

Test(url_validator, valid_loopback_ipv4) {
  assert_url_valid("http://127.0.0.0", "127.0.0.0");
  assert_url_valid("http://127.0.0.1", "127.0.0.1 loopback");
  assert_url_valid("http://127.0.0.2", "127.0.0.2");
  assert_url_valid("http://127.1.1.1", "127.1.1.1");
  assert_url_valid("http://127.255.255.254", "127.255.255.254");
}

Test(url_validator, valid_link_local_ipv4) {
  assert_url_valid("http://169.254.0.0", "169.254.0.0");
  assert_url_valid("http://169.254.0.1", "169.254.0.1");
  assert_url_valid("http://169.254.1.1", "169.254.1.1");
  assert_url_valid("http://169.254.255.254", "169.254.255.254");
}

Test(url_validator, reject_malformed_ipv4) {
  assert_url_invalid("http://1.1.1", "three octets");
  assert_url_invalid("http://1.1.1.1.1", "five octets");
  assert_url_invalid("http://1.1.1.256", "octet > 255");
  assert_url_invalid("http://1.1.256.1", "middle octet > 255");
  assert_url_invalid("http://256.1.1.1", "first octet > 255");
}

Test(url_validator, reject_malformed_ipv4_2) {
  assert_url_invalid("http://1.1.1.-1", "negative octet");
  assert_url_invalid("http://1.1..1", "double dot");
  assert_url_invalid("http://1.1.1.1.1.1", "many octets");
  assert_url_invalid("http://1", "single octet");
  assert_url_invalid("http://3628126748", "decimal IP");
}

Test(url_validator, reject_ipv4_malformed) {
  assert_url_invalid("http://1.1.1.-1", "negative octet");
  assert_url_invalid("http://1.1.1.+1", "plus sign in octet");
  assert_url_invalid("http://1.1.1.~1", "tilde in octet");
  assert_url_invalid("http://1.1.1.!1", "exclamation in octet");
  assert_url_invalid("http://1..1.1", "empty octet");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid IPv6
 * ============================================================================ */

Test(url_validator, reject_unbracketed_ipv6) {
  assert_url_invalid("http://2001:db8::1", "unbracketed IPv6");
  assert_url_invalid("http://::1", "unbracketed loopback");
  assert_url_invalid("http://fe80::1", "unbracketed link-local");
  assert_url_invalid("http://ff02::1", "unbracketed multicast");
}

Test(url_validator, reject_malformed_ipv6) {
  assert_url_invalid("http://[2001:db8::1", "missing closing bracket");
  assert_url_invalid("http://2001:db8::1]", "missing opening bracket");
  assert_url_invalid("http://[2001:db8::/32]", "CIDR notation not allowed");
  assert_url_invalid("http://[2001:db8::xyz]", "invalid hex characters");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid Ports
 * ============================================================================ */

Test(url_validator, reject_invalid_ports) {
  assert_url_invalid("http://example.com:", "colon no port");
  assert_url_invalid("http://example.com:abc", "non-numeric port");
  assert_url_invalid("http://example.com:-8080", "negative port");
  assert_url_invalid("http://example.com:8080a", "alphanumeric port");
}

Test(url_validator, reject_invalid_ports_2) {
  assert_url_invalid("http://example.com:-1", "negative port");
  assert_url_invalid("http://example.com:abc", "non-numeric port");
  assert_url_invalid("http://example.com:8a", "hex port");
  assert_url_invalid("http://example.com:8.0", "float port");
  assert_url_invalid("http://example.com:80 ", "space after port");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Spaces and Whitespace
 * ============================================================================ */

Test(url_validator, reject_spaces_in_url_1) {
  assert_url_invalid("http://exam ple.com", "space in host");
  assert_url_invalid("http://example .com", "space before TLD");
  assert_url_invalid("http://example. com", "space after dot");
  assert_url_invalid("http:// example.com", "space after scheme");
  assert_url_invalid("http ://example.com", "space before scheme");
}

Test(url_validator, reject_spaces_in_url_2) {
  assert_url_invalid("http://example.com /path", "space before path");
  assert_url_invalid("http://example.com/ path", "space in path");
  assert_url_invalid("http://example.com/path /file", "space in path 2");
  assert_url_invalid("http://example.com?q=hello world", "space in query");
  assert_url_invalid("http://example.com#hello world", "space in fragment");
}

Test(url_validator, reject_tabs_and_control_chars) {
  assert_url_invalid("http://exam\tple.com", "tab in host");
  assert_url_invalid("http://example.com\tpath", "tab in path");
  assert_url_invalid("http://example.com?q=\t", "tab in query");
  assert_url_invalid("http://example.com\npath", "newline in path");
  assert_url_invalid("http://example.com\r\npath", "CRLF in path");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid Domain Labels
 * ============================================================================ */

Test(url_validator, reject_domain_label_violations) {
  assert_url_invalid("http://-example.com", "leading hyphen");
  assert_url_invalid("http://example-.com", "trailing hyphen");
  assert_url_invalid("http://example-.example.com", "trailing hyphen label");
  assert_url_invalid("http://-example.example.com", "leading hyphen label");
  assert_url_invalid("http://-.com", "hyphen only");
}

Test(url_validator, reject_empty_labels) {
  assert_url_invalid("http://.example.com", "leading dot");
  assert_url_invalid("http://example..com", "double dot");
  assert_url_invalid("http://example..co.uk", "double dot before TLD");
  assert_url_invalid("http://.com", "bare dot TLD");
}

Test(url_validator, reject_invalid_ips) {
  // Invalid octet values
  assert_url_invalid("http://1.2.3.256", "invalid IP (octet > 255)");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Invalid Characters
 * ============================================================================ */

Test(url_validator, reject_invalid_characters_in_host) {
  assert_url_invalid("http://exam\x01ple.com", "control character in host");
  assert_url_invalid("http://exam\x1fple.com", "control character in host");
  assert_url_invalid("http://exam\tple.com", "tab in host");
  assert_url_invalid("http://exam\nple.com", "newline in host");
  assert_url_invalid("http://exam ple.com", "space in host");
}

Test(url_validator, reject_invalid_characters_2) {
  assert_url_invalid("http://exam[ple.com", "bracket in host");
  assert_url_invalid("http://exam]ple.com", "closing bracket in host");
  assert_url_invalid("http://exam{ple.com", "curly bracket in host");
  assert_url_invalid("http://exam}ple.com", "closing curly in host");
  assert_url_invalid("http://exam<ple.com", "angle bracket");
}

Test(url_validator, reject_invalid_characters_3) {
  assert_url_invalid("http://exam|ple.com", "pipe in host");
  assert_url_invalid("http://exam\\ple.com", "backslash in host");
  assert_url_invalid("http://exam\"ple.com", "quote in host");
  assert_url_invalid("http://exam'ple.com", "apostrophe in host");
  assert_url_invalid("http://exam`ple.com", "backtick in host");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Protocol Issues
 * ============================================================================ */

Test(url_validator, reject_protocol_issues) {
  assert_url_invalid("htp://example.com", "typo: htp");
  assert_url_invalid("http2://example.com", "http2");
  assert_url_invalid("http/1.1://example.com", "versioned scheme");
  assert_url_invalid("http-s://example.com", "hyphenated scheme");
  assert_url_invalid("http+secure://example.com", "http+secure");
}

/* ============================================================================
 * Test Suite: Comprehensive Rejection Cases - Miscellaneous
 * ============================================================================ */

Test(url_validator, reject_miscellaneous) {
  assert_url_invalid("not a url", "plain text");
  assert_url_invalid("just some words here", "sentence");
  assert_url_valid("localhost", "bare hostname (defaults to http)");
  assert_url_valid("example.com", "bare domain (defaults to http)");
  assert_url_valid("192.168.1.1", "bare IP (defaults to http)");
}
