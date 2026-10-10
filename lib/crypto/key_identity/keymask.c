#include <ascii-chat/crypto/key_identity/keymask.h>
#include <sodium.h>
#include <string.h>

asciichat_error_t key_fingerprint_digest(const uint8_t public_key[32], uint8_t digest[KEYMASK_DIGEST_SIZE]) {
  if (!public_key || !digest)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing public key or fingerprint output");
  if (crypto_hash_sha256(digest, public_key, 32) != 0)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot compute public-key fingerprint");
  return ASCIICHAT_OK;
}

// Independent feature hashes keep each choice stable as other features evolve.
static void feature_seed(const uint8_t digest[32], const char *feature, uint8_t seed[32]) {
  static const char domain[] = "ascii-chat/keymask/v2";
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  crypto_hash_sha256_update(&state, (const unsigned char *)domain, sizeof(domain));
  crypto_hash_sha256_update(&state, digest, 32);
  crypto_hash_sha256_update(&state, (const unsigned char *)feature, strlen(feature));
  crypto_hash_sha256_final(&state, seed);
}

static void stamp(char cells[16][32], unsigned row, unsigned col, const char *text) {
  for (; *text && col < 32; ++col, ++text)
    cells[row][col] = *text;
}

static void centered(char cells[16][32], unsigned row, const char *text) {
  stamp(cells, row, (unsigned)(32 - strlen(text)) / 2, text);
}

// Independent feature hashes keep each choice stable as other features evolve.
static void feature_seed(const uint8_t digest[32], const char *feature, uint8_t seed[32]) {
  static const char domain[] = "ascii-chat/keymask/v2";
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  crypto_hash_sha256_update(&state, (const unsigned char *)domain, sizeof(domain));
  crypto_hash_sha256_update(&state, digest, 32);
  crypto_hash_sha256_update(&state, (const unsigned char *)feature, strlen(feature));
  crypto_hash_sha256_final(&state, seed);
}

static void stamp(char cells[16][32], unsigned row, unsigned col, const char *text) {
  for (; *text && col < 32; ++col, ++text)
    cells[row][col] = *text;
}

static void centered(char cells[16][32], unsigned row, const char *text) {
  stamp(cells, row, (unsigned)(32 - strlen(text)) / 2, text);
}

