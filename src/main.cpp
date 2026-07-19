#include "app/AppContext.h"

#include <iostream>

int main()
{
    app::AppContext ctx;
    if (!app::setupAudioGraph(ctx)) {
        std::cerr << "Failed to start audio engine / graph.\n";
        return 1;
    }

    const int code = app::runGui(ctx);

    if (ctx.engine) {
        ctx.engine->stop();
    }
    return code;
}
