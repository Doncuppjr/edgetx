#pragma once
#include <functional>
#include <list>
struct lv_group_t { bool alive; unsigned id; };
struct lv_indev_t { lv_group_t* group; };
enum { OPAQUE = 1 };
class Window {
 public:
  bool hasWindowFlag(unsigned) const { return false; }
  bool isPageGroup() const { return false; }
};
lv_group_t* lv_group_create();
void lv_group_del(lv_group_t*);
lv_group_t* lv_group_get_default();
void lv_group_set_default(lv_group_t*);
lv_indev_t* lv_indev_get_next(lv_indev_t*);
void lv_indev_set_group(lv_indev_t*, lv_group_t*);
