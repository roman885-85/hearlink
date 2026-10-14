// Раздача обновления (hearlink/ota.h): выбор следующего нужного блока по кругу. Алгоритм здесь повторён —
// если меняется в ota.h, поменять и тут. До 2.67 блок прямо перед текущим местом не находился никогда (см. ota.h).
#include <cstdio>
#include <cstdint>
#include <vector>
static uint32_t pickNew(const std::vector<uint32_t> &need, uint32_t blocks, uint32_t idx) {
  uint32_t words = (blocks + 31) / 32, b = UINT32_MAX;
  for (uint32_t step = 0; step <= words && b == UINT32_MAX; step++) {
    uint32_t w = ((idx >> 5) + step) % words, m = need[w];
    if (step == 0) m &= ~0u << (idx & 31);
    else if (step == words) m &= ~(~0u << (idx & 31));
    if (m) b = w * 32 + __builtin_ctz(m);
  }
  return b;
}
static uint32_t pickOld(const std::vector<uint32_t> &need, uint32_t blocks, uint32_t idx) {
  uint32_t b = idx, tries = 0;
  while (tries < blocks) {
    if ((b & 31) == 0 && !need[b >> 5]) { tries += 32; b += 32; if (b >= blocks) b = 0; continue; }
    if (need[b >> 5] & (1u << (b & 31))) break;
    tries++;
    if (++b >= blocks) b = 0;
  }
  return tries >= blocks ? UINT32_MAX : b;
}
int main() {
  int fail = 0;
  for (uint32_t blocks : { 11173u, 11169u, 11168u, 33u, 1u, 64u, 95u }) {
    uint32_t words = (blocks + 31) / 32;
    long badOld = 0, badNew = 0, n = 0, wrongOrder = 0;
    for (uint32_t idx = 0; idx < blocks; idx += (blocks > 200 ? 7 : 1))
      for (uint32_t k = 0; k < blocks; k += (blocks > 200 ? 13 : 1)) {
        std::vector<uint32_t> need(words, 0);
        need[k >> 5] |= 1u << (k & 31);
        n++;
        if (pickOld(need, blocks, idx) != k) badOld++;
        if (pickNew(need, blocks, idx) != k) badNew++;
        // два блока: должен выбрать ближайший по кругу от idx
        uint32_t k2 = (k + 40) % blocks;
        need[k2 >> 5] |= 1u << (k2 & 31);
        uint32_t d1 = (k + blocks - idx) % blocks, d2 = (k2 + blocks - idx) % blocks;
        if (pickNew(need, blocks, idx) != (d1 <= d2 ? k : k2)) wrongOrder++;
      }
    std::vector<uint32_t> empty(words, 0);
    bool emptyOk = pickNew(empty, blocks, blocks / 2) == UINT32_MAX;
    printf("otascan: блоков %5u, случаев %ld; прежний способ не находил блок %ld раз, новый — %ld; порядок по кругу нарушен %ld раз; пустая карта: %s\n", blocks, n, badOld, badNew,
           wrongOrder, emptyOk ? "ничего (верно)" : "ОШИБКА");
    if (badNew || wrongOrder || !emptyOk) fail = 1;
  }
  return fail;
}
