/**
 * @file rp_dt_get.c
 * @author Rastislav Szabo <raszabo@cisco.com>, Lukas Macko <lmacko@cisco.com>
 * @brief
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

#include <libyang/libyang.h>
#include <pthread.h>
#include "sysrepo.h"
#include "sr_common.h"

#include "access_control.h"
#include "rp_internal.h"
#include "rp_dt_get.h"
#include "rp_dt_xpath.h"
#include "rp_dt_edit.h"

/**
 * @brief Fills sr_val_t from lyd_node structure. It fills xpath and copies the value.
 * @param [in] node
 * @param [out] value
 * @return Error code (SR_ERR_OK on success)
 */
static int
rp_dt_get_value_from_node(struct lyd_node *node, sr_val_t *val)
{
    CHECK_NULL_ARG3(node, val, node->schema);

    int rc = SR_ERR_OK;
    char *xpath = NULL;
    struct lyd_node_leaf_list *data_leaf = NULL;
    struct lys_node_container *sch_cont = NULL;

    rc = rp_dt_create_xpath_for_node(node, &xpath);
    CHECK_RC_MSG_RETURN(rc, "Create xpath for node failed");
    val->xpath = xpath;

    switch (node->schema->nodetype) {
    case LYS_LEAF:
        data_leaf = (struct lyd_node_leaf_list *) node;
        val->dflt = node->dflt;

        if (data_leaf->value_type == LY_TYPE_LEAFREF && NULL == data_leaf->value.leafref) {
            if (0 != lyd_validate_leafref(data_leaf)) {
                SR_LOG_WRN("Cannot resolve leafref \"%s\" just yet.", xpath);
            }
        }

        val->type = sr_libyang_leaf_get_type(data_leaf);

        rc = sr_libyang_leaf_copy_value(data_leaf, val);
        CHECK_RC_MSG_GOTO(rc, cleanup, "Copying of value failed");
        break;
    case LYS_CONTAINER:
        sch_cont = (struct lys_node_container *) node->schema;
        val->type = sch_cont->presence == NULL ? SR_CONTAINER_T : SR_CONTAINER_PRESENCE_T;
        break;
    case LYS_LIST:
        val->type = SR_LIST_T;
        break;
    case LYS_LEAFLIST:
        data_leaf = (struct lyd_node_leaf_list *) node;

        val->type = sr_libyang_leaf_get_type(data_leaf);

        rc = sr_libyang_leaf_copy_value(data_leaf, val);
        CHECK_RC_MSG_GOTO(rc, cleanup, "Copying of value failed");
        break;
    default:
        SR_LOG_WRN_MSG("Get value is not implemented for this node type");
        rc = SR_ERR_INTERNAL;
        goto cleanup;
    }
    return SR_ERR_OK;

cleanup:
    sr_free_val_content(val);
    return rc;
}

int
rp_dt_get_values_from_nodes(struct ly_set *nodes, sr_val_t **values, size_t *value_cnt)
{
    CHECK_NULL_ARG2(nodes, values);
    int rc = SR_ERR_OK;
    sr_val_t *vals = NULL;
    size_t cnt = 0;
    struct lyd_node *node = NULL;

    vals = calloc(nodes->number, sizeof(*vals));
    CHECK_NULL_NOMEM_RETURN(vals);

    for (size_t i = 0; i < nodes->number; i++) {
        node = nodes->set.d[i];
        if (NULL == node || NULL == node->schema || LYS_RPC == node->schema->nodetype ||
            LYS_NOTIF == node->schema->nodetype || LYS_ACTION == node->schema->nodetype) {
            /* ignore this node */
            continue;
        }
        rc = rp_dt_get_value_from_node(node, &vals[i]);
        if (SR_ERR_OK != rc) {
            SR_LOG_ERR("Getting value from node %s failed", node->schema->name);
            sr_free_values(vals, i);
            return SR_ERR_INTERNAL;
        }
        cnt++;
    }

    *values = vals;
    *value_cnt = cnt;

    return rc;
}

