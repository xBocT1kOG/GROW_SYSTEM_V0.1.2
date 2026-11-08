#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/services/network_protocol.h"
#include "../../src/tasks/network_sync.h"

static bool parse(const char *json, network_controls_t *controls)
{
    return network_parse_controls(json, strlen(json), controls);
}

int main(void)
{
    network_controls_t controls = {0};
    assert(parse("{\"v3\":1,\"v5\":55,\"v7\":0}", &controls));
    assert(controls.pump && controls.light == 55 && !controls.automatic);
    assert(parse("{\"v3\":[\"0\"],\"v5\":\"125\",\"v7\":[1],\"v8\":2}", &controls));
    assert(!controls.pump && controls.light == 100 && controls.automatic);
    assert(parse("{\"v3\":0,\"v5\":-20,\"v7\":1}", &controls));
    assert(controls.light == 0);
    const char *invalid[] = {
        "", "{}", "[]", "{\"error\":\"failure\"}",
        "{\"v3\":2,\"v5\":55,\"v7\":0}",
        "{\"v3\":1,\"v5\":55.5,\"v7\":0}",
        "{\"v3\":1,\"v5\":\"55junk\",\"v7\":0}",
        "{\"v3\":1,\"v5\":1e99,\"v7\":0}",
        "{\"v3\":1,\"v5\":[1,2],\"v7\":0}",
        "{\"v3\":1,\"v5\":55,\"v7\":3}",
        "{\"v3\":1,\"v5\":55,\"v7\":0}junk"
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        controls = (network_controls_t){true, 42, true};
        assert(!parse(invalid[i], &controls));
        assert(controls.pump && controls.light == 42 && controls.automatic);
    }
    network_sync_t sync = {0};
    assert(network_sync_changed(&sync, 1, 55));
    // No baseline update on queue failure: the same command remains retryable.
    assert(network_sync_changed(&sync, 1, 55));
    network_sync_observe(&sync, 1, 55);
    assert(!network_sync_changed(&sync, 1, 55));
    // A local 60% command is uploaded; its cloud echo is not a remote command.
    network_sync_observe(&sync, 1, 60);
    assert(!network_sync_changed(&sync, 1, 60));
    assert(network_sync_changed(&sync, 1, 35));
    puts("Network parsing and cloud baseline tests passed");
    return 0;
}
