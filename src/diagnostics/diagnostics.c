#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>

smux_diagnostics_t *smux_diag_create(void) {
    smux_diagnostics_t *d = calloc(1, sizeof(smux_diagnostics_t));
    if (!d) return NULL;
    d->enabled = true;
    return d;
}

void smux_diag_destroy(smux_diagnostics_t *d) {
    free(d);
}

void smux_diag_reset(smux_diagnostics_t *d) {
    if (!d) return;
    bool was_enabled = d->enabled;
    memset(&d->stats, 0, sizeof(d->stats));
    d->enabled = was_enabled;
}

void smux_diag_enable(smux_diagnostics_t *d) { if (d) d->enabled = true; }
void smux_diag_disable(smux_diagnostics_t *d) { if (d) d->enabled = false; }
