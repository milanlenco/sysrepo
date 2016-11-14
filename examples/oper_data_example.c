/**
 * @file oper_data_example.c
 * @author Rastislav Szabo <raszabo@cisco.com>, Lukas Macko <lmacko@cisco.com>
 * @brief Example usage operational data API.
 *
 * @copyright
 * Copyright 2016 Cisco Systems, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <inttypes.h>

#include "sysrepo.h"
#include "sysrepo/values.h"
#include "sysrepo/xpath.h"

volatile int exit_application = 0;

static int
data_provider_cb(const char *xpath, sr_val_t **values, size_t *values_cnt, void *private_ctx)
{
    sr_val_t *v = NULL;
    int rc = SR_ERR_OK;

    printf("Data for '%s' requested.\n", xpath);

    /* allocate space for data to return */
    rc = sr_new_values(2, &v);
    if (SR_ERR_OK != rc) {
        return rc;
    }

    sr_val_set_xpath(&v[0], "/dummy-amp:amplifier/stage-1/sensors/gain");
    v[0].type = SR_DECIMAL64_T;
    v[0].data.decimal64_val = 10.5;

    sr_val_set_xpath(&v[1], "/dummy-amp:amplifier/stage-1/sensors/signal-loss");
    v[1].type = SR_BOOL_T;
    v[1].data.bool_val = true;

    *values = v;
    *values_cnt = 2;

    return SR_ERR_OK;
}

static void
sigint_handler(int signum)
{
    exit_application = 1;
}

static int
data_provider(sr_session_ctx_t *session)
{
    sr_subscription_ctx_t *subscription = NULL;
    int rc = SR_ERR_OK;

    /* subscribe for providing operational data */
    rc = sr_dp_get_items_subscribe(session, "/dummy-amp:amplifier/stage-1/sensors", data_provider_cb, NULL,
            SR_SUBSCR_DEFAULT, &subscription);
    if (SR_ERR_OK != rc) {
        fprintf(stderr, "Error by sr_dp_get_items_subscribe: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    printf("\n\n ========== SUBSCRIBED FOR PROVIDING OPER DATA ==========\n\n");

    /* loop until ctrl-c is pressed / SIGINT is received */
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    while (!exit_application) {
        sleep(1000);  /* or do some more useful work... */
    }

    printf("Application exit requested, exiting.\n");

cleanup:
    if (NULL != subscription) {
        sr_unsubscribe(session, subscription);
    }
    return rc;
}

static int
data_requester(sr_session_ctx_t *session)
{
    sr_val_t *value = NULL;
    sr_val_iter_t *iter = NULL;
    int rc = SR_ERR_OK;

    /* get all list instances with their content (recursive) */
    rc = sr_get_items_iter(session, "/dummy-amp:amplifier/stage-1//*", &iter);
    if (SR_ERR_OK != rc) {
        return rc;
    }

    while (SR_ERR_OK == sr_get_item_next(session, iter, &value)) {
        sr_print_val(value);
        sr_free_val(value);
    }
    sr_free_val_iter(iter);

    return SR_ERR_OK;
}

static int
module_change_cb(sr_session_ctx_t *session, const char *module_name, sr_notif_event_t event, void *private_ctx)
{
    printf("Running configuration of the module %s has changed.\n", module_name);
    return 0;
}

int
main(int argc, char **argv)
{
    sr_conn_ctx_t *connection = NULL;
    sr_session_ctx_t *session = NULL;
    sr_subscription_ctx_t *subscription = NULL;
    int rc = SR_ERR_OK;

    /* connect to sysrepo */
    rc = sr_connect("example_application", SR_CONN_DEFAULT, &connection);
    if (SR_ERR_OK != rc) {
        fprintf(stderr, "Error by sr_connect: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    /* start session */
    rc = sr_session_start(connection, SR_DS_RUNNING, SR_SESS_DEFAULT, &session);
    if (SR_ERR_OK != rc) {
        fprintf(stderr, "Error by sr_session_start: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    /* subscribe for changes in running config */
    rc = sr_module_change_subscribe(session, "dummy-amp", module_change_cb, NULL,
            0, SR_SUBSCR_DEFAULT, &subscription);
    if (SR_ERR_OK != rc) {
        fprintf(stderr, "Error by sr_module_change_subscribe: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    if (1 == argc) {
        /* run as a data provider */
        printf("This application will be a data provider for state data of dummy-amp.\n");
        printf("Run the same executable with one (any) argument to request some data.\n");
        rc = data_provider(session);
    } else {
        /* run as a data requester */
        printf("Requesting state data of dummy-amp:\n");
        rc = data_requester(session);
    }

cleanup:
    if (NULL != subscription) {
        sr_unsubscribe(session, subscription);
    }
    if (NULL != session) {
        sr_session_stop(session);
    }
    if (NULL != connection) {
        sr_disconnect(connection);
    }
    return rc;
}