int
rp_dt_get_value(const dm_ctx_t *dm_ctx, struct lyd_node *data_tree, const char *xpath, bool check_enabled, sr_val_t **value)
{
    CHECK_NULL_ARG4(dm_ctx, data_tree, xpath, value);
    int rc = SR_ERR_OK;
    sr_val_t *val = NULL;
    struct lyd_node *node = NULL;

    rc = rp_dt_find_node(dm_ctx, data_tree, xpath, check_enabled, &node);
    if (SR_ERR_OK != rc) {
        if (SR_ERR_NOT_FOUND != rc) {
            SR_LOG_ERR("Find node failed (%d) xpath %s", rc, xpath);
        }
        return rc;
    }

    val = calloc(1, sizeof(*val));
    CHECK_NULL_NOMEM_RETURN(val);

    rc = rp_dt_get_value_from_node(node, val);
    if (SR_ERR_OK != rc) {
        SR_LOG_ERR("Get value from node failed for xpath %s", xpath);
        free(val);
    } else {
        *value = val;
    }

    return rc;
}

int
rp_dt_get_values(const dm_ctx_t *dm_ctx, struct lyd_node *data_tree, const char *xpath, bool check_enable, sr_val_t **values, size_t *count)
{
    CHECK_NULL_ARG5(dm_ctx, data_tree, xpath, values, count);

    int rc = SR_ERR_OK;

    struct ly_set *nodes = NULL;
    rc = rp_dt_find_nodes(dm_ctx, data_tree, xpath, check_enable, &nodes);
    if (SR_ERR_OK != rc) {
        if (SR_ERR_NOT_FOUND != rc) {
            SR_LOG_ERR("Get nodes for xpath %s failed (%d)", xpath, rc);
        }
        return rc;
    }

    rc = rp_dt_get_values_from_nodes(nodes, values, count);
    if (SR_ERR_OK != rc) {
        SR_LOG_ERR("Copying values from nodes failed for xpath '%s'", xpath);
    }

    ly_set_free(nodes);
    return SR_ERR_OK;
}

/**
 * @brief Determines if (and what) state data subtrees are needed to be loaded.
 */
static int
rp_dt_xpath_requests_state_data(rp_ctx_t *rp_ctx, const char *module_name, const char *xpath, np_subscription_t ***subscriptions_arr, size_t *subscriptions_cnt)
{
    CHECK_NULL_ARG4(rp_ctx, xpath, subscriptions_arr, subscriptions_cnt);
    int rc = SR_ERR_OK;
    np_subscription_t **subs = NULL;
    size_t subs_cnt = 0;

    rc = np_get_data_provider_subscriptions(rp_ctx->np_ctx, module_name, &subs, &subs_cnt);
    CHECK_RC_MSG_RETURN(rc, "Get data provider subscriptions failed");

    //TODO: optimize which data are loaded, a predicate refer to a node at the different level or state node
    //TODO: optimize xpath which should be used in request

    *subscriptions_arr = subs;
    *subscriptions_cnt = subs_cnt;

    SR_LOG_DBG("%zu data providers asked for data in order to resolve %s", *subscriptions_cnt, xpath);

    return rc;
}

int
rp_dt_remove_loaded_state_data(rp_ctx_t *rp_ctx, rp_session_t *rp_session)
{
    CHECK_NULL_ARG2(rp_ctx, rp_session);
    int rc = SR_ERR_OK;

    while (rp_session->loaded_state_data[rp_session->datastore]->count > 0) {
        char *item_xpath = (char *) rp_session->loaded_state_data[rp_session->datastore]->data[rp_session->loaded_state_data[rp_session->datastore]->count-1];
        rc = rp_dt_delete_item(rp_ctx->dm_ctx, rp_session->dm_session, item_xpath, SR_EDIT_DEFAULT);
        CHECK_RC_LOG_RETURN(rc, "Error %s occured while removing state data for xpath %s", sr_strerror(rc), item_xpath);
        sr_list_rm(rp_session->loaded_state_data[rp_session->datastore], item_xpath);
        free(item_xpath);
    }

    return rc;
}

/**
 * @brief
 *
 * @note Temporary solution will be removed as soon as the logic for loading
 * only subset of state data is implemented
 *
 * @param [in] rp_ctx
 * @param [in] rp_session
 * @param [in] module_name
 * @return Error code (SR_ERR_OK on success)
 */
