/*
 * Route map northbound implementation.
 *
 * Copyright (C) 2019 Network Device Education Foundation, Inc. ("NetDEF")
 *                    Rafael Zalamena
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <zebra.h>

#include "lib/command.h"
#include "lib/log.h"
#include "lib/northbound.h"
#include "lib/routemap.h"

/*
 * Auxiliary functions to avoid code duplication:
 *
 * lib_route_map_entry_set_destroy: unset `set` commands.
 * lib_route_map_entry_match_destroy: unset `match` commands.
 */
int lib_route_map_entry_match_destroy(enum nb_event event,
				      const struct lyd_node *dnode)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	rhc = nb_running_get_entry(dnode, NULL, true);
	if (rhc->rhc_mhook == NULL)
		return NB_OK;

	rv = rhc->rhc_mhook(NULL, rhc->rhc_rmi, rhc->rhc_rule, NULL,
			    rhc->rhc_event);
	if (rv != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;

	return NB_OK;
}

int lib_route_map_entry_set_destroy(enum nb_event event,
				    const struct lyd_node *dnode)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	rhc = nb_running_get_entry(dnode, NULL, true);
	if (rhc->rhc_shook == NULL)
		return NB_OK;

	rv = rhc->rhc_shook(NULL, rhc->rhc_rmi, rhc->rhc_rule, NULL);
	if (rv != CMD_SUCCESS)
		return NB_ERR_INCONSISTENCY;

	return NB_OK;
}

/*
 * Auxiliary hook context list manipulation functions.
 */
struct routemap_hook_context *
routemap_hook_context_insert(struct route_map_index *rmi)
{
	struct routemap_hook_context *rhc;

	rhc = XCALLOC(MTYPE_TMP, sizeof(*rhc));
	rhc->rhc_rmi = rmi;
	TAILQ_INSERT_TAIL(&rmi->rhclist, rhc, rhc_entry);

	return rhc;
}

void
routemap_hook_context_free(struct routemap_hook_context *rhc)
{
	struct route_map_index *rmi = rhc->rhc_rmi;

	TAILQ_REMOVE(&rmi->rhclist, rhc, rhc_entry);
	XFREE(MTYPE_TMP, rhc);
}

/*
 * XPath: /frr-route-map:lib/route-map
 */
static int lib_route_map_create(enum nb_event event,
				const struct lyd_node *dnode,
				union nb_resource *resource)
{
	struct route_map *rm;
	const char *rm_name;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rm_name = yang_dnode_get_string(dnode, "./name");
		rm = route_map_get(rm_name);
		nb_running_set_entry(dnode, rm);
		break;
	}

	return NB_OK;
}

static int lib_route_map_destroy(enum nb_event event,
				 const struct lyd_node *dnode)
{
	struct route_map *rm;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rm = nb_running_unset_entry(dnode);
		route_map_delete(rm);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry
 */
static int lib_route_map_entry_create(enum nb_event event,
				      const struct lyd_node *dnode,
				      union nb_resource *resource)
{
	struct route_map_index *rmi;
	struct route_map *rm;
	uint16_t sequence;
	int action;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		sequence = yang_dnode_get_uint16(dnode, "./sequence");
		action = yang_dnode_get_enum(dnode, "./action") == 0
				 ? RMAP_PERMIT
				 : RMAP_DENY;
		rm = nb_running_get_entry(dnode, NULL, true);
		rmi = route_map_index_get(rm, action, sequence);
		nb_running_set_entry(dnode, rmi);
		break;
	}

	return NB_OK;
}