asciichat_error_t keymask_render(const uint8_t digest[KEYMASK_DIGEST_SIZE], bool unicode, char *out, size_t size) {
  if (!out || !size)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Keymask output buffer");
  out[0] = '\0';
  if (!digest)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Keymask digest");

  char cells[16][32];
  memset(cells, ' ', sizeof(cells));
  uint8_t outline[32], crown[32], eyes[32], center[32], mouth[32], marks[32];
  feature_seed(digest, "outline", outline);
  feature_seed(digest, "crown", crown);
  feature_seed(digest, "eyes", eyes);
  feature_seed(digest, "center", center);
  feature_seed(digest, "mouth", mouth);
  feature_seed(digest, "markings", marks);

  // Half-widths from forehead to chin. Each family has a distinct silhouette.
  static const unsigned widths[8][12] = {
      {8, 10, 12, 12, 12, 11, 10, 9, 8, 6, 4, 2},     {6, 7, 8, 8, 8, 8, 8, 8, 7, 6, 5, 4},
      {11, 13, 14, 14, 13, 12, 11, 10, 9, 7, 5, 3},   {9, 10, 11, 12, 13, 14, 13, 11, 9, 6, 3, 1},
      {12, 12, 12, 12, 12, 12, 12, 12, 12, 10, 8, 6}, {5, 7, 9, 11, 12, 12, 11, 10, 8, 6, 4, 2},
      {10, 11, 12, 11, 10, 9, 10, 11, 10, 8, 6, 4},   {7, 9, 11, 13, 14, 13, 12, 11, 10, 9, 8, 7},
  };
  const unsigned *profile = widths[outline[0] % 8];
  for (unsigned r = 0; r < 12; ++r) {
    unsigned left = 16 - profile[r], right = 15 + profile[r];
    cells[r + 3][left] = '#';
    cells[r + 3][right] = '#';
    if ((outline[1] & 1) && r > 0 && r < 10) {
      cells[r + 3][left + 1] = '#';
      cells[r + 3][right - 1] = '#';
    }
    if (r == 0 || r == 11)
      for (unsigned c = left; c <= right; ++c)
        cells[r + 3][c] = '#';
  }
  static const char *crowns[8][3] = {
      {"/\\            /\\", "/  \\          /  \\", "\\   \\________/   /"},
      {"      /\\      ", "     /##\\     ", "____/####\\____"},
      {"o              o", "|              |", "/\\____________/\\"},
      {"  []  []  []  ", "  ||  ||  ||  ", "__||__||__||__"},
      {"   ________   ", "  /========\\  ", " /__________\\ "},
      {"\\\\          //", " \\\\        // ", "  \\\\______//  "},
      {"     /||\\     ", "    /||||\\    ", "___/||||||\\___"},
      {" /\\        /\\ ", "<  >      <  >", " \\/________\\/ "},
  };
  for (unsigned r = 0; r < 3; ++r)
    centered(cells, r, crowns[crown[0] % 8][r]);

  static const char *eye_rows[8][2] = {
      {"[###]  [###]", "  V      V  "},   {" /--\\  /--\\ ", " \\__/  \\__/ "}, {" \\##\\  /##/ ", "  \\_/  \\_/  "},
      {"============", "[--O----O--]"},   {" .--.  .--. ", " (XX)  (XX) "},     {" /^^\\  /^^\\ ", " |..|  |..| "},
      {"  <>    <>  ", " /__\\  /__\\ "}, {" [::]  [::] ", "  --    --  "},
  };
  centered(cells, 5, eye_rows[eyes[0] % 8][0]);
  centered(cells, 6, eye_rows[eyes[0] % 8][1]);
  static const char *noses[6][2] = {{"/\\", "\\/"},   {"||", "||"}, {"##", "VV"},
                                    {"/\\", "/__\\"}, {"::", "[]"}, {"\\/", "||"}};
  centered(cells, 8, noses[center[0] % 6][0]);
  centered(cells, 9, noses[center[0] % 6][1]);
  static const char *mouths[8][2] = {
      {"[||||||]", " ------ "}, {"\\ VV VV /", " \\____/ "}, {" ====== ", "        "},     {" /----\\ ", " \\____/ "},
      {" [++++] ", "  ----  "}, {" \\____/ ", "  V  V  "},   {"  /\\/\\  ", "  \\/\\/  "}, {" [####] ", "  ||||  "},
  };
  centered(cells, 11, mouths[mouth[0] % 8][0]);
  centered(cells, 12, mouths[mouth[0] % 8][1]);
  // Cheek ornaments stay outside the facial features and inside the outline.
  static const char *ornaments[] = {"/", "=", ":", "+", "<", "*", "o", "-"};
  for (unsigned r = 7; r <= 10; ++r) {
    unsigned col = 18 - profile[r - 3];
    char mark = ornaments[marks[r] % 8][0];
    cells[r][col] = mark;
    cells[r][31 - col] = mark == '<' ? '>' : mark == '/' ? '\\' : mark;
  }
  // One deliberate asymmetric cheek stripe, on a hash-selected side.
  unsigned accent_row = 7 + marks[0] % 3;
  unsigned accent_col = 19 - profile[accent_row - 3];
  if (marks[1] & 1)
    accent_col = 31 - accent_col;
  cells[accent_row][accent_col] = '!';
  cells[accent_row + 1][accent_col] = '!';

  size_t required = KEYMASK_ROWS * (KEYMASK_WIDTH + 1) + 1;
  for (unsigned r = 0; r < 16; ++r)
    for (unsigned c = 0; c < 32; ++c)
      if (unicode && cells[r][c] == '#')
        required += 2;
  if (size < required)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Keymask output needs %zu bytes", required);
  char *p = out;
  const char border[] = "+--------------------------------+\n";
  memcpy(p, border, sizeof(border) - 1);
  p += sizeof(border) - 1;
  for (unsigned r = 0; r < 16; ++r) {
    *p++ = '|';
    for (unsigned c = 0; c < 32; ++c) {
      if (unicode && cells[r][c] == '#') {
        memcpy(p, "\xe2\x96\x88", 3);
        p += 3;
      } else {
        *p++ = cells[r][c];
      }
    }
    *p++ = '|';
    *p++ = '\n';
  }
  memcpy(p, border, sizeof(border));
  return ASCIICHAT_OK;
}