static int
rp_dt_mark_all_state_data_in_module_as_loaded(rp_ctx_t *rp_ctx, rp_session_t *rp_session, const char *module_name)
{
    CHECK_NULL_ARG3(rp_ctx, rp_session, module_name);
    int rc = SR_ERR_OK;
    md_ctx_t *md_ctx = NULL;
    md_module_t *module = NULL;
    char *xpath = NULL;

    rc = dm_get_md_ctx(rp_ctx->dm_ctx, &md_ctx);
    CHECK_RC_MSG_RETURN(rc,"Failed to retrieve md_ctx");

    md_ctx_lock(md_ctx, false);

    rc = md_get_module_info(md_ctx, rp_session->module_name, NULL, &module);
    CHECK_RC_LOG_GOTO(rc, cleanup, "Module %s was not found in module dependency", rp_session->module_name);

    sr_llist_node_t *node = module->op_data_subtrees->first;
    while (NULL != node) {
        md_subtree_ref_t *sub = node->data;
        xpath = strdup(sub->xpath);
        CHECK_NULL_NOMEM_GOTO(xpath, rc, cleanup);
        rc = sr_list_add(rp_session->loaded_state_data[rp_session->datastore], xpath);
        CHECK_RC_MSG_GOTO(rc, cleanup, "List add failed");
        xpath = NULL;
        node = node->next;
    }

cleanup:
    md_ctx_unlock(md_ctx);
    free(xpath);
    return rc;
}

/**
 * @brief Loads configuration data and asks for state data if needed. Request
 * can enter this function in RP_REQ_NEW state or RP_REQ_FINISHED.
 *
 * In RP_REQ_NEW state saves the data tree name into session.
 *
 * @param [in] rp_ctx
 * @param [in] rp_session
 * @param [in] xpath
 * @param [out] data_tree
 * @return Error code (SR_ERR_OK on success)
 */
static int
rp_dt_prepare_data(rp_ctx_t *rp_ctx, rp_session_t *rp_session, const char *xpath, struct lyd_node **data_tree)
{
    CHECK_NULL_ARG4(rp_ctx, rp_session, xpath, data_tree);
    int rc = SR_ERR_OK;
    bool has_state_data = false;
    np_subscription_t **subscriptions = NULL;
    size_t subscription_cnt = 0;

    if (RP_REQ_NEW == rp_session->state) {

        /* in case of get_items_with_opts module name is not freed to save some
         * copying in case of cache hit */
        free(rp_session->module_name);
        rp_session->module_name = NULL;

        rc = rp_dt_remove_loaded_state_data(rp_ctx, rp_session);
        CHECK_RC_MSG_GOTO(rc, cleanup, "Failed to remove state data from data tree");

        rc = sr_copy_first_ns(xpath, &rp_session->module_name);
        CHECK_RC_LOG_GOTO(rc, cleanup, "Copying module name failed for xpath '%s'", xpath);

        rc = ac_check_node_permissions(rp_session->ac_session, xpath, AC_OPER_READ);
        CHECK_RC_LOG_GOTO(rc, cleanup, "Access control check failed for xpath '%s'", xpath);

        rc = dm_get_datatree(rp_ctx->dm_ctx, rp_session->dm_session, rp_session->module_name, data_tree);

        /* check of data tree's emptiness is performed outside of this function -> ignore SR_ERR_NOT_FOUND */
        rc = SR_ERR_NOT_FOUND == rc ? SR_ERR_OK : rc;
        CHECK_RC_LOG_GOTO(rc, cleanup, "Getting data tree failed (%d) for xpath '%s'", rc, xpath);

        /* if the request requires operational data pause the processing and wait for data to be provided */
        if ((SR_DS_RUNNING == rp_session->datastore || SR_DS_CANDIDATE == rp_session->datastore) &&
            (!(SR_SESS_CONFIG_ONLY & rp_session->options)) &&
            (!(SR__SESSION_FLAGS__SESS_NOTIFICATION & rp_session->options)) &&
            (SR_ERR_OK == dm_has_state_data(rp_ctx->dm_ctx, rp_session->module_name, &has_state_data) && has_state_data)) {

            rc = rp_dt_xpath_requests_state_data(rp_ctx, rp_session->module_name, xpath, &subscriptions, &subscription_cnt);
            CHECK_RC_MSG_GOTO(rc, cleanup, "rp_dt_xpath_requests_state_data failed");

            if (0 == subscription_cnt) {
                SR_LOG_DBG("No state state data provider is asked for data because of xpath %s", xpath);
            }

            for (size_t i = 0; i < subscription_cnt; i++) {
                char *xp = (char *) subscriptions[i]->xpath; /* Todo: xpath that should be used for initial data request */
                rc = np_data_provider_request(rp_ctx->np_ctx, subscriptions[i], rp_session, xp);
                SR_LOG_DBG("Sending request for state data: %s", xp);
                if (SR_ERR_OK != rc) {
                    SR_LOG_WRN("Request for operational data failed with xpath %s on subscription %s", xp, subscriptions[i]->xpath);
                } else {
                    rp_session->dp_req_waiting += 1;
                }
                //TODO: subscriptions should be freed after all potential subsequent request for nested data has been sent
                np_free_subscription(subscriptions[i]);
            }
            free(subscriptions);

            if (rp_session->dp_req_waiting > 0) {
                rp_session->state = RP_REQ_WAITING_FOR_DATA;
            }

            //TODO: mark only subtrees that were actually loaded
            rc = rp_dt_mark_all_state_data_in_module_as_loaded(rp_ctx, rp_session, rp_session->module_name);
            CHECK_RC_MSG_GOTO(rc, cleanup, "Mark all module state data as loaded failed");

        }
        CHECK_RC_MSG_GOTO(rc, cleanup, "rp_dt_module_has_state data failed");

    } else if (RP_REQ_DATA_LOADED == rp_session->state) {
        SR_LOG_DBG("Session id = %u data loaded, continue processing", rp_session->id);
        rc = dm_get_datatree(rp_ctx->dm_ctx, rp_session->dm_session, rp_session->module_name, data_tree);
        /* check of data tree's emptiness is performed outside of this function -> ignore SR_ERR_NOT_FOUND */
        rc = SR_ERR_NOT_FOUND == rc ? SR_ERR_OK : rc;
    } else {
        SR_LOG_ERR("Session id = %u is in invalid state.", rp_session->id);
        rc = SR_ERR_INTERNAL;
    }

cleanup:
    return rc;
}

