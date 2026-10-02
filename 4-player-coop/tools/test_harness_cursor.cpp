#include "../runtime/harness_cursor.h"
#include <cassert>
#include <cstdio>

int main() {
  HarnessCursor first, second;
  POINT point{};
  assert(first.SetPosition(-100, 250));
  assert(first.GetPosition(&point) && point.x == -100 && point.y == 250);
  assert(second.GetPosition(&point) && point.x == 0 && point.y == 0);
  assert(!first.GetPosition(nullptr) && GetLastError() == ERROR_INVALID_PARAMETER);
  assert(first.Show(FALSE) == -1 && first.Show(FALSE) == -2);
  assert(second.Show(TRUE) == 1 && first.Show(TRUE) == -1);
  HCURSOR shape = reinterpret_cast<HCURSOR>(0x1234);
  assert(first.SetShape(shape) == nullptr);
  assert(first.SetShape(nullptr) == shape);
  assert(second.SetShape(nullptr) == nullptr);
  std::puts("Harness cursor: private position, display count, shape and invalid pointer behavior; no desktop API calls");
}
