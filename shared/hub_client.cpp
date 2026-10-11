#include "hub_client.h"

#include <windows.h>

const HubApi* HubGet()
{
    static const HubApi* api;
    static bool resolved;
    if (!resolved) {
        resolved = true;
        if (HMODULE menu = GetModuleHandleA("menu.dll"))
            if (auto get = reinterpret_cast<RisenHubApiFn>(GetProcAddress(menu, "RisenHubApi")))
                if (const HubApi* a = get(); a && a->version >= HUB_API_VERSION)
                    api = a;
    }
    return api;
}

bool HubBusy()
{
    const HubApi* api = HubGet();
    return api && api->isBusy();
}
