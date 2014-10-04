/*
 * init/mode/run_normal.c
 *
 * Copyright(c) 2007-2014 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include <xboot.h>
#include <xboot/menu.h>
#include <shell/exec.h>

void run_normal_mode(void)
{
	struct menu_item_t * item;

	do {
		item = get_menu_indexof_item(0);

		if(item && item->title && item->command)
			exec_cmdline(item->command);
		else
			xboot_set_mode(MODE_SHELL);

	} while(xboot_get_mode() == MODE_NORMAL);
}
