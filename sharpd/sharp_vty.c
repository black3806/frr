/*
 * SHARP - vty code
 * Copyright (C) Cumulus Networks, Inc.
 *               Donald Sharp
 *
 * This file is part of FRR.
 *
 * FRR is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * FRR is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; see the file COPYING; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <zebra.h>

#include "vty.h"
#include "command.h"
#include "prefix.h"
#include "nexthop.h"
#include "log.h"
#include "vrf.h"
#include "zclient.h"
#include "nexthop_group.h"

#include "sharpd/sharp_zebra.h"
#include "sharpd/sharp_vty.h"
#ifndef VTYSH_EXTRACT_PL
#include "sharpd/sharp_vty_clippy.c"
#endif

extern uint32_t total_routes;
extern uint32_t installed_routes;
extern uint32_t removed_routes;

uint8_t inst;
struct prefix prefix;
struct prefix orig_prefix;
struct nexthop nhop;
struct nexthop_group nhop_group;
uint32_t rts;
int32_t repeat;

DEFPY(watch_nexthop_v6, watch_nexthop_v6_cmd,
      "sharp watch nexthop X:X::X:X$nhop",
      "Sharp routing Protocol\n"
      "Watch for changes\n"
      "Watch for nexthop changes\n"
      "The v6 nexthop to signal for watching\n")
{
	struct prefix p;

	memset(&p, 0, sizeof(p));

	p.prefixlen = 128;
	memcpy(&p.u.prefix6, &nhop, 16);
	p.family = AF_INET6;

	sharp_zebra_nexthop_watch(&p, true);

	return CMD_SUCCESS;
}

DEFPY(watch_nexthop_v4, watch_nexthop_v4_cmd,
      "sharp watch nexthop A.B.C.D$nhop",
      "Sharp routing Protocol\n"
      "Watch for changes\n"
      "Watch for nexthop changes\n"
      "The v4 nexthop to signal for watching\n")
{
	struct prefix p;

	memset(&p, 0, sizeof(p));

	p.prefixlen = 32;
	p.u.prefix4 = nhop;
	p.family = AF_INET;

	sharp_zebra_nexthop_watch(&p, true);

	return CMD_SUCCESS;
}

DEFPY (install_routes,
       install_routes_cmd,
       "sharp install routes A.B.C.D$start <nexthop <A.B.C.D$nexthop4|X:X::X:X$nexthop6>|nexthop-group NAME$nexthop_group> (1-1000000)$routes [instance (0-255)$instance] [repeat (2-1000)$rpt]",
       "Sharp routing Protocol\n"
       "install some routes\n"
       "Routes to install\n"
       "Address to start /32 generation at\n"
       "Nexthop to use(Can be an IPv4 or IPv6 address)\n"
       "V4 Nexthop address to use\n"
       "V6 Nexthop address to use\n"
       "Nexthop-Group to use\n"
       "The Name of the nexthop-group\n"
       "How many to create\n"
       "Instance to use\n"
       "Instance\n"
       "Should we repeat this command\n"
       "How many times to repeat this command\n")
{
	total_routes = routes;
	installed_routes = 0;

	if (rpt >= 2)
		repeat = rpt * 2;
	else
		repeat = 0;

	memset(&prefix, 0, sizeof(prefix));
	memset(&orig_prefix, 0, sizeof(orig_prefix));
	memset(&nhop, 0, sizeof(nhop));
	memset(&nhop_group, 0, sizeof(nhop_group));

	prefix.family = AF_INET;
	prefix.prefixlen = 32;
	prefix.u.prefix4 = start;
	orig_prefix = prefix;

	if (nexthop_group) {
		struct nexthop_group_cmd *nhgc = nhgc_find(nexthop_group);
		if (!nhgc) {
			vty_out(vty,
				"Specified Nexthop Group: %s does not exist\n",
				nexthop_group);
			return CMD_WARNING;
		}

		nhop_group.nexthop = nhgc->nhg.nexthop;
	} else {
		if (nexthop4.s_addr != INADDR_ANY) {
			nhop.gate.ipv4 = nexthop4;
			nhop.type = NEXTHOP_TYPE_IPV4;
		} else {
			nhop.gate.ipv6 = nexthop6;
			nhop.type = NEXTHOP_TYPE_IPV6;
		}

		nhop_group.nexthop = &nhop;
	}

	inst = instance;
	rts = routes;
	sharp_install_routes_helper(&prefix, inst, &nhop_group, rts);

	return CMD_SUCCESS;
}

DEFPY(vrf_label, vrf_label_cmd,
      "sharp label <ip$ipv4|ipv6$ipv6> vrf NAME$name label (0-100000)$label",
      "Sharp Routing Protocol\n"
      "Give a vrf a label\n"
      "Pop and forward for IPv4\n"
      "Pop and forward for IPv6\n"
      VRF_CMD_HELP_STR
      "The label to use, 0 specifies remove the label installed from previous\n"
      "Specified range to use\n")
{
	struct vrf *vrf;
	afi_t afi = (ipv4) ? AFI_IP : AFI_IP6;

	if (strcmp(name, "default") == 0)
		vrf = vrf_lookup_by_id(VRF_DEFAULT);
	else
		vrf = vrf_lookup_by_name(name);

	if (!vrf) {
		vty_out(vty, "Unable to find vrf you silly head");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (label == 0)
		label = MPLS_LABEL_NONE;

	vrf_label_add(vrf->vrf_id, afi, label);
	return CMD_SUCCESS;
}

DEFPY (remove_routes,
       remove_routes_cmd,
       "sharp remove routes A.B.C.D$start (1-1000000)$routes [instance (0-255)$instance]",
       "Sharp Routing Protocol\n"
       "Remove some routes\n"
       "Routes to remove\n"
       "Starting spot\n"
       "Routes to uniinstall\n"
       "instance to use\n"
       "Value of instance\n")
{
	total_routes = routes;
	removed_routes = 0;

	memset(&prefix, 0, sizeof(prefix));

	prefix.family = AF_INET;
	prefix.prefixlen = 32;
	prefix.u.prefix4 = start;

	inst = instance;
	rts = routes;
	sharp_remove_routes_helper(&prefix, inst, rts);

	return CMD_SUCCESS;
}

DEFUN_NOSH (show_debugging_sharpd,
	    show_debugging_sharpd_cmd,
	    "show debugging [sharp]",
	    SHOW_STR
	    DEBUG_STR
	    "Sharp Information\n")
{
	vty_out(vty, "Sharp debugging status\n");

	return CMD_SUCCESS;
}

void sharp_vty_init(void)
{
	install_element(ENABLE_NODE, &install_routes_cmd);
	install_element(ENABLE_NODE, &remove_routes_cmd);
	install_element(ENABLE_NODE, &vrf_label_cmd);
	install_element(ENABLE_NODE, &watch_nexthop_v6_cmd);
	install_element(ENABLE_NODE, &watch_nexthop_v4_cmd);

	install_element(VIEW_NODE, &show_debugging_sharpd_cmd);

	return;
}
