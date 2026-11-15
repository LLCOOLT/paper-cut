/* Online play on Apple's systems: the network's changes (Network.framework's path monitor), for
 * port/src/common/statesync_net_pc.c, which connects again at once on one instead of waiting for the old
 * connection to go quiet: Wi-Fi to cellular, the connection lost or back, a cable in or out.
 *
 * portAppleNetChanges: a count, one more each time the path's state or its kind of interface changes (the
 * first path, the one the app started on, isn't a change). The monitor starts on the first call and runs
 * for the app's life on a queue of its own; the count is read from any thread. */
#import <Foundation/Foundation.h>
#import <Network/Network.h>
#include <stdatomic.h>

static atomic_uint sChanges;

/* the path's kind of interface: 1 Wi-Fi, 2 cellular, 3 wired, 0 another or none */
static int kindOf(nw_path_t path) {
    if (nw_path_uses_interface_type(path, nw_interface_type_wifi)) {
        return 1;
    }
    if (nw_path_uses_interface_type(path, nw_interface_type_cellular)) {
        return 2;
    }
    if (nw_path_uses_interface_type(path, nw_interface_type_wired)) {
        return 3;
    }
    return 0;
}

unsigned portAppleNetChanges(void) {
    static dispatch_once_t sOnce;
    static nw_path_monitor_t sMonitor;
    dispatch_once(&sOnce, ^{
      dispatch_queue_t queue = dispatch_queue_create("papercut.netpath", DISPATCH_QUEUE_SERIAL);
      /* (the last path's state and kind: only the monitor's queue touches them) */
      __block int known = 0, lastKind = 0;
      __block nw_path_status_t lastStatus = nw_path_status_invalid;
      sMonitor = nw_path_monitor_create();
      nw_path_monitor_set_queue(sMonitor, queue);
      nw_path_monitor_set_update_handler(sMonitor, ^(nw_path_t path) {
        const nw_path_status_t status = nw_path_get_status(path);
        const int kind = kindOf(path);
        if (known && (status != lastStatus || kind != lastKind)) {
            atomic_fetch_add(&sChanges, 1u);
        }
        known = 1;
        lastStatus = status;
        lastKind = kind;
      });
      nw_path_monitor_start(sMonitor);
    });
    return atomic_load(&sChanges);
}
