/**
 * @defgroup rp_ft Data tree content filtering.
 * @ingroup rp
 * @{
 * @brief Functions for filtering data tree content before converting
 * it from libyang representation into sysrepo data structures.
 * @file rp_dt_filter.h
 * @author Milan Lenco <milan.lenco@pantheon.tech>
 *
 * @copyright
 * Copyright 2016 Pantheon Technologies, s.r.o.
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

#ifndef RP_DT_FILTER_H
#define RP_DT_FILTER_H

#include <stdbool.h>
#include "nacm.h"
#include "rp_internal.h"

/**
 * @brief Tree pruning context as used by Request processor.
 */
typedef struct rp_tree_pruning_ctx_s {
    bool check_enabled;
    nacm_data_val_ctx_t *nacm_data_val_ctx;
} rp_tree_pruning_ctx_t;

/**
 * @brief Filter-out nodes from a given data tree that the user doesn't have permission to read.
 * If copy-on-write is enabled, the tree is duplicated, but only if some nodes have been actually
 * filtered out. In that case, make sure to compare pointers *data_tree* and *result* to learn
 * if they point to the same tree or not.
 *
 * @param [in] dm_ctx Data manager context.
 * @param [in] user_credentials Credentials of the user accessing the data tree.
 * @param [in] data_tree Data tree to copy and filter.
 * @param [in] copy_on_write Flag if the tree should be duplicated before editing. If disabled, the
 *                           tree will be modified in-place.
 * @param [out] result Resulting data tree.
 */
int rp_dt_nacm_filtering(dm_ctx_t *dm_ctx, const ac_ucred_t *user_credentials, struct lyd_node *data_tree,
        bool copy_on_write, struct lyd_node **result);

/**
 * @brief Filter data tree nodes by NACM read access.
 *
 * @param [in] dm_ctx Data manager context.
 * @param [in] user_credentials Credentials of the user accessing the nodes.
 * @param [in] data_tree Data tree from which the nodes have been acquired.
 * @param [in, out] nodes An array of nodes to filter.
 * @param [in, out] node_cnt Number of nodes before and after the filtering.
 */
int rp_dt_nodes_nacm_filtering(dm_ctx_t *dm_ctx, const ac_ucred_t *user_credentials, struct lyd_node *data_tree,
        struct lyd_node **nodes, unsigned int *node_cnt);

/**
 * @brief Intialize and start Request processor tree pruning. Trees will be pruned based on the
 * NACM configuration and persistent data.
 *
 * @param [in] dm_ctx Data manager context.
 * @param [in] user_credentials Credentials of the user accessing the tree.
 * @param [in] root Root of the tree to prune.
 * @param [in] enable_nacm Flag if the NACM is enabled for this session.
 * @param [in] data_tree Data tree to which the root belongs to.
 * @param [in] check_enabled Prune away subtrees which are not enabled.
 * @param [out] pruning_cb Pruning callback to use for ::sr_copy_node_to_tree and the like.
 * @param [out] pruning_ctx Pruning context to use with the callback.
 */
int rp_dt_init_tree_pruning(dm_ctx_t *dm_ctx, const ac_ucred_t *user_credentials, bool enable_nacm, struct lyd_node *root,
        struct lyd_node *data_tree, bool check_enabled, sr_tree_pruning_cb *pruning_cb, rp_tree_pruning_ctx_t **pruning_ctx);

/**
 * @brief Stop tree pruning and deallocate all memory associated with the context.
 *
 * @param [in] pruning_ctx Pruning context to destroy.
 */
void rp_dt_cleanup_tree_pruning(rp_tree_pruning_ctx_t *pruning_ctx);

#endif /* RP_DT_FILTER_H */

/**
 * @}
 */
