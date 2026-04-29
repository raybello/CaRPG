#include <cstdio>
#include "app.h"

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    if (!app.initWindow())  return 1;
    if (!app.initImGui())   return 1;
    if (!app.initScene())   return 1;
    app.run();
    app.shutdown();
    return 0;
}