static int lib_route_map_entry_destroy(enum nb_event event,
				       const struct lyd_node *dnode)
{
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_unset_entry(dnode);
		route_map_index_delete(rmi, 1);
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/description
 */
static int lib_route_map_entry_description_modify(enum nb_event event,
						  const struct lyd_node *dnode,
						  union nb_resource *resource)
{
	struct route_map_index *rmi;
	const char *description;

	switch (event) {
	case NB_EV_VALIDATE:
		/* NOTHING */
		break;
	case NB_EV_PREPARE:
		description = yang_dnode_get_string(dnode, NULL);
		resource->ptr = XSTRDUP(MTYPE_TMP, description);
		if (resource->ptr == NULL)
			return NB_ERR_RESOURCE;
		break;
	case NB_EV_ABORT:
		XFREE(MTYPE_TMP, resource->ptr);
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		if (rmi->description != NULL)
			XFREE(MTYPE_TMP, rmi->description);
		rmi->description = resource->ptr;
		break;
	}

	return NB_OK;
}

static int lib_route_map_entry_description_destroy(enum nb_event event,
						   const struct lyd_node *dnode)
{
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		if (rmi->description != NULL)
			XFREE(MTYPE_TMP, rmi->description);
		rmi->description = NULL;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/action
 */
static int lib_route_map_entry_action_modify(enum nb_event event,
					     const struct lyd_node *dnode,
					     union nb_resource *resource)
{
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		rmi->type = yang_dnode_get_enum(dnode, NULL);
		/* TODO: notify? */
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/call
 */
static int lib_route_map_entry_call_modify(enum nb_event event,
					   const struct lyd_node *dnode,
					   union nb_resource *resource)
{
	struct route_map_index *rmi;
	const char *rm_name, *rmn_name;

	switch (event) {
	case NB_EV_VALIDATE:
		rm_name = yang_dnode_get_string(dnode, "../../name");
		rmn_name = yang_dnode_get_string(dnode, NULL);
		/* Don't allow to jump to the same route map instance. */
		if (strcmp(rm_name, rmn_name) == 0)
			return NB_ERR_VALIDATION;

		/* TODO: detect circular route map sequences. */
		break;
	case NB_EV_PREPARE:
		rmn_name = yang_dnode_get_string(dnode, NULL);
		resource->ptr = XSTRDUP(MTYPE_ROUTE_MAP_NAME, rmn_name);
		break;
	case NB_EV_ABORT:
		XFREE(MTYPE_ROUTE_MAP_NAME, resource->ptr);
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		if (rmi->nextrm) {
			route_map_upd8_dependency(RMAP_EVENT_CALL_DELETED,
						  rmi->nextrm, rmi->map->name);
			XFREE(MTYPE_ROUTE_MAP_NAME, rmi->nextrm);
		}
		rmi->nextrm = resource->ptr;
		route_map_upd8_dependency(RMAP_EVENT_CALL_ADDED, rmi->nextrm,
					  rmi->map->name);
		break;
	}

	return NB_OK;
}

static int lib_route_map_entry_call_destroy(enum nb_event event,
					    const struct lyd_node *dnode)
{
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		route_map_upd8_dependency(RMAP_EVENT_CALL_DELETED, rmi->nextrm,
					  rmi->map->name);
		XFREE(MTYPE_ROUTE_MAP_NAME, rmi->nextrm);
		rmi->nextrm = NULL;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/exit-policy
 */
static int lib_route_map_entry_exit_policy_modify(enum nb_event event,
						  const struct lyd_node *dnode,
						  union nb_resource *resource)
{
	struct route_map_index *rmi;
	int rm_action;
	int policy;

	switch (event) {
	case NB_EV_VALIDATE:
		policy = yang_dnode_get_enum(dnode, NULL);
		switch (policy) {
		case 0: /* permit-or-deny */
			break;
		case 1: /* next */
			/* FALLTHROUGH */
		case 2: /* goto */
			rm_action = yang_dnode_get_enum(dnode, "../action");
			if (rm_action == 1 /* deny */) {
				/*
				 * On deny it is not possible to 'goto'
				 * anywhere.
				 */
				return NB_ERR_VALIDATION;
			}
			break;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		policy = yang_dnode_get_enum(dnode, NULL);

		switch (policy) {
		case 0: /* permit-or-deny */
			rmi->exitpolicy = RMAP_EXIT;
			break;
		case 1: /* next */
			rmi->exitpolicy = RMAP_NEXT;
			break;
		case 2: /* goto */
			rmi->exitpolicy = RMAP_GOTO;
			break;
		}
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/goto-value
 */
static int lib_route_map_entry_goto_value_modify(enum nb_event event,
						 const struct lyd_node *dnode,
						 union nb_resource *resource)
{
	struct route_map_index *rmi;
	uint16_t rmi_index;
	uint16_t rmi_next;

	switch (event) {
	case NB_EV_VALIDATE:
		rmi_index = yang_dnode_get_uint16(dnode, "../sequence");
		rmi_next = yang_dnode_get_uint16(dnode, NULL);
		if (rmi_next <= rmi_index) {
			/* Can't jump backwards on a route map. */
			return NB_ERR_VALIDATION;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		rmi->nextpref = yang_dnode_get_uint16(dnode, NULL);
		break;
	}

	return NB_OK;
}

static int lib_route_map_entry_goto_value_destroy(enum nb_event event,
						  const struct lyd_node *dnode)
{
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		rmi->nextpref = 0;
		break;
	}

	return NB_OK;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition
 */
static int
lib_route_map_entry_match_condition_create(enum nb_event event,
					   const struct lyd_node *dnode,
					   union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	struct route_map_index *rmi;

	switch (event) {
	case NB_EV_VALIDATE:
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		/* NOTHING */
		break;
	case NB_EV_APPLY:
		rmi = nb_running_get_entry(dnode, NULL, true);
		rhc = routemap_hook_context_insert(rmi);
		nb_running_set_entry(dnode, rhc);
		break;
	}

	return NB_OK;
}

static int
lib_route_map_entry_match_condition_destroy(enum nb_event event,
					    const struct lyd_node *dnode)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	rv = lib_route_map_entry_match_destroy(event, dnode);
	rhc = nb_running_unset_entry(dnode);
	routemap_hook_context_free(rhc);

	return rv;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/interface
 */
static int lib_route_map_entry_match_condition_interface_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *ifname;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_interface == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	ifname = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_interface;
	rhc->rhc_rule = "interface";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_interface(NULL, rhc->rhc_rmi,
						 "interface", ifname,
						 RMAP_EVENT_MATCH_ADDED);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_match_condition_interface_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/access-list-num
 */
static int lib_route_map_entry_match_condition_access_list_num_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *acl;
	int condition, rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	rv = CMD_SUCCESS;
	acl = yang_dnode_get_string(dnode, NULL);
	rhc = nb_running_get_entry(dnode, NULL, true);
	condition = yang_dnode_get_enum(dnode, "../condition");
	switch (condition) {
	case 1: /* ipv4-address-list */
		if (rmap_match_set_hook.match_ip_address == NULL)
			break;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_address;
		rhc->rhc_rule = "ip address";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_address(
			NULL, rhc->rhc_rmi, "ip address", acl,
			RMAP_EVENT_FILTER_ADDED);
		break;
	case 3: /* ipv4-next-hop-list */
		if (rmap_match_set_hook.match_ip_next_hop == NULL)
			break;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_next_hop;
		rhc->rhc_rule = "ip next-hop";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_next_hop(
			NULL, rhc->rhc_rmi, "ip next-hop", acl,
			RMAP_EVENT_FILTER_ADDED);
		break;
	}
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_match_condition_access_list_num_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath:
 * /frr-route-map:lib/route-map/entry/match-condition/access-list-num-extended
 */
static int lib_route_map_entry_match_condition_access_list_num_extended_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	return lib_route_map_entry_match_condition_access_list_num_modify(
		event, dnode, resource);
}

static int lib_route_map_entry_match_condition_access_list_num_extended_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_condition_access_list_num_destroy(
		event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/list-name
 */
static int lib_route_map_entry_match_condition_list_name_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *acl;
	int condition;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook installation, otherwise we can just stop. */
	acl = yang_dnode_get_string(dnode, NULL);
	rhc = nb_running_get_entry(dnode, NULL, true);
	condition = yang_dnode_get_enum(dnode, "../condition");
	switch (condition) {
	case 1: /* ipv4-address-list */
		if (rmap_match_set_hook.match_ip_address == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_address;
		rhc->rhc_rule = "ip address";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_address(
			NULL, rhc->rhc_rmi, "ip address", acl,
			RMAP_EVENT_FILTER_ADDED);
		break;
	case 2: /* ipv4-prefix-list */
		if (rmap_match_set_hook.match_ip_address_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ip_address_prefix_list;
		rhc->rhc_rule = "ip address prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ip_address_prefix_list(
			NULL, rhc->rhc_rmi, "ip address prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED);
		break;
	case 3: /* ipv4-next-hop-list */
		if (rmap_match_set_hook.match_ip_next_hop == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_next_hop;
		rhc->rhc_rule = "ip next-hop";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ip_next_hop(
			NULL, rhc->rhc_rmi, "ip next-hop", acl,
			RMAP_EVENT_FILTER_ADDED);
		break;
	case 4: /* ipv4-next-hop-prefix-list */
		if (rmap_match_set_hook.match_ip_next_hop_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ip_next_hop_prefix_list;
		rhc->rhc_rule = "ip next-hop prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ip_next_hop_prefix_list(
			NULL, rhc->rhc_rmi, "ip next-hop prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED);
		break;
	case 6: /* ipv6-address-list */
		if (rmap_match_set_hook.match_ipv6_address == NULL)
			return NB_OK;
		rhc->rhc_mhook = rmap_match_set_hook.no_match_ipv6_address;
		rhc->rhc_rule = "ipv6 address";
		rhc->rhc_event = RMAP_EVENT_FILTER_DELETED;
		rv = rmap_match_set_hook.match_ipv6_address(
			NULL, rhc->rhc_rmi, "ipv6 address", acl,
			RMAP_EVENT_FILTER_ADDED);
		break;
	case 7: /* ipv6-prefix-list */
		if (rmap_match_set_hook.match_ipv6_address_prefix_list == NULL)
			return NB_OK;
		rhc->rhc_mhook =
			rmap_match_set_hook.no_match_ipv6_address_prefix_list;
		rhc->rhc_rule = "ipv6 address prefix-list";
		rhc->rhc_event = RMAP_EVENT_PLIST_DELETED;
		rv = rmap_match_set_hook.match_ipv6_address_prefix_list(
			NULL, rhc->rhc_rmi, "ipv6 address prefix-list", acl,
			RMAP_EVENT_PLIST_ADDED);
		break;
	default:
		rv = CMD_ERR_NO_MATCH;
		break;
	}
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_match_condition_list_name_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/ipv4-next-hop-type
 */
static int lib_route_map_entry_match_condition_ipv4_next_hop_type_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_ip_next_hop_type == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	type = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_ip_next_hop_type;
	rhc->rhc_rule = "ip next-hop type";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_ip_next_hop_type(
		NULL, rhc->rhc_rmi, "ip next-hop type", type,
		RMAP_EVENT_MATCH_ADDED);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_match_condition_ipv4_next_hop_type_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/ipv6-next-hop-type
 */
static int lib_route_map_entry_match_condition_ipv6_next_hop_type_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_ipv6_next_hop_type == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	type = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_ipv6_next_hop_type;
	rhc->rhc_rule = "ipv6 next-hop type";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_ipv6_next_hop_type(
		NULL, rhc->rhc_rmi, "ipv6 next-hop type", type,
		RMAP_EVENT_MATCH_ADDED);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_match_condition_ipv6_next_hop_type_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/metric
 */
static int
lib_route_map_entry_match_condition_metric_modify(enum nb_event event,
						  const struct lyd_node *dnode,
						  union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *type;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	type = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_metric;
	rhc->rhc_rule = "metric";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_metric(NULL, rhc->rhc_rmi, "metric",
					      type, RMAP_EVENT_MATCH_ADDED);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int
lib_route_map_entry_match_condition_metric_destroy(enum nb_event event,
						   const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/match-condition/tag
 */
static int
lib_route_map_entry_match_condition_tag_modify(enum nb_event event,
					       const struct lyd_node *dnode,
					       union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *tag;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.match_tag == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	tag = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_mhook = rmap_match_set_hook.no_match_tag;
	rhc->rhc_rule = "tag";
	rhc->rhc_event = RMAP_EVENT_MATCH_DELETED;

	rv = rmap_match_set_hook.match_tag(NULL, rhc->rhc_rmi, "tag", tag,
					   RMAP_EVENT_MATCH_ADDED);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_mhook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int
lib_route_map_entry_match_condition_tag_destroy(enum nb_event event,
						const struct lyd_node *dnode)
{
	return lib_route_map_entry_match_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action
 */
static int lib_route_map_entry_set_action_create(enum nb_event event,
						 const struct lyd_node *dnode,
						 union nb_resource *resource)
{
	return lib_route_map_entry_match_condition_create(event, dnode,
							  resource);
}

static int lib_route_map_entry_set_action_destroy(enum nb_event event,
						  const struct lyd_node *dnode)
{
	struct routemap_hook_context *rhc;
	int rv;

	if (event != NB_EV_APPLY)
		return NB_OK;

	rv = lib_route_map_entry_set_destroy(event, dnode);
	rhc = nb_running_unset_entry(dnode);
	routemap_hook_context_free(rhc);

	return rv;
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/ipv4-address
 */
static int
lib_route_map_entry_set_action_ipv4_address_modify(enum nb_event event,
						   const struct lyd_node *dnode,
						   union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *address;
	struct in_addr ia;
	int rv;

	switch (event) {
	case NB_EV_VALIDATE:
		/*
		 * NOTE: validate if 'action' is 'ipv4-next-hop',
		 * currently it is not necessary because this is the
		 * only implemented action.
		 */
		yang_dnode_get_ipv4(&ia, dnode, NULL);
		if (ia.s_addr == INADDR_ANY || IPV4_CLASS_DE(ntohl(ia.s_addr)))
			return NB_ERR_VALIDATION;
		/* FALLTHROUGH */
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		return NB_OK;
	case NB_EV_APPLY:
		break;
	}

	/* Check for hook function. */
	if (rmap_match_set_hook.set_ip_nexthop == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	address = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_ip_nexthop;
	rhc->rhc_rule = "ip next-hop";

	rv = rmap_match_set_hook.set_ip_nexthop(NULL, rhc->rhc_rmi,
						"ip next-hop", address);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_set_action_ipv4_address_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/ipv6-address
 */
static int
lib_route_map_entry_set_action_ipv6_address_modify(enum nb_event event,
						   const struct lyd_node *dnode,
						   union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *address;
	struct in6_addr i6a;
	int rv;

	switch (event) {
	case NB_EV_VALIDATE:
		/*
		 * NOTE: validate if 'action' is 'ipv6-next-hop',
		 * currently it is not necessary because this is the
		 * only implemented action. Other actions might have
		 * different validations.
		 */
		yang_dnode_get_ipv6(&i6a, dnode, NULL);
		if (!IN6_IS_ADDR_LINKLOCAL(&i6a))
			return NB_ERR_VALIDATION;
		/* FALLTHROUGH */
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		return NB_OK;
	case NB_EV_APPLY:
		break;
	}

	/* Check for hook function. */
	if (rmap_match_set_hook.set_ipv6_nexthop_local == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	address = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_ipv6_nexthop_local;
	rhc->rhc_rule = "ipv6 next-hop local";

	rv = rmap_match_set_hook.set_ipv6_nexthop_local(
		NULL, rhc->rhc_rmi, "ipv6 next-hop local", address);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int lib_route_map_entry_set_action_ipv6_address_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/value
 */
static int set_action_modify(enum nb_event event, const struct lyd_node *dnode,
			     union nb_resource *resource, const char *value)
{
	struct routemap_hook_context *rhc;
	int rv;

	/*
	 * NOTE: validate if 'action' is 'metric', currently it is not
	 * necessary because this is the only implemented action. Other
	 * actions might have different validations.
	 */
	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_metric == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_metric;
	rhc->rhc_rule = "metric";

	rv = rmap_match_set_hook.set_metric(NULL, rhc->rhc_rmi, "metric",
					    value);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int
lib_route_map_entry_set_action_value_modify(enum nb_event event,
					    const struct lyd_node *dnode,
					    union nb_resource *resource)
{
	const char *metric = yang_dnode_get_string(dnode, NULL);

	return set_action_modify(event, dnode, resource, metric);
}

static int
lib_route_map_entry_set_action_value_destroy(enum nb_event event,
					     const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/add-metric
 */
static int
lib_route_map_entry_set_action_add_metric_modify(enum nb_event event,
						 const struct lyd_node *dnode,
						 union nb_resource *resource)
{
	return set_action_modify(event, dnode, resource, "+metric");
}

static int
lib_route_map_entry_set_action_add_metric_destroy(enum nb_event event,
						  const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_action_value_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/subtract-metric
 */
static int lib_route_map_entry_set_action_subtract_metric_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	return set_action_modify(event, dnode, resource, "-metric");
}

static int lib_route_map_entry_set_action_subtract_metric_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_action_value_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/use-round-trip-time
 */
static int lib_route_map_entry_set_action_use_round_trip_time_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	return set_action_modify(event, dnode, resource, "rtt");
}

static int lib_route_map_entry_set_action_use_round_trip_time_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_action_value_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/add-round-trip-time
 */
static int lib_route_map_entry_set_action_add_round_trip_time_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	return set_action_modify(event, dnode, resource, "+rtt");
}

static int lib_route_map_entry_set_action_add_round_trip_time_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_action_value_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/subtract-round-trip-time
 */
static int lib_route_map_entry_set_action_subtract_round_trip_time_modify(
	enum nb_event event, const struct lyd_node *dnode,
	union nb_resource *resource)
{
	return set_action_modify(event, dnode, resource, "-rtt");
}

static int lib_route_map_entry_set_action_subtract_round_trip_time_destroy(
	enum nb_event event, const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_action_value_destroy(event, dnode);
}

/*
 * XPath: /frr-route-map:lib/route-map/entry/set-action/tag
 */
static int
lib_route_map_entry_set_action_tag_modify(enum nb_event event,
					  const struct lyd_node *dnode,
					  union nb_resource *resource)
{
	struct routemap_hook_context *rhc;
	const char *tag;
	int rv;

	/*
	 * NOTE: validate if 'action' is 'tag', currently it is not
	 * necessary because this is the only implemented action. Other
	 * actions might have different validations.
	 */
	if (event != NB_EV_APPLY)
		return NB_OK;

	/* Check for hook function. */
	if (rmap_match_set_hook.set_tag == NULL)
		return NB_OK;

	/* Add configuration. */
	rhc = nb_running_get_entry(dnode, NULL, true);
	tag = yang_dnode_get_string(dnode, NULL);

	/* Set destroy information. */
	rhc->rhc_shook = rmap_match_set_hook.no_set_tag;
	rhc->rhc_rule = "tag";

	rv = rmap_match_set_hook.set_tag(NULL, rhc->rhc_rmi, "tag", tag);
	if (rv != CMD_SUCCESS) {
		rhc->rhc_shook = NULL;
		return NB_ERR_INCONSISTENCY;
	}

	return NB_OK;
}

static int
lib_route_map_entry_set_action_tag_destroy(enum nb_event event,
					   const struct lyd_node *dnode)
{
	return lib_route_map_entry_set_destroy(event, dnode);
}

/* clang-format off */
#if defined(__GNUC__) && ((__GNUC__ - 0) < 5) && !defined(__clang__)
/*
 * gcc versions before 5.x miscalculate the size for structs with variable
 * length arrays (they just count it as size 0)
 */
struct frr_yang_module_info_sizen {
	/* YANG module name. */
	const char *name;

	/* Northbound callbacks. */
	const struct {
		/* Data path of this YANG node. */
		const char *xpath;

		/* Callbacks implemented for this node. */
		struct nb_callbacks cbs;

		/* Priority - lower priorities are processed first. */
		uint32_t priority;
	} nodes[28];
};

const struct frr_yang_module_info_sizen frr_route_map_info_sizen asm("frr_route_map_info") = {
#else
const struct frr_yang_module_info frr_route_map_info = {
#endif
	.name = "frr-route-map",
	.nodes = {
		{
			.xpath = "/frr-route-map:lib/route-map",
			.cbs = {
				.create = lib_route_map_create,
				.destroy = lib_route_map_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry",
			.cbs = {
				.create = lib_route_map_entry_create,
				.destroy = lib_route_map_entry_destroy,
				.cli_show = route_map_instance_show,
				.cli_show_end = route_map_instance_show_end,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/description",
			.cbs = {
				.modify = lib_route_map_entry_description_modify,
				.destroy = lib_route_map_entry_description_destroy,
				.cli_show = route_map_description_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/action",
			.cbs = {
				.modify = lib_route_map_entry_action_modify,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/call",
			.cbs = {
				.modify = lib_route_map_entry_call_modify,
				.destroy = lib_route_map_entry_call_destroy,
				.cli_show = route_map_call_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/exit-policy",
			.cbs = {
				.modify = lib_route_map_entry_exit_policy_modify,
				.cli_show = route_map_exit_policy_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/goto-value",
			.cbs = {
				.modify = lib_route_map_entry_goto_value_modify,
				.destroy = lib_route_map_entry_goto_value_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition",
			.cbs = {
				.create = lib_route_map_entry_match_condition_create,
				.destroy = lib_route_map_entry_match_condition_destroy,
				.cli_show = route_map_condition_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/interface",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_interface_modify,
				.destroy = lib_route_map_entry_match_condition_interface_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/access-list-num",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_access_list_num_modify,
				.destroy = lib_route_map_entry_match_condition_access_list_num_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/access-list-num-extended",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_access_list_num_extended_modify,
				.destroy = lib_route_map_entry_match_condition_access_list_num_extended_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/list-name",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_list_name_modify,
				.destroy = lib_route_map_entry_match_condition_list_name_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/ipv4-next-hop-type",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_ipv4_next_hop_type_modify,
				.destroy = lib_route_map_entry_match_condition_ipv4_next_hop_type_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/ipv6-next-hop-type",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_ipv6_next_hop_type_modify,
				.destroy = lib_route_map_entry_match_condition_ipv6_next_hop_type_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/metric",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_metric_modify,
				.destroy = lib_route_map_entry_match_condition_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/match-condition/tag",
			.cbs = {
				.modify = lib_route_map_entry_match_condition_tag_modify,
				.destroy = lib_route_map_entry_match_condition_tag_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action",
			.cbs = {
				.create = lib_route_map_entry_set_action_create,
				.destroy = lib_route_map_entry_set_action_destroy,
				.cli_show = route_map_action_show,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/ipv4-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_ipv4_address_modify,
				.destroy = lib_route_map_entry_set_action_ipv4_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/ipv6-address",
			.cbs = {
				.modify = lib_route_map_entry_set_action_ipv6_address_modify,
				.destroy = lib_route_map_entry_set_action_ipv6_address_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/value",
			.cbs = {
				.modify = lib_route_map_entry_set_action_value_modify,
				.destroy = lib_route_map_entry_set_action_value_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/add-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_add_metric_modify,
				.destroy = lib_route_map_entry_set_action_add_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/subtract-metric",
			.cbs = {
				.modify = lib_route_map_entry_set_action_subtract_metric_modify,
				.destroy = lib_route_map_entry_set_action_subtract_metric_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/use-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_use_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_use_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/add-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_add_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_add_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/subtract-round-trip-time",
			.cbs = {
				.modify = lib_route_map_entry_set_action_subtract_round_trip_time_modify,
				.destroy = lib_route_map_entry_set_action_subtract_round_trip_time_destroy,
			}
		},
		{
			.xpath = "/frr-route-map:lib/route-map/entry/set-action/tag",
			.cbs = {
				.modify = lib_route_map_entry_set_action_tag_modify,
				.destroy = lib_route_map_entry_set_action_tag_destroy,
			}
		},
		{
			.xpath = NULL,
		},
	}
};
