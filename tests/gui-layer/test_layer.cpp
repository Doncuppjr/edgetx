#include "layer.h"
#include <cassert>
#include <memory>
#include <vector>

static std::vector<std::unique_ptr<lv_group_t>> groups;
static lv_group_t* current = nullptr;
static lv_indev_t input{nullptr};
lv_group_t* lv_group_create() {
  groups.emplace_back(new lv_group_t{true, unsigned(groups.size())});
  return groups.back().get();
}
void lv_group_del(lv_group_t* g) {
  assert(g && g->alive);
  // Match LVGL: deleting a group clears its current device/default links,
  // but cannot repair restoration pointers held in EdgeTX's layer stack.
  if (current == g) current = nullptr;
  if (input.group == g) input.group = nullptr;
  g->alive = false;
}
lv_group_t* lv_group_get_default() { return current; }
void lv_group_set_default(lv_group_t* g) {
  assert(!g || g->alive); current = g;
}
lv_indev_t* lv_indev_get_next(lv_indev_t* p) { return p ? nullptr : &input; }
void lv_indev_set_group(lv_indev_t* d, lv_group_t* g) {
  assert(!g || g->alive); d->group = g;
}

int main() {
  Window a,b,c,d,unknown;
  // USB/menu removal under a live modal must not remove the active layer.
  Layer::push(&a); auto ga=current;
  Layer::push(&b); auto gb=current;
  Layer::push(&c); auto gc=current;
  Layer::pop(&b);
  assert(Layer::back()==&c && current==gc && gc->alive && !gb->alive);
  Layer::pop(&c);
  assert(Layer::back()==&a && current==ga);
  Layer::pop(&a);
  assert(!Layer::back() && !current && !input.group);

  // Multiple non-top removals must repair the entire restoration chain.
  Layer::push(&a); ga=current;
  Layer::push(&b); gb=current;
  Layer::push(&c); gc=current;
  Layer::push(&d); auto gd=current;
  Layer::pop(&a); Layer::pop(&c);
  assert(current==gd && !ga->alive && !gc->alive);
  Layer::pop(&d);
  assert(Layer::back()==&b && current==gb);
  Layer::pop(&unknown); assert(current==gb); // absent window is harmless
  Layer::pop(&b);
  Layer::pop(&b); // repeated/empty pop
  assert(!Layer::back() && !current && !input.group);
  for (const auto& g:groups) assert(!g->alive);
}
