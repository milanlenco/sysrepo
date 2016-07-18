/**
 * @file dm_test.c
 * @author Rastislav Szabo <raszabo@cisco.com>, Lukas Macko <lmacko@cisco.com>
 * @brief Data Manager unit tests.
 *
 * @copyright
 * Copyright 2015 Cisco Systems, Inc.
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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <stdio.h>
#include "data_manager.h"
#include "test_data.h"
#include "sr_common.h"
#include "test_module_helper.h"
#include "rp_dt_lookup.h"
#include "rp_dt_xpath.h"

int setup(void **state)
{
    /* make sure that test-module data is created */
    createDataTreeTestModule();
    createDataTreeExampleModule();
    return 0;
}

void dm_create_cleanup(void **state){
   int rc;
   dm_ctx_t *ctx;
   rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
   assert_int_equal(SR_ERR_OK,rc);

   dm_cleanup(ctx);

}

static struct lyd_node *
dm_lyd_new_leaf(dm_data_info_t *data_info, struct lyd_node *parent, const struct lys_module *module, const char *node_name, const char *value)
{
    int rc = SR_ERR_OK;
    CHECK_NULL_ARG_NORET4(rc, data_info, module, node_name, value);
    if (SR_ERR_OK != rc){
        return NULL;
    }

    struct lyd_node *new = NULL;
    new = lyd_new_leaf(parent, module, node_name, value);

    if (NULL == parent) {
        if (NULL == data_info->node) {
            data_info->node = new;
        } else {
            struct lyd_node *last_sibling = data_info->node;
            while (NULL != last_sibling->next) {
                last_sibling = last_sibling->next;
            }
            if (0 != lyd_insert_after(last_sibling, new)) {
                SR_LOG_ERR_MSG("Append of top level node failed");
                lyd_free(new);
                return NULL;
            }
        }
    }

    return new;
}

void dm_get_data_tree(void **state)
{
    int rc;
    dm_ctx_t *ctx;
    dm_session_t *ses_ctx;
    struct lyd_node *data_tree;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    dm_session_start(ctx, NULL, SR_DS_STARTUP, &ses_ctx);
    /* Load from file */
    assert_int_equal(SR_ERR_OK, dm_get_datatree(ctx, ses_ctx ,"example-module", &data_tree));
    /* Get from avl tree */
    assert_int_equal(SR_ERR_OK, dm_get_datatree(ctx, ses_ctx ,"example-module", &data_tree));
    /* Module without data - return OK, because default leaves are automatically added */
    assert_int_equal(SR_ERR_OK, dm_get_datatree(ctx, ses_ctx ,"small-module", &data_tree));
    /* Not existing module should return an error*/
    assert_int_equal(SR_ERR_UNKNOWN_MODEL, dm_get_datatree(ctx, ses_ctx ,"not-existing-module", &data_tree));

    dm_session_stop(ctx, ses_ctx);

    dm_cleanup(ctx);

}

void
dm_list_schema_test(void **state)
{
    int rc;
    dm_ctx_t *ctx;
    dm_session_t *ses_ctx;
    sr_schema_t *schemas;
    size_t count;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &ses_ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_list_schemas(ctx, ses_ctx, &schemas, &count);
    assert_int_equal(SR_ERR_OK, rc);

    for (size_t i = 0; i < count; i++) {
        printf("\n\nSchema #%zu:\n%s\n%s\n%s\n", i,
                schemas[i].module_name,
                schemas[i].ns,
                schemas[i].prefix);
            printf("\t%s\n\t%s\n\t%s\n\n",
                    schemas[i].revision.revision,
                    schemas[i].revision.file_path_yang,
                    schemas[i].revision.file_path_yin);


        for (size_t s = 0; s < schemas[i].submodule_count; s++) {
            printf("\t%s\n", schemas[i].submodules[s].submodule_name);

               printf("\t\t%s\n\t\t%s\n\t\t%s\n\n",
                       schemas[i].submodules[s].revision.revision,
                       schemas[i].submodules[s].revision.file_path_yang,
                       schemas[i].submodules[s].revision.file_path_yin);

        }
    }

    sr_free_schemas(schemas, count);

    dm_session_stop(ctx, ses_ctx);

    dm_cleanup(ctx);
}