int
rp_dt_get_value_wrapper(rp_ctx_t *rp_ctx, rp_session_t *rp_session, const char *xpath, sr_val_t **value)
{
    CHECK_NULL_ARG4(rp_ctx, rp_ctx->dm_ctx, rp_session, rp_session->dm_session);
    CHECK_NULL_ARG2(xpath, value);
    SR_LOG_INF("Get item request %s datastore, xpath: %s", sr_ds_to_str(rp_session->datastore), xpath);

    int rc = SR_ERR_OK;
    struct lyd_node *data_tree = NULL;

    rc = rp_dt_prepare_data(rp_ctx, rp_session, xpath, &data_tree);
    CHECK_RC_LOG_GOTO(rc, cleanup, "rp_dt_prepare_data failed %s", sr_strerror(rc));

    if (RP_REQ_WAITING_FOR_DATA == rp_session->state) {
        SR_LOG_DBG("Session id = %u is waiting for the data", rp_session->id);
        return rc;
    }

    if (NULL == data_tree) {
        goto cleanup;
    }

    rc = rp_dt_get_value(rp_ctx->dm_ctx, data_tree, xpath, dm_is_running_ds_session(rp_session->dm_session), value);
cleanup:
    if (SR_ERR_NOT_FOUND == rc || (SR_ERR_OK == rc && NULL == data_tree)) {
        rc = rp_dt_validate_node_xpath(rp_ctx->dm_ctx, NULL, xpath, NULL, NULL);
        if (SR_ERR_OK != rc) {
            /* Print warning only, because we are not able to validate all xpath */
            SR_LOG_WRN("Validation of xpath %s was not successful", xpath);
        }
        rc = SR_ERR_NOT_FOUND;
    } else if (SR_ERR_OK != rc) {
        SR_LOG_ERR("Get value failed for xpath '%s'", xpath);
    }

    rp_session->state = RP_REQ_FINISHED;
    free(rp_session->module_name);
    rp_session->module_name = NULL;
    return rc;
}

