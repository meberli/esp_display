// Host test for main/ota_channel.h:
//   g++ -std=c++17 -Wall -I main tools/test_ota_channel.cpp -o t && ./t
#include <cstdio>
#include <cstring>

#include "ota_channel.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  using ota_channel::Release;
  using ota_channel::pick;
  using ota_channel::skip_add;
  using ota_channel::skip_has;

  // skip list: leading v, duplicates, cap
  char skip[192] = "";
  CHECK(!skip_has(skip, "0.9.14-beta.1"));
  skip_add(skip, sizeof(skip), "v0.9.14-beta.1");
  CHECK(skip_has(skip, "0.9.14-beta.1"));
  CHECK(skip_has(skip, "v0.9.14-beta.1"));
  CHECK(!skip_has(skip, "0.9.14-beta.2"));
  CHECK(!skip_has(skip, "0.9.14"));
  skip_add(skip, sizeof(skip), "0.9.14-beta.1");  // duplicate
  CHECK(strcmp(skip, "0.9.14-beta.1") == 0);
  skip_add(skip, sizeof(skip), "0.9.14-beta.2");
  CHECK(skip_has(skip, "0.9.14-beta.2"));
  char many[192] = "";
  for (int i = 1; i <= 10; ++i) {
    char v[16];
    snprintf(v, sizeof(v), "0.9.14-beta.%d", i);
    skip_add(many, sizeof(many), v);
  }
  CHECK(!skip_has(many, "0.9.14-beta.1"));  // oldest dropped (cap 8)
  CHECK(!skip_has(many, "0.9.14-beta.2"));
  CHECK(skip_has(many, "0.9.14-beta.3"));
  CHECK(skip_has(many, "0.9.14-beta.10"));

  const Release rels[] = {
      {"0.9.13", false, false, "https://ex/0.9.13.bin"},
      {"0.9.14-beta.1", true, false, "https://ex/0.9.14-beta.1.bin"},
      {"0.9.14-beta.2", true, false, "https://ex/0.9.14-beta.2.bin"},
      {"0.9.12", false, false, "https://ex/0.9.12.bin"},
      {"0.9.15-beta.1", true, true, "https://ex/draft.bin"},  // draft: ignored
      {"0.9.14-beta.3", true, false, ""},                     // no asset for this board
  };

  // Stable channel: only 0.9.13 is newer than 0.9.12 (betas excluded)
  CHECK(pick(rels, 6, false, "0.9.12", "") == 0);
  CHECK(pick(rels, 6, false, "0.9.13", "") < 0);
  CHECK(pick(rels, 6, false, "v0.9.13", "") < 0);

  // Beta channel: newest newer signed candidate is 0.9.14-beta.2
  int i = pick(rels, 6, true, "0.9.13", "");
  CHECK(i == 2);
  i = pick(rels, 6, true, "0.9.12", "");
  CHECK(i == 2);

  // Running a beta: a later beta is offered; the final of the same version would be too
  const Release with_final[] = {
      {"0.9.14-beta.1", true, false, "https://ex/b1.bin"},
      {"0.9.14-beta.2", true, false, "https://ex/b2.bin"},
      {"0.9.14", false, false, "https://ex/final.bin"},
  };
  CHECK(pick(with_final, 3, true, "0.9.14-beta.1", "") == 2);  // 0.9.14 > beta.2
  CHECK(pick(with_final, 2, true, "0.9.14-beta.1", "") == 1);  // only betas: beta.2
  CHECK(pick(with_final, 3, false, "0.9.14-beta.1", "") == 2); // stable channel still offers the final
  CHECK(pick(with_final, 3, false, "0.9.14", "") < 0);

  // Skip a failed beta: take the next-newest remaining, not the failed one
  CHECK(pick(rels, 6, true, "0.9.13", "0.9.14-beta.2") == 1);  // falls back to beta.1
  CHECK(pick(rels, 6, true, "0.9.13", "0.9.14-beta.1,0.9.14-beta.2") < 0);
  // A later final is still offered after a failed beta of the same base
  CHECK(pick(with_final, 3, true, "0.9.13", "0.9.14-beta.2") == 2);

  // Same version already running: no offer
  CHECK(pick(with_final, 3, true, "0.9.14", "") < 0);
  CHECK(pick(with_final, 3, true, "0.9.14-beta.2", "0.9.14") < 0);  // final skipped, no newer beta

  printf("%d failures\n", fails);
  return fails;
}