void
dm_get_schema_test(void **state)
{
    int rc;
    dm_ctx_t *ctx;
    char *schema = NULL;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    /* module latest revision */
    rc = dm_get_schema(ctx, "module-a", NULL, NULL, true, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    /* module latest revision  yin format*/
    rc = dm_get_schema(ctx, "module-a", NULL, NULL, false, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    /* module-b latest revision which depends on module-a older revision */
    rc = dm_get_schema(ctx, "module-b", NULL, NULL, true, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    /* module selected revision */
    rc = dm_get_schema(ctx, "module-a", "2016-02-02", NULL, true, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    /* submodule latest revision */
    rc = dm_get_schema(ctx, "module-a", NULL, "sub-a-one", true, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    /* submodule selected revision */
    rc = dm_get_schema(ctx, "module-a", "2016-02-02", "sub-a-one", true, &schema);
    assert_int_equal(SR_ERR_OK, rc);
    assert_non_null(schema);
    free(schema);

    dm_cleanup(ctx);

}

void
dm_get_schema_negative_test(void **state)
{

    int rc;
    dm_ctx_t *ctx;
    char *schema = NULL;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    /* unknown module */
    rc = dm_get_schema(ctx, "unknown", NULL, NULL, true, &schema);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);
    assert_null(schema);


    /* module unknown revision */
    rc = dm_get_schema(ctx, "module-a", "2018-02-02", NULL, true, &schema);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);
    assert_null(schema);


    /* unknown submodule */
    rc = dm_get_schema(ctx, "module-a", NULL, "sub-unknown", true, &schema);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);
    assert_null(schema);

    /* submodule unknown revision */
    rc = dm_get_schema(ctx, "module-a", "2018-02-10", "sub-a-one", true, &schema);
    assert_int_equal(SR_ERR_NOT_FOUND, rc);
    assert_null(schema);

    dm_cleanup(ctx);
}

void
dm_validate_data_trees_test(void **state)
{
    int rc;
    dm_ctx_t *ctx = NULL;
    dm_session_t *ses_ctx = NULL;
    struct lyd_node *node = NULL;
    dm_data_info_t *info = NULL;
    sr_error_info_t *errors = NULL;
    size_t err_cnt = 0;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &ses_ctx);
    assert_int_equal(SR_ERR_OK, rc);

    /* test validation with no data trees copied */
    rc = dm_validate_session_data_trees(ctx, ses_ctx, &errors, &err_cnt);
    assert_int_equal(SR_ERR_OK, rc);
    sr_free_errors(errors, err_cnt);

    /* copy a couple data trees to session*/
    rc = dm_get_data_info(ctx, ses_ctx, "example-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_get_data_info(ctx, ses_ctx, "test-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_validate_session_data_trees(ctx, ses_ctx, &errors, &err_cnt);
    assert_int_equal(SR_ERR_OK, rc);
    sr_free_errors(errors, err_cnt);

    /* make an invalid  change */
    info->modified = true;
    /* already existing leaf */
    node = dm_lyd_new_leaf(info, info->node, info->module, "i8", "42");
    assert_non_null(node);


    rc = dm_validate_session_data_trees(ctx, ses_ctx, &errors, &err_cnt);
    assert_int_equal(SR_ERR_VALIDATION_FAILED, rc);
    sr_free_errors(errors, err_cnt);

    dm_session_stop(ctx, ses_ctx);
    dm_cleanup(ctx);
}

void
dm_discard_changes_test(void **state)
{
    int rc;
    dm_ctx_t *ctx = NULL;
    dm_session_t *ses_ctx = NULL;
    dm_data_info_t *info = NULL;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &ses_ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_get_data_info(ctx, ses_ctx, "test-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_discard_changes(ctx, ses_ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_get_data_info(ctx, ses_ctx, "test-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    /* check current value */
    assert_int_equal(8, ((struct lyd_node_leaf_list *)info->node->child->next->next->next->next)->value.int8);


    /* change leaf i8 value */
    info->modified = true;
    //TODO change to lyd_change_leaf
    ((struct lyd_node_leaf_list *)info->node->child->next->next->next->next)->value.int8 = 100;

    /* we should have the value changed*/
    rc = dm_get_data_info(ctx, ses_ctx, "test-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    assert_int_equal(100, ((struct lyd_node_leaf_list *)info->node->child->next->next->next->next)->value.int8);

    /* discard changes to get current datastore value*/
    rc = dm_discard_changes(ctx, ses_ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_get_data_info(ctx, ses_ctx, "test-module", &info);
    assert_int_equal(SR_ERR_OK, rc);

    assert_int_equal(8, ((struct lyd_node_leaf_list *)info->node->child->next->next->next->next)->value.int8);

    dm_session_stop(ctx, ses_ctx);
    dm_cleanup(ctx);
}

void
dm_add_operation_test(void **state)
{
    int rc;
    dm_ctx_t *ctx = NULL;
    dm_session_t *ses_ctx = NULL;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    dm_session_start(ctx, NULL, SR_DS_STARTUP, &ses_ctx);

    rc = dm_add_operation(ses_ctx, DM_DELETE_OP, NULL, NULL, SR_EDIT_DEFAULT, 0, NULL);
    assert_int_equal(SR_ERR_INVAL_ARG, rc);

    sr_val_t *val = NULL;
    val = calloc(1, sizeof(*val));
    assert_non_null(val);

    val->type = SR_INT8_T;
    val->data.int8_val = 42;

    rc = dm_add_operation(ses_ctx, DM_SET_OP, "/abc:def", val, SR_EDIT_DEFAULT, 0, NULL);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_add_operation(ses_ctx, DM_DELETE_OP, "/abc:def", NULL, SR_EDIT_DEFAULT, 0, NULL);
    assert_int_equal(SR_ERR_OK, rc);

    sr_val_t *val1 = NULL;
    val1 = calloc(1, sizeof(*val1));
    assert_non_null(val1);
    val1->type = SR_STRING_T;
    val1->data.string_val = strdup("abc");

    /* NULL passed in loc_id argument, val1 should be freed */
    rc = dm_add_operation(ses_ctx, DM_SET_OP, NULL, val1, SR_EDIT_DEFAULT, 0, NULL);
    assert_int_equal(SR_ERR_INVAL_ARG, rc);

    dm_session_stop(ctx, ses_ctx);
    dm_cleanup(ctx);

}

void
dm_locking_test(void **state)
{
   int rc;
   dm_ctx_t *ctx = NULL;
   dm_session_t *sessionA = NULL, *sessionB = NULL;

   rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
   assert_int_equal(SR_ERR_OK, rc);

   dm_session_start(ctx, NULL, SR_DS_STARTUP, &sessionA);
   dm_session_start(ctx, NULL, SR_DS_STARTUP, &sessionB);

   rc = dm_lock_module(ctx, sessionA, "example-module");
   assert_int_equal(SR_ERR_OK, rc);

   rc = dm_lock_module(ctx, sessionB, "example-module");
   assert_int_equal(SR_ERR_LOCKED, rc);

   /* automatically release lock by session stop */
   dm_session_stop(ctx, sessionA);

   rc = dm_lock_module(ctx, sessionB, "example-module");
   assert_int_equal(SR_ERR_OK, rc);
   dm_session_stop(ctx, sessionB);
   dm_cleanup(ctx);
}

void
dm_copy_module_test(void **state)
{
   int rc = SR_ERR_OK;
   dm_ctx_t *ctx = NULL;
   dm_session_t *sessionA = NULL;

   rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
   assert_int_equal(SR_ERR_OK, rc);

   rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &sessionA);
   assert_int_equal(SR_ERR_OK, rc);

   rc = dm_copy_module(ctx, sessionA, "example-module", SR_DS_STARTUP, SR_DS_RUNNING);
   assert_int_equal(SR_ERR_OK, rc);

   rc = rp_dt_enable_xpath(ctx, sessionA, "/test-module:main");
   assert_int_equal(SR_ERR_OK, rc);

   rc = dm_copy_all_models(ctx, sessionA, SR_DS_STARTUP, SR_DS_RUNNING);
   assert_int_equal(SR_ERR_OK, rc);

   dm_session_stop(ctx, sessionA);
   dm_cleanup(ctx);
}

void
dm_rpc_test(void **state)
{
    int rc = SR_ERR_OK;
    dm_ctx_t *ctx = NULL;
    dm_session_t *session = NULL;
    sr_val_t *input = NULL, *output = NULL;
    const struct lys_module *module = NULL;
    size_t input_cnt = 0, output_cnt = 0;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &session);
    assert_int_equal(SR_ERR_OK, rc);

    /* load test-module */
    rc = dm_get_module(ctx, "test-module", NULL, &module);
    assert_int_equal(SR_ERR_OK, rc);

    /* non-existing RPC */
    rc = dm_validate_rpc(ctx, session, "/test-module:non-existing-rpc", &input, &input_cnt, true);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* RPC input */
    input_cnt = 1;
    input = calloc(input_cnt, sizeof(*input));
    input[0].xpath = strdup("/test-module:activate-software-image/image-name");
    input[0].type = SR_STRING_T;
    input[0].data.string_val = strdup("acmefw-2.3");

    rc = dm_validate_rpc(ctx, session, "/test-module:activate-software-image", &input, &input_cnt, true);
    assert_int_equal(SR_ERR_OK, rc);
    assert_int_equal(input_cnt, 2); /* including default leaf */

    /* invalid RPC input */
    free(input[0].xpath);
    input[0].xpath = strdup("/test-module:activate-software-image/non-existing-input");
    rc = dm_validate_rpc(ctx, session, "/test-module:activate-software-image", &input, &input_cnt, true);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* RPC output */
    output_cnt = 2;
    output = calloc(output_cnt, sizeof(*output));
    output[0].xpath = strdup("/test-module:activate-software-image/status");
    output[0].type = SR_STRING_T;
    output[0].data.string_val = strdup("The image acmefw-2.3 is being installed.");
    output[1].xpath = strdup("/test-module:activate-software-image/version");
    output[1].type = SR_STRING_T;
    output[1].data.string_val = strdup("2.3");

    rc = dm_validate_rpc(ctx, session, "/test-module:activate-software-image", &output, &output_cnt, false);
    assert_int_equal(SR_ERR_OK, rc);
    assert_int_equal(output_cnt, 3); /* including default leaf */

    /* invalid RPC output */
    free(output[1].xpath);
    output[1].xpath = strdup("/test-module:activate-software-image/non-existing-output");
    rc = dm_validate_rpc(ctx, session, "/test-module:activate-software-image", &output, &output_cnt, false);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    sr_free_values(input, input_cnt);
    sr_free_values(output, output_cnt);

    dm_session_stop(ctx, session);
    dm_cleanup(ctx);
}

void
dm_state_data_test(void **state)
{
    int rc = SR_ERR_OK;
    dm_ctx_t *ctx = NULL;
    dm_session_t *session = NULL;
    bool has_state_data = false;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &session);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_has_state_data(ctx, "ietf-ip", &has_state_data);
    assert_int_equal(SR_ERR_OK, rc);
    assert_false(has_state_data);

    rc = dm_has_state_data(ctx, "ietf-interfaces", &has_state_data);
    assert_int_equal(SR_ERR_OK, rc);
    assert_true(has_state_data);

    rc = dm_has_state_data(ctx, "info-module", &has_state_data);
    assert_int_equal(SR_ERR_OK, rc);
    assert_false(has_state_data);

    rc = dm_has_state_data(ctx, "test-module", &has_state_data);
    assert_int_equal(SR_ERR_OK, rc);
    assert_false(has_state_data);

    rc = dm_has_state_data(ctx, "state-module", &has_state_data);
    assert_int_equal(SR_ERR_OK, rc);
    assert_true(has_state_data);

    dm_session_stop(ctx, session);
    dm_cleanup(ctx);
}

void
dm_event_notif_test(void **state)
{
    int rc = SR_ERR_OK;
    dm_ctx_t *ctx = NULL;
    dm_session_t *session = NULL;
    sr_val_t *values = NULL;
    const struct lys_module *module = NULL;
    size_t values_cnt = 0;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &session);
    assert_int_equal(SR_ERR_OK, rc);

    /* load test-module */
    rc = dm_get_module(ctx, "test-module", NULL, &module);
    assert_int_equal(SR_ERR_OK, rc);

    /* non-existing event notification */
    rc = dm_validate_event_notif(ctx, session, "/test-module:non-existing-event-notif", &values, &values_cnt);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* valid event notification */
    values_cnt = 6;
    values = calloc(values_cnt, sizeof(*values));
    values[0].xpath = strdup("/test-module:link-removed/source");
    values[0].type = SR_CONTAINER_T;
    values[0].data.uint64_val = 0;
    values[1].xpath = strdup("/test-module:link-removed/source/address");
    values[1].type = SR_STRING_T;
    values[1].data.string_val = strdup("10.10.2.4");
    values[2].xpath = strdup("/test-module:link-removed/source/interface");
    values[2].type = SR_STRING_T;
    values[2].data.string_val = strdup("eth0");
    values[3].xpath = strdup("/test-module:link-removed/destination");
    values[3].type = SR_CONTAINER_T;
    values[3].data.uint64_val = 0;
    values[4].xpath = strdup("/test-module:link-removed/destination/address");
    values[4].type = SR_STRING_T;
    values[4].data.string_val = strdup("10.10.2.5");
    values[5].xpath = strdup("/test-module:link-removed/destination/interface");
    values[5].type = SR_STRING_T;
    values[5].data.string_val = strdup("eth2");

    rc = dm_validate_event_notif(ctx, session, "/test-module:link-removed", &values, &values_cnt);
    assert_int_equal(SR_ERR_OK, rc);
    /* including default leaf */
    assert_int_equal(values_cnt, 7);
    assert_string_equal("/test-module:link-removed/MTU", values[6].xpath);
    assert_int_equal(SR_UINT16_T, values[6].type);
    assert_int_equal(1500, values[6].data.uint16_val);

    /* invalid event notification values */
    free(values[6].xpath);
    values[6].xpath = strdup("/test-module:link-removed/non-existing-node");
    rc = dm_validate_event_notif(ctx, session, "/test-module:link-removed", &values, &values_cnt);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* notification nested in the data tree (YANG 1.1) */
    sr_free_values(values, values_cnt);
    values_cnt = 2;
    values = calloc(values_cnt, sizeof(*values));
    values[0].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/status-change/loaded");
    values[0].type = SR_BOOL_T;
    values[0].data.bool_val = true;
    values[1].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/status-change/time-of-change");
    values[1].type = SR_UINT32_T;
    values[1].data.int32_val = 1468827615;
    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/status-change",
            &values, &values_cnt, true);
    assert_int_equal(SR_ERR_OK, rc);
    assert_int_equal(values_cnt, 2);

    /* (nested) notification not present in the data tree */
    sr_free_values(values, values_cnt);
    values_cnt = 2;
    values = calloc(values_cnt, sizeof(*values));
    values[0].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"non-existent-module\"]/status-change/loaded");
    values[0].type = SR_BOOL_T;
    values[0].data.bool_val = true;
    values[1].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"non-existent-module\"]/status-change/time-of-change");
    values[1].type = SR_UINT32_T;
    values[1].data.int32_val = 1468827615;
    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"non-existent-module\"]/status-change",
            &values, &values_cnt, true);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    sr_free_values(values, values_cnt);
    dm_session_stop(ctx, session);
    dm_cleanup(ctx);
}