int
rp_dt_get_values_wrapper(rp_ctx_t *rp_ctx, rp_session_t *rp_session, const char *xpath, sr_val_t **values, size_t *count)
{
    CHECK_NULL_ARG4(rp_ctx, rp_ctx->dm_ctx, rp_session, rp_session->dm_session);
    CHECK_NULL_ARG3(xpath, values, count);
    SR_LOG_INF("Get items request %s datastore, xpath: %s", sr_ds_to_str(rp_session->datastore), xpath);

    int rc = SR_ERR_OK;
    struct lyd_node *data_tree = NULL;

    rc = rp_dt_prepare_data(rp_ctx, rp_session, xpath, &data_tree);
    CHECK_RC_MSG_GOTO(rc, cleanup, "rp_dt_prepare_data failed");

    if (RP_REQ_WAITING_FOR_DATA == rp_session->state) {
        SR_LOG_DBG("Session id = %u is waiting for the data", rp_session->id);
        return rc;
    }

    if (NULL == data_tree) {
        goto cleanup;
    }

    rc = rp_dt_get_values(rp_ctx->dm_ctx, data_tree, xpath, dm_is_running_ds_session(rp_session->dm_session), values, count);
    if (SR_ERR_OK != rc && SR_ERR_NOT_FOUND != rc) {
        SR_LOG_ERR("Get values failed for xpath '%s'", xpath);
    }

cleanup:
    if (SR_ERR_NOT_FOUND == rc || (SR_ERR_OK == rc && (0 == count || NULL == data_tree))) {
        if (SR_ERR_OK != rp_dt_validate_node_xpath(rp_ctx->dm_ctx, NULL, xpath, NULL, NULL)) {
            /* Print warning only, because we are not able to validate all xpath */
            SR_LOG_WRN("Validation of xpath %s was not successful", xpath);
        }
        rc = SR_ERR_NOT_FOUND;
    }
    rp_session->state = RP_REQ_FINISHED;
    free(rp_session->module_name);
    rp_session->module_name = NULL;
    return rc;
}

int
rp_dt_get_values_wrapper_with_opts(rp_ctx_t *rp_ctx, rp_session_t *rp_session, rp_dt_get_items_ctx_t *get_items_ctx, const char *xpath,
        size_t offset, size_t limit, sr_val_t **values, size_t *count)
{
    CHECK_NULL_ARG5(rp_ctx, rp_ctx->dm_ctx, rp_session, rp_session->dm_session, get_items_ctx);
    CHECK_NULL_ARG3(xpath, values, count);
    SR_LOG_INF("Get items request %s datastore, xpath: %s, offset: %zu, limit: %zu", sr_ds_to_str(rp_session->datastore), xpath, offset, limit);

    int rc = SR_ERR_OK;
    struct lyd_node *data_tree = NULL;
    struct ly_set *nodes = NULL;

    if (get_items_ctx->xpath != NULL && 0 == strcmp(xpath, get_items_ctx->xpath) &&
            offset == get_items_ctx->offset) {
        /* cache hit do not load data from data providers */
        rp_session->state = RP_REQ_DATA_LOADED;
    }

    rc = rp_dt_prepare_data(rp_ctx, rp_session, xpath, &data_tree);
    CHECK_RC_MSG_GOTO(rc, cleanup, "rp_dt_prepare_data failed");

    if (RP_REQ_WAITING_FOR_DATA == rp_session->state) {
        SR_LOG_DBG("Session id = %u is waiting for the data", rp_session->id);
        return rc;
    }

    if (NULL == data_tree) {
        goto cleanup;
    }

    rc = rp_dt_find_nodes_with_opts(rp_ctx->dm_ctx, rp_session->dm_session, get_items_ctx, data_tree, xpath, offset, limit, &nodes);
    if (SR_ERR_OK != rc) {
        if (SR_ERR_NOT_FOUND != rc) {
            SR_LOG_ERR("Get nodes for xpath %s failed (%d)", xpath, rc);
        }
        goto cleanup;
    }

    rc = rp_dt_get_values_from_nodes(nodes, values, count);
cleanup:
    if (SR_ERR_NOT_FOUND == rc || (SR_ERR_OK == rc && (0 == count || NULL == data_tree))) {
        rc = rp_dt_validate_node_xpath(rp_ctx->dm_ctx, NULL, xpath, NULL, NULL);
        if (SR_ERR_OK != rc) {
            /* Print warning only, because we are not able to validate all xpath */
            SR_LOG_WRN("Validation of xpath %s was not successful", xpath);
        }
        rc = SR_ERR_NOT_FOUND;
    } else if (SR_ERR_OK != rc) {
        SR_LOG_ERR("Copying values from nodes failed for xpath '%s'", xpath);
    }

    ly_set_free(nodes);
    rp_session->state = RP_REQ_FINISHED;
    return rc;

}

/**
 * @brief generates changes for the children of created/deleted container/list
 *
 */
