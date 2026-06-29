#include "diagnostics.h"
#include "../session/session.h"
#include "../channel/channel.h"
#include "../reassembly/reassembly.h"
#include "../flow/flow.h"
#include "../route/route.h"
#include <stdio.h>
#include <string.h>

/* Debug report generators for troubleshooting */

int smux_debug_session(const smux_session_t *session, char *buf, size_t len) {
    if (!session || !buf || len == 0) return 0;
    const char *state_names[] = {"IDLE", "HANDSHAKE", "ESTABLISHED", "DRAINING", "CLOSED"};
    const char *state = (session->state <= SMUX_SESS_CLOSED) ?
                        state_names[session->state] : "UNKNOWN";

    return snprintf(buf, len,
        "Session State: %s\n"
        "Version: %u.%u (remote %u.%u)\n"
        "Window: %u (negotiated)\n"
        "Sequences: local=%u remote=%u\n"
        "Keepalive: %u ms\n"
        "Compression: %s\n"
        "Encryption: %s\n",
        state,
        session->local_version_major, session->local_version_minor,
        session->remote_version_major, session->remote_version_minor,
        session->negotiated_window,
        session->next_seq, session->remote_seq,
        session->keepalive_ms,
        session->compression_enabled ? "enabled" : "disabled",
        session->encryption_enabled ? "enabled" : "disabled"
    );
}

int smux_debug_channel(const smux_channel_t *ch, char *buf, size_t len) {
    if (!ch || !buf || len == 0) return 0;
    const char *state_names[] = {"CLOSED", "OPENING", "OPEN", "DRAINING", "ERROR", "MIGRATING"};
    const char *state = (ch->state <= SMUX_CHAN_MIGRATING) ?
                        state_names[ch->state] : "UNKNOWN";

    return snprintf(buf, len,
        "Channel %u: %s\n"
        "  Seq: local=%u remote=%u\n"
        "  Window: %u/%u used (credit=%u)\n"
        "  Priority: %u, Route: %u\n"
        "  Bytes: sent=%u recv=%u\n"
        "  Fragments pending: %u\n"
        "  Reassembly: %s\n",
        ch->channel_id, state,
        ch->local_seq, ch->remote_seq,
        ch->window_used, ch->window_size, ch->credit,
        ch->priority, ch->route_id,
        ch->bytes_sent, ch->bytes_received,
        ch->fragments_pending,
        ch->has_reassembly ? "active" : "none"
    );
}

int smux_debug_channel_table(const smux_channel_table_t *table, char *buf, size_t len) {
    if (!table || !buf || len == 0) return 0;
    int written = snprintf(buf, len, "Active channels: %u (draining: %u)\n",
                          table->active_count, table->draining_count);
    if (written < 0 || (size_t)written >= len) return written;

    for (int i = 0; i < SMUX_MAX_CHANNELS && (size_t)written < len - 100; i++) {
        if (table->channels[i].state != SMUX_CHAN_CLOSED) {
            int n = smux_debug_channel(&table->channels[i],
                                       buf + written, len - (size_t)written);
            if (n > 0) written += n;
        }
    }
    return written;
}

int smux_debug_route_table(const smux_route_table_t *rt, char *buf, size_t len) {
    if (!rt || !buf || len == 0) return 0;
    int written = snprintf(buf, len, "Active routes: %d (primary: %u)\n",
                          rt->active_count, rt->primary_route);

    for (int i = 0; i < SMUX_MAX_ROUTES && (size_t)written < len - 100; i++) {
        if (rt->routes[i].active) {
            int n = snprintf(buf + written, len - (size_t)written,
                "  Route %u: cap=%u lat=%u us, channels=%u, pending_acks=%u\n",
                rt->routes[i].route_id, rt->routes[i].capacity,
                rt->routes[i].latency_us, rt->routes[i].channels_assigned,
                rt->routes[i].pending_acks);
            if (n > 0) written += n;
        }
    }
    return written;
}

int smux_debug_flow(const smux_flow_ctrl_t *fc, char *buf, size_t len) {
    if (!fc || !buf || len == 0) return 0;
    return snprintf(buf, len,
        "Flow: window=%u used=%u credit=%u\n"
        "  High water: %u  Low water: %u\n"
        "  Backpressure: %s  Stalls: %u\n"
        "  Total: granted=%u consumed=%u\n",
        fc->window_size, fc->window_used, fc->credit_available,
        fc->high_water, fc->low_water,
        fc->backpressure_active ? "active" : "off",
        fc->stall_count,
        fc->total_granted, fc->total_consumed
    );
}