void
dm_action_test(void **state)
{
    int rc = SR_ERR_OK;
    dm_ctx_t *ctx = NULL;
    dm_session_t *session = NULL;
    sr_val_t *input = NULL, *output = NULL;
    const struct lys_module *module = NULL;
    size_t input_cnt = 0, output_cnt = 0;

    rc = dm_init(NULL, NULL, NULL, CM_MODE_LOCAL, TEST_SCHEMA_SEARCH_DIR, TEST_DATA_SEARCH_DIR, &ctx);
    assert_int_equal(SR_ERR_OK, rc);

    rc = dm_session_start(ctx, NULL, SR_DS_STARTUP, &session);
    assert_int_equal(SR_ERR_OK, rc);

    /* load test-module */
    rc = dm_get_module(ctx, "test-module", NULL, &module);
    assert_int_equal(SR_ERR_OK, rc);

    /* non-existing action in the schema tree */
    rc = dm_validate_action(ctx, session, "/test-module:non-existing-action",
            &input, &input_cnt, true);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* action input */
    input_cnt = 1;
    input = calloc(input_cnt, sizeof(*input));
    input[0].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load/params");
    input[0].type = SR_STRING_T;
    input[0].data.string_val = strdup("--log-level 2");

    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load",
            &input, &input_cnt, true);
    assert_int_equal(SR_ERR_OK, rc);
    assert_int_equal(input_cnt, 3); /* including default leafs */
    assert_string_equal("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load/params", input[0].xpath);
    assert_int_equal(SR_STRING_T, input[0].type);
    assert_string_equal("--log-level 2", input[0].data.string_val);
    assert_string_equal("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load/force", input[1].xpath);
    assert_int_equal(SR_BOOL_T, input[1].type);
    assert_false(input[1].data.bool_val);
    assert_string_equal("/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load/dry-run", input[2].xpath);
    assert_int_equal(SR_BOOL_T, input[2].type);
    assert_false(input[2].data.bool_val);

    /* non-existing location of the Action in the data tree */
    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"non-existent-module\"]/load",
            &input, &input_cnt, true);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    /* invalid action input */
    input[2].type = SR_UINT16_T;
    input[2].data.uint16_val = 1;
    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"irqbypass.ko\"]/load",
            &input, &input_cnt, true);
    assert_int_equal(SR_ERR_VALIDATION_FAILED, rc);

    /* action output */
    output_cnt = 3;
    output = calloc(output_cnt, sizeof(*output));
    output[0].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies/dependency");
    output[0].type = SR_STRING_T;
    output[0].data.string_val = strdup("drm");
    output[1].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies/dependency");
    output[1].type = SR_STRING_T;
    output[1].data.string_val = strdup("drm_kms_helper");
    output[2].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies/dependency");
    output[2].type = SR_STRING_T;
    output[2].data.string_val = strdup("ttm");

    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies",
            &output, &output_cnt, false);
    assert_int_equal(SR_ERR_OK, rc);
    assert_int_equal(output_cnt, 3);

    /* invalid action output */
    free(output[2].xpath);
    free(output[2].data.string_val);
    output[2].xpath = strdup("/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies/return-code");
    output[2].type = SR_UINT8_T;
    output[2].data.uint8_val = 0;
    rc = dm_validate_action(ctx, session, "/test-module:kernel-modules/kernel-module[name=\"vboxvideo.ko\"]/get-dependencies",
            &output, &output_cnt, false);
    assert_int_equal(SR_ERR_BAD_ELEMENT, rc);

    sr_free_values(input, input_cnt);
    sr_free_values(output, output_cnt);

    dm_session_stop(ctx, session);
    dm_cleanup(ctx);
}

int main(){
    sr_log_stderr(SR_LL_DBG);

    const struct CMUnitTest tests[] = {
            cmocka_unit_test(dm_create_cleanup),
            cmocka_unit_test(dm_get_data_tree),
            cmocka_unit_test(dm_list_schema_test),
            cmocka_unit_test(dm_validate_data_trees_test),
            cmocka_unit_test(dm_discard_changes_test),
            cmocka_unit_test(dm_get_schema_test),
            cmocka_unit_test(dm_get_schema_negative_test),
            cmocka_unit_test(dm_add_operation_test),
            cmocka_unit_test(dm_locking_test),
            cmocka_unit_test(dm_copy_module_test),
            cmocka_unit_test(dm_rpc_test),
            cmocka_unit_test(dm_state_data_test),
            cmocka_unit_test(dm_event_notif_test),
            cmocka_unit_test(dm_action_test)
    };
    return cmocka_run_group_tests(tests, setup, NULL);
}