static int
rp_dt_add_changes_for_children(sr_list_t *changes, LYD_DIFFTYPE type, struct lyd_node *node)
{
    CHECK_NULL_ARG2(changes, node);
    int rc = SR_ERR_OK;
    struct lyd_node *next = NULL, *elem = NULL;
    sr_change_t *ch = NULL;

    LY_TREE_DFS_BEGIN(node, next, elem) {
        ch = calloc(1, sizeof(*ch));
        CHECK_NULL_NOMEM_GOTO(ch, rc, cleanup);

        ch->oper = type == LYD_DIFF_CREATED ?  SR_OP_CREATED : SR_OP_DELETED;
        ch->sch_node = elem->schema;

        sr_val_t **ptr = LYD_DIFF_CREATED == type ? &ch->new_value : &ch->old_value;
        *ptr = calloc(1, sizeof(**ptr));
        CHECK_NULL_NOMEM_GOTO(*ptr, rc, cleanup);
        rc = rp_dt_get_value_from_node(elem, *ptr);
        CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");

        rc = sr_list_add(changes, ch);
        CHECK_RC_MSG_GOTO(rc, cleanup, "List add failed");
        ch = NULL;
        LYD_TREE_DFS_END(node, next, elem);
    }
cleanup:
    if (NULL != ch) {
        sr_free_changes(ch, 1);
    }
    return rc;
}

int
rp_dt_difflist_to_changes(struct lyd_difflist *difflist, sr_list_t **changes)
{
    CHECK_NULL_ARG2(difflist, changes);
    int rc = SR_ERR_OK;
    sr_change_t *ch = NULL;

    sr_list_t *list = NULL;
    rc = sr_list_init(&list);
    CHECK_RC_MSG_RETURN(rc, "List init failed");

    for(size_t d_cnt = 0; LYD_DIFF_END != difflist->type[d_cnt]; d_cnt++) {
        if (!(LYD_DIFF_CREATED == difflist->type[d_cnt] && (LYS_LIST | LYS_CONTAINER) & difflist->second[d_cnt]->schema->nodetype) &&
            !(LYD_DIFF_DELETED == difflist->type[d_cnt] && (LYS_LIST | LYS_CONTAINER) & difflist->first[d_cnt]->schema->nodetype)) {
            ch = calloc(1, sizeof(*ch));
            CHECK_NULL_NOMEM_GOTO(ch, rc, cleanup);
        }

        switch (difflist->type[d_cnt]) {
        case LYD_DIFF_CREATED:
            if ((LYS_LIST | LYS_CONTAINER) & difflist->second[d_cnt]->schema->nodetype) {
                rc = rp_dt_add_changes_for_children(list, difflist->type[d_cnt], difflist->second[d_cnt]);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Add changes for children failed");
            } else {
                ch->oper = SR_OP_CREATED;
                ch->sch_node = difflist->second[d_cnt]->schema;
                ch->new_value = calloc(1, sizeof(*ch->new_value));
                CHECK_NULL_NOMEM_GOTO(ch->new_value, rc, cleanup);
                rc = rp_dt_get_value_from_node(difflist->second[d_cnt], ch->new_value);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            }
            break;
        case LYD_DIFF_DELETED:
            if ((LYS_LIST | LYS_CONTAINER) & difflist->first[d_cnt]->schema->nodetype) {
                rc = rp_dt_add_changes_for_children(list, difflist->type[d_cnt], difflist->first[d_cnt]);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Add changes for children failed");
            } else {
                ch->oper = SR_OP_DELETED;
                ch->sch_node = difflist->first[d_cnt]->schema;
                ch->old_value = calloc(1, sizeof(*ch->old_value));
                CHECK_NULL_NOMEM_GOTO(ch->old_value, rc, cleanup);
                rc = rp_dt_get_value_from_node(difflist->first[d_cnt], ch->old_value);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            }
            break;
        case LYD_DIFF_MOVEDAFTER1:
            ch->oper = SR_OP_MOVED;
            ch->sch_node = difflist->first[d_cnt]->schema;

            if (NULL != difflist->second[d_cnt]){
                ch->old_value = calloc(1, sizeof(*ch->old_value));
                CHECK_NULL_NOMEM_GOTO(ch->old_value, rc, cleanup);
                rc = rp_dt_get_value_from_node(difflist->second[d_cnt], ch->old_value);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            }

            ch->new_value = calloc(1, sizeof(*ch->new_value));
            CHECK_NULL_NOMEM_GOTO(ch->new_value, rc, cleanup);
            rc = rp_dt_get_value_from_node(difflist->first[d_cnt], ch->new_value);
            CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            break;
        case LYD_DIFF_MOVEDAFTER2:
            ch->oper = SR_OP_MOVED;
            ch->sch_node = difflist->second[d_cnt]->schema;

            if (NULL != difflist->first[d_cnt]){
                ch->old_value = calloc(1, sizeof(*ch->old_value));
                CHECK_NULL_NOMEM_GOTO(ch->old_value, rc, cleanup);
                rc = rp_dt_get_value_from_node(difflist->first[d_cnt], ch->old_value);
                CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            }

            ch->new_value = calloc(1, sizeof(*ch->new_value));
            CHECK_NULL_NOMEM_GOTO(ch->new_value, rc, cleanup);
            rc = rp_dt_get_value_from_node(difflist->second[d_cnt], ch->new_value);
            CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
            break;
        default:
            /* case LYD_DIFF_CHANGED */
            ch->oper = SR_OP_MODIFIED;
            ch->sch_node = difflist->first[d_cnt]->schema;

            ch->old_value = calloc(1, sizeof(*ch->old_value));
            rc = rp_dt_get_value_from_node(difflist->first[d_cnt], ch->old_value);
            CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");

            ch->new_value = calloc(1, sizeof(*ch->new_value));
            rc = rp_dt_get_value_from_node(difflist->second[d_cnt], ch->new_value);
            CHECK_RC_MSG_GOTO(rc, cleanup, "Get value from node failed");
        }

        if (NULL != ch) {
            rc = sr_list_add(list, ch);
            CHECK_RC_MSG_GOTO(rc, cleanup, "List add failed");
            ch = NULL;
        }

    }

cleanup:
    if (SR_ERR_OK != rc) {
        if (NULL != ch) {
            sr_free_changes(ch, 1);
        }
        for (int i = 0; i < list->count; i++) {
            sr_free_changes(list->data[i], 1);
        }
        sr_list_cleanup(list);
    } else {
        *changes = list;
    }
    return rc;
}

