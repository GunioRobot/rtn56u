/*
  File: acl_delete_entry.c

  Copyright (C) 1999, 2000
  Andreas Gruenbacher, <a.gruenbacher@computer.org>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "libacl.h"


/* 23.4.9 */
int
acl_delete_entry(acl_t acl, acl_entry_t entry_d)
{
	acl_obj *acl_obj_p = ext2int(acl, acl);
	acl_entry_obj *entry_obj_p = ext2int(acl_entry, entry_d);
	if (!acl_obj_p || !entry_obj_p)
		return -1;


	if (acl_obj_p->acurr == entry_obj_p)
		acl_obj_p->acurr = acl_obj_p->acurr->eprev;
	entry_obj_p->eprev->enext = entry_obj_p->enext;
	entry_obj_p->enext->eprev = entry_obj_p->eprev;

	free_obj_p(entry_obj_p);
	acl_obj_p->aused--;
	return 0;
}