int
rp_dt_get_changes(rp_ctx_t *rp_ctx, rp_session_t *rp_session, dm_commit_context_t *c_ctx, const char *xpath,
        size_t offset, size_t limit, sr_list_t **matched_changes)
{
    CHECK_NULL_ARG4(rp_ctx, rp_session, c_ctx, xpath);
    CHECK_NULL_ARG(matched_changes);

    int rc = SR_ERR_OK;
    char *module_name = NULL;
    dm_model_subscription_t lookup = {0};
    dm_model_subscription_t *ms = NULL;

    rc = sr_copy_first_ns(xpath, &module_name);
    CHECK_RC_MSG_RETURN(rc, "Copy first ns failed");

    rc = dm_get_module(rp_ctx->dm_ctx, module_name, NULL, &lookup.module);
    CHECK_RC_LOG_GOTO(rc, cleanup, "Dm get module failed for %s", module_name);

    ms = sr_btree_search(c_ctx->subscriptions, &lookup);
    if (NULL == ms) {
        SR_LOG_ERR("Module subscription not found for module %s", lookup.module->name);
        rc = SR_ERR_INTERNAL;
        goto cleanup;
    }


    RWLOCK_RDLOCK_TIMED_CHECK_GOTO(&ms->changes_lock, rc, cleanup);

    /* generate changes on demand */
    if (!ms->changes_generated) {
        pthread_rwlock_unlock(&ms->changes_lock);
        /* acquire write lock */
        RWLOCK_WRLOCK_TIMED_CHECK_GOTO(&ms->changes_lock, rc, cleanup);
        /* check if some generated the changes meanwhile */
        if (!ms->changes_generated) {
            rc = rp_dt_difflist_to_changes(ms->difflist, &ms->changes);
            if (SR_ERR_OK != rc) {
                SR_LOG_ERR_MSG("Difflist to changes failed");
                pthread_rwlock_unlock(&ms->changes_lock);
                goto cleanup;
            }
            ms->changes_generated = true;
        }
    }

    rc = rp_dt_find_changes(rp_ctx->dm_ctx, rp_session->dm_session, ms, &rp_session->change_ctx, xpath, offset, limit, matched_changes);
    pthread_rwlock_unlock(&ms->changes_lock);

    if (SR_ERR_NOT_FOUND != rc) {
        CHECK_RC_LOG_GOTO(rc, cleanup, "Find changes failed for %s", xpath);
    }

cleanup:
    free(module_name);
    return rc;
}
